#include "core/edit.h"

#include <algorithm>
#include <limits>

namespace sharkcut {

namespace {

ObjectId newId(Project& project) {
    return project.nextId++;
}

bool contains(const std::vector<ObjectId>& ids, ObjectId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void insertSorted(Track& track, const Clip& clip) {
    const auto it =
        std::lower_bound(track.clips.begin(), track.clips.end(), clip.start,
                         [](const Clip& c, std::int64_t start) { return c.start < start; });
    track.clips.insert(it, clip);
}

// Despeja [start, end) en la pista: los clips tapados se recortan, se parten o se quitan.
void clearRange(Project& project, std::size_t trackIndex, std::int64_t start, std::int64_t end) {
    Track& track = project.tracks[trackIndex];
    std::vector<Clip> result;
    result.reserve(track.clips.size() + 1);
    for (const Clip& clip : track.clips) {
        if (clip.end() <= start || clip.start >= end) {
            result.push_back(clip);
            continue;
        }
        if (clip.start < start) { // sobrevive la parte de la izquierda
            Clip left = clip;
            left.length = start - clip.start;
            result.push_back(left);
        }
        if (clip.end() > end) { // sobrevive la parte de la derecha
            Clip right = clip;
            if (clip.start < start) {
                right.id = newId(project); // el clip se partió en dos
            }
            const std::int64_t cut = end - clip.start;
            right.start = end;
            right.in = clip.in + cut;
            right.length = clip.end() - end;
            result.push_back(right);
        }
    }
    track.clips = std::move(result);
}

} // namespace

ObjectId addMedia(Project& project, MediaItem item) {
    item.id = newId(project);
    project.media.push_back(std::move(item));
    return project.media.back().id;
}

bool removeMedia(Project& project, ObjectId media) {
    const auto removed =
        std::erase_if(project.media, [media](const MediaItem& item) { return item.id == media; });
    if (removed == 0) {
        return false;
    }
    for (Track& track : project.tracks) {
        std::erase_if(track.clips, [media](const Clip& clip) { return clip.media == media; });
    }
    relinkClips(project);
    return true;
}

ObjectId placeClip(Project& project, ObjectId media, std::int64_t start, std::int64_t in,
                   std::int64_t length) {
    const MediaItem* item = project.findMedia(media);
    if (!item || start < 0 || in < 0) {
        return 0;
    }
    length = std::min(length, project.mediaFrames(*item) - in);
    if (length <= 0) {
        return 0;
    }

    ObjectId first = 0;
    auto put = [&](std::size_t trackIndex) {
        if (trackIndex >= project.tracks.size()) {
            return;
        }
        clearRange(project, trackIndex, start, start + length);
        const Clip clip{newId(project), media, 0, start, in, length};
        insertSorted(project.tracks[trackIndex], clip);
        if (first == 0) {
            first = clip.id;
        }
    };
    if (item->hasVideo) {
        put(kVideoTrack);
    }
    if (item->hasAudio) {
        put(kAudioTrack);
    }
    relinkClips(project);
    return first;
}

ObjectId placeClip(Project& project, ObjectId media, std::int64_t start) {
    const MediaItem* item = project.findMedia(media);
    return item ? placeClip(project, media, start, 0, project.mediaFrames(*item)) : 0;
}

bool splitAt(Project& project, std::int64_t frame, const std::vector<ObjectId>& clips) {
    const bool all = clips.empty();
    const std::vector<ObjectId> targets =
        all ? std::vector<ObjectId>{} : withLinked(project, clips);
    bool changed = false;
    for (Track& track : project.tracks) {
        for (std::size_t i = 0; i < track.clips.size(); ++i) {
            const Clip clip = track.clips[i];
            if (clip.start >= frame || frame >= clip.end() ||
                (!all && !contains(targets, clip.id))) {
                continue;
            }
            Clip right = clip;
            right.id = newId(project);
            right.start = frame;
            right.in = clip.in + (frame - clip.start);
            right.length = clip.end() - frame;
            track.clips[i].length = frame - clip.start;
            track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(i) + 1, right);
            ++i;
            changed = true;
        }
    }
    if (changed) {
        relinkClips(project);
    }
    return changed;
}

bool removeClips(Project& project, const std::vector<ObjectId>& clips) {
    const std::vector<ObjectId> group = withLinked(project, clips);
    if (group.empty()) {
        return false;
    }
    for (Track& track : project.tracks) {
        std::erase_if(track.clips, [&group](const Clip& clip) { return contains(group, clip.id); });
    }
    relinkClips(project);
    return true;
}

std::int64_t limitTrimStart(const Project& project, ObjectId clip, std::int64_t start) {
    std::int64_t lo = 0;
    std::int64_t hi = std::numeric_limits<std::int64_t>::max();
    std::int64_t current = start;
    for (ObjectId id : withLinked(project, {clip})) {
        const auto ref = project.findClip(id);
        const Track& track = project.tracks[ref->track];
        const Clip& c = track.clips[ref->index];
        current = c.start;
        lo = std::max(lo, c.start - c.in); // no antes del inicio del medio
        if (ref->index > 0) {
            lo = std::max(lo, track.clips[ref->index - 1].end()); // ni sobre el anterior
        }
        hi = std::min(hi, c.end() - 1); // al menos un fotograma
    }
    if (lo > hi) {
        return current;
    }
    return std::clamp(start, lo, hi);
}

bool trimStart(Project& project, ObjectId clip, std::int64_t start) {
    const Clip* target = project.clip(clip);
    if (!target) {
        return false;
    }
    const std::int64_t delta = limitTrimStart(project, clip, start) - target->start;
    if (delta == 0) {
        return false;
    }
    for (ObjectId id : withLinked(project, {clip})) {
        const auto ref = project.findClip(id);
        Clip& c = project.tracks[ref->track].clips[ref->index];
        c.start += delta;
        c.in += delta;
        c.length -= delta;
    }
    relinkClips(project);
    return true;
}

std::int64_t limitTrimEnd(const Project& project, ObjectId clip, std::int64_t end) {
    std::int64_t lo = 0;
    std::int64_t hi = std::numeric_limits<std::int64_t>::max();
    std::int64_t current = end;
    for (ObjectId id : withLinked(project, {clip})) {
        const auto ref = project.findClip(id);
        const Track& track = project.tracks[ref->track];
        const Clip& c = track.clips[ref->index];
        current = c.end();
        lo = std::max(lo, c.start + 1); // al menos un fotograma
        if (const MediaItem* item = project.findMedia(c.media)) {
            hi = std::min(hi, c.start + project.mediaFrames(*item) - c.in); // fin del medio
        }
        if (ref->index + 1 < track.clips.size()) {
            hi = std::min(hi, track.clips[ref->index + 1].start); // sin pisar el siguiente
        }
    }
    if (lo > hi) {
        return current;
    }
    return std::clamp(end, lo, hi);
}

bool trimEnd(Project& project, ObjectId clip, std::int64_t end) {
    const Clip* target = project.clip(clip);
    if (!target) {
        return false;
    }
    const std::int64_t delta = limitTrimEnd(project, clip, end) - target->end();
    if (delta == 0) {
        return false;
    }
    for (ObjectId id : withLinked(project, {clip})) {
        const auto ref = project.findClip(id);
        project.tracks[ref->track].clips[ref->index].length += delta;
    }
    relinkClips(project);
    return true;
}

std::int64_t limitMove(const Project& project, const std::vector<ObjectId>& clips,
                       std::int64_t delta) {
    const std::vector<ObjectId> group = withLinked(project, clips);
    if (group.empty()) {
        return 0;
    }
    std::int64_t first = std::numeric_limits<std::int64_t>::max();
    for (ObjectId id : group) {
        first = std::min(first, project.clip(id)->start);
    }
    return std::max(delta, -first);
}

bool moveClips(Project& project, const std::vector<ObjectId>& clips, std::int64_t delta) {
    const std::vector<ObjectId> group = withLinked(project, clips);
    delta = limitMove(project, group, delta);
    if (group.empty() || delta == 0) {
        return false;
    }
    // Se sacan los clips que se mueven, se despeja su destino y se vuelven a poner.
    std::vector<std::vector<Clip>> moving(project.tracks.size());
    for (std::size_t t = 0; t < project.tracks.size(); ++t) {
        auto& trackClips = project.tracks[t].clips;
        for (const Clip& clip : trackClips) {
            if (contains(group, clip.id)) {
                Clip moved = clip;
                moved.start += delta;
                moving[t].push_back(moved);
            }
        }
        std::erase_if(trackClips, [&group](const Clip& clip) { return contains(group, clip.id); });
    }
    for (std::size_t t = 0; t < moving.size(); ++t) {
        for (const Clip& clip : moving[t]) {
            clearRange(project, t, clip.start, clip.end());
        }
        for (const Clip& clip : moving[t]) {
            insertSorted(project.tracks[t], clip);
        }
    }
    relinkClips(project);
    return true;
}

std::vector<ObjectId> withLinked(const Project& project, const std::vector<ObjectId>& clips) {
    std::vector<ObjectId> result;
    auto add = [&](ObjectId id) {
        if (id != 0 && !contains(result, id) && project.clip(id)) {
            result.push_back(id);
        }
    };
    for (ObjectId id : clips) {
        if (const Clip* clip = project.clip(id)) {
            add(id);
            add(clip->link);
        }
    }
    return result;
}

void relinkClips(Project& project) {
    for (Track& track : project.tracks) {
        for (Clip& clip : track.clips) {
            clip.link = 0;
        }
    }
    if (project.tracks.size() <= kAudioTrack) {
        return;
    }
    auto& video = project.tracks[kVideoTrack].clips;
    auto& audio = project.tracks[kAudioTrack].clips;
    std::size_t j = 0;
    for (Clip& v : video) {
        while (j < audio.size() && audio[j].start < v.start) {
            ++j;
        }
        if (j < audio.size()) {
            Clip& a = audio[j];
            if (a.start == v.start && a.media == v.media && a.in == v.in && a.length == v.length) {
                v.link = a.id;
                a.link = v.id;
            }
        }
    }
}

} // namespace sharkcut
