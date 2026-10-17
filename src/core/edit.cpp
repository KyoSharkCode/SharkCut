#include "core/edit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>

namespace sharkcut {

namespace {

// Clips partidos durante una operación: original → parte derecha (nuevo id) y el
// enlace que tenía el original. Sirve para enlazar entre sí las dos partes derechas
// de un par enlazado que se partió por el mismo sitio.
struct SplitInfo {
    ObjectId right = 0;
    ObjectId link = 0;
};
using Splits = std::map<ObjectId, SplitInfo>;

ObjectId newId(Project& project) {
    return project.nextId++;
}

bool contains(const std::vector<ObjectId>& ids, ObjectId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

Clip* findMutable(Project& project, ObjectId id) {
    const auto ref = project.findClip(id);
    return ref ? &project.tracks[ref->track].clips[ref->index] : nullptr;
}

bool inLockedTrack(const Project& project, ObjectId id) {
    const auto ref = project.findClip(id);
    return ref && project.tracks[ref->track].locked;
}

bool anyLocked(const Project& project, const std::vector<ObjectId>& ids) {
    return std::any_of(ids.begin(), ids.end(),
                       [&project](ObjectId id) { return inLockedTrack(project, id); });
}

void insertSorted(Track& track, const Clip& clip) {
    const auto it =
        std::lower_bound(track.clips.begin(), track.clips.end(), clip.start,
                         [](const Clip& c, std::int64_t start) { return c.start < start; });
    track.clips.insert(it, clip);
}

// Despeja [start, end) en la pista: los clips tapados se recortan, se parten o se quitan.
void clearRange(Project& project, std::size_t trackIndex, std::int64_t start, std::int64_t end,
                Splits& splits) {
    std::vector<Clip> result;
    result.reserve(project.tracks[trackIndex].clips.size() + 1);
    for (const Clip& clip : project.tracks[trackIndex].clips) {
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
                splits[clip.id] = {right.id, clip.link};
            }
            const std::int64_t cut = end - clip.start;
            right.start = end;
            right.in = clip.in + cut;
            right.length = clip.end() - end;
            result.push_back(right);
        }
    }
    project.tracks[trackIndex].clips = std::move(result);
}

bool sameRange(const Clip& a, const Clip& b) {
    return a.media == b.media && a.start == b.start && a.in == b.in && a.length == b.length;
}

const Clip* clipInTrack(const Track& track, ObjectId id) {
    for (const Clip& clip : track.clips) {
        if (clip.id == id) {
            return &clip;
        }
    }
    return nullptr;
}

TransitionLimits limitsIn(const Project& project, const Transition& t) {
    TransitionLimits limits;
    const Clip* a = project.clip(t.from);
    const Clip* b = project.clip(t.to);
    if (!a || !b) {
        return limits;
    }
    const Transition* intoA = project.transitionInto(a->id);
    const Transition* outOfB = project.transitionOutOf(b->id);
    limits.maxBefore = std::max<std::int64_t>(0, a->length - (intoA ? intoA->after : 0));
    limits.maxAfter = std::max<std::int64_t>(0, b->length - (outOfB ? outOfB->before : 0));
    return limits;
}

// Reparte `length` fotogramas alrededor del corte (centrado) sin pasar de los límites.
std::pair<std::int64_t, std::int64_t> centered(std::int64_t length, TransitionLimits limits) {
    std::int64_t before = std::min(length / 2, limits.maxBefore);
    std::int64_t after = std::min(length - before, limits.maxAfter);
    before = std::min(length - after, limits.maxBefore); // lo que no cupo detrás, delante
    return {before, after};
}

constexpr std::int64_t kMinTransition = 2;

// Tras una edición: las transiciones siguen al clip saliente si se partió, y se quitan
// las que ya no están en un corte (o no caben); se limita su duración a lo que cabe.
void fixTransitions(Project& project, const Splits& splits) {
    for (Track& track : project.tracks) {
        if (track.kind != TrackKind::Video) {
            track.transitions.clear();
            continue;
        }
        for (Transition& t : track.transitions) {
            // Si el clip saliente se partió, el corte queda al final de su parte derecha.
            for (int guard = 0; guard < 64; ++guard) {
                const auto it = splits.find(t.from);
                if (it == splits.end()) {
                    break;
                }
                t.from = it->second.right;
            }
        }
        std::erase_if(track.transitions, [&track](const Transition& t) {
            const Clip* a = clipInTrack(track, t.from);
            const Clip* b = clipInTrack(track, t.to);
            return !a || !b || a->end() != b->start;
        });
        std::sort(track.transitions.begin(), track.transitions.end(),
                  [&track](const Transition& x, const Transition& y) {
                      return clipInTrack(track, x.to)->start < clipInTrack(track, y.to)->start;
                  });
    }
    for (Track& track : project.tracks) {
        for (std::size_t i = 0; i < track.transitions.size();) {
            Transition& t = track.transitions[i];
            const TransitionLimits limits = limitsIn(project, t);
            t.before = std::min(t.before, limits.maxBefore);
            t.after = std::min(t.after, limits.maxAfter);
            t.params.durationFrames = t.length();
            if (t.sound != 0 && !project.clip(t.sound)) {
                t.sound = 0;
            }
            if (t.length() < kMinTransition) {
                track.transitions.erase(track.transitions.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }
    }
}

// Tras una edición: enlaza las partes derechas de los pares que se partieron juntos y
// deshace los enlaces que ya no son válidos (y pone al día las transiciones).
void fixLinks(Project& project, const Splits& splits) {
    for (const auto& [original, info] : splits) {
        if (Clip* right = findMutable(project, info.right)) {
            right->link = 0; // heredado del original: no vale para la parte derecha
        }
    }
    for (const auto& [original, info] : splits) {
        const auto partner = splits.find(info.link);
        if (info.link == 0 || partner == splits.end()) {
            continue;
        }
        Clip* a = findMutable(project, info.right);
        Clip* b = findMutable(project, partner->second.right);
        if (a && b) {
            a->link = b->id;
            b->link = a->id;
        }
    }

    struct Entry {
        Clip* clip;
        TrackKind kind;
    };
    std::map<ObjectId, Entry> all;
    for (Track& track : project.tracks) {
        for (Clip& clip : track.clips) {
            all[clip.id] = {&clip, track.kind};
        }
    }
    std::vector<ObjectId> broken;
    for (const auto& [id, entry] : all) {
        const ObjectId link = entry.clip->link;
        if (link == 0) {
            continue;
        }
        const auto partner = all.find(link);
        const bool valid = partner != all.end() && partner->second.kind != entry.kind &&
                           partner->second.clip->link == id &&
                           sameRange(*partner->second.clip, *entry.clip);
        if (!valid) {
            broken.push_back(id);
        }
    }
    for (ObjectId id : broken) {
        all[id].clip->link = 0;
    }
    fixTransitions(project, splits);
}

Transition* findTransitionMutable(Project& project, ObjectId id) {
    const auto ref = project.findTransition(id);
    return ref ? &project.tracks[ref->track].transitions[ref->index] : nullptr;
}

bool transitionLocked(const Project& project, ObjectId id) {
    const auto ref = project.findTransition(id);
    return !ref || project.tracks[ref->track].locked;
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
    fixLinks(project, {});
    return true;
}

// --- Pistas -----------------------------------------------------------------------

std::size_t addTrack(Project& project, TrackKind kind) {
    if (kind == TrackKind::Audio) {
        project.tracks.push_back(Track{TrackKind::Audio, {}});
        return project.tracks.size() - 1;
    }
    const std::size_t index = project.trackCount(TrackKind::Video);
    project.tracks.insert(project.tracks.begin() + static_cast<std::ptrdiff_t>(index),
                          Track{TrackKind::Video, {}});
    return index;
}

bool removeTrack(Project& project, std::size_t track) {
    if (track >= project.tracks.size() || project.trackCount(project.tracks[track].kind) <= 1) {
        return false;
    }
    project.tracks.erase(project.tracks.begin() + static_cast<std::ptrdiff_t>(track));
    fixLinks(project, {});
    return true;
}

bool setTrackHidden(Project& project, std::size_t track, bool hidden) {
    if (track >= project.tracks.size() || project.tracks[track].kind != TrackKind::Video ||
        project.tracks[track].hidden == hidden) {
        return false;
    }
    project.tracks[track].hidden = hidden;
    return true;
}

bool setTrackMuted(Project& project, std::size_t track, bool muted) {
    if (track >= project.tracks.size() || project.tracks[track].kind != TrackKind::Audio ||
        project.tracks[track].muted == muted) {
        return false;
    }
    project.tracks[track].muted = muted;
    return true;
}

bool setTrackLocked(Project& project, std::size_t track, bool locked) {
    if (track >= project.tracks.size() || project.tracks[track].locked == locked) {
        return false;
    }
    project.tracks[track].locked = locked;
    return true;
}

// --- Clips ------------------------------------------------------------------------

ObjectId placeClip(Project& project, ObjectId media, std::int64_t start, std::int64_t in,
                   std::int64_t length, PlaceTarget target) {
    const MediaItem* item = project.findMedia(media);
    if (!item || start < 0 || in < 0) {
        return 0;
    }
    length = std::min(length, project.mediaFrames(*item) - in);
    if (length <= 0) {
        return 0;
    }
    const bool hasVideo = item->hasVideo;
    const bool hasAudio = item->hasAudio;

    // Pistas de destino (se crean las que falten).
    if (hasVideo) {
        while (project.trackCount(TrackKind::Video) <= target.video) {
            addTrack(project, TrackKind::Video);
        }
    }
    if (hasAudio) {
        while (project.trackCount(TrackKind::Audio) <= target.audio) {
            addTrack(project, TrackKind::Audio);
        }
    }
    std::optional<std::size_t> videoTrack =
        hasVideo ? project.trackIndex(TrackKind::Video, target.video) : std::nullopt;
    std::optional<std::size_t> audioTrack =
        hasAudio ? project.trackIndex(TrackKind::Audio, target.audio) : std::nullopt;
    if (videoTrack && project.tracks[*videoTrack].locked) {
        videoTrack.reset();
    }
    if (audioTrack && project.tracks[*audioTrack].locked) {
        audioTrack.reset();
    }
    if (!videoTrack && !audioTrack) {
        return 0;
    }

    Splits splits;
    const ObjectId videoId = videoTrack ? newId(project) : 0;
    const ObjectId audioId = audioTrack ? newId(project) : 0;
    auto put = [&](std::size_t track, ObjectId id, ObjectId link) {
        clearRange(project, track, start, start + length, splits);
        Clip clip;
        clip.id = id;
        clip.media = media;
        clip.link = link;
        clip.start = start;
        clip.in = in;
        clip.length = length;
        insertSorted(project.tracks[track], clip);
    };
    if (videoTrack) {
        put(*videoTrack, videoId, audioId);
    }
    if (audioTrack) {
        put(*audioTrack, audioId, videoId);
    }
    fixLinks(project, splits);
    return videoId != 0 ? videoId : audioId;
}

ObjectId placeClip(Project& project, ObjectId media, std::int64_t start, PlaceTarget target) {
    const MediaItem* item = project.findMedia(media);
    return item ? placeClip(project, media, start, 0, project.mediaFrames(*item), target) : 0;
}

bool splitAt(Project& project, std::int64_t frame, const std::vector<ObjectId>& clips) {
    const bool all = clips.empty();
    const std::vector<ObjectId> targets =
        all ? std::vector<ObjectId>{} : withLinked(project, clips);
    Splits splits;
    for (Track& track : project.tracks) {
        if (track.locked) {
            continue;
        }
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
            splits[clip.id] = {right.id, clip.link};
            track.clips[i].length = frame - clip.start;
            track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(i) + 1, right);
            ++i;
        }
    }
    if (splits.empty()) {
        return false;
    }
    fixLinks(project, splits);
    return true;
}

bool removeClips(Project& project, const std::vector<ObjectId>& clips) {
    std::vector<ObjectId> group = withLinked(project, clips);
    std::erase_if(group, [&project](ObjectId id) { return inLockedTrack(project, id); });
    if (group.empty()) {
        return false;
    }
    for (Track& track : project.tracks) {
        std::erase_if(track.clips, [&group](const Clip& clip) { return contains(group, clip.id); });
    }
    fixLinks(project, {});
    return true;
}

std::int64_t limitTrimStart(const Project& project, ObjectId clip, std::int64_t start) {
    std::int64_t lo = 0;
    std::int64_t hi = std::numeric_limits<std::int64_t>::max();
    std::int64_t current = start;
    const std::vector<ObjectId> group = withLinked(project, {clip});
    for (ObjectId id : group) {
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
    if (lo > hi || anyLocked(project, group)) {
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
        Clip& c = *findMutable(project, id);
        c.start += delta;
        c.in += delta;
        c.length -= delta;
    }
    fixLinks(project, {});
    return true;
}

std::int64_t limitTrimEnd(const Project& project, ObjectId clip, std::int64_t end) {
    std::int64_t lo = 0;
    std::int64_t hi = std::numeric_limits<std::int64_t>::max();
    std::int64_t current = end;
    const std::vector<ObjectId> group = withLinked(project, {clip});
    for (ObjectId id : group) {
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
    if (lo > hi || anyLocked(project, group)) {
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
        findMutable(project, id)->length += delta;
    }
    fixLinks(project, {});
    return true;
}

std::int64_t limitMove(const Project& project, const std::vector<ObjectId>& clips,
                       std::int64_t delta) {
    const std::vector<ObjectId> group = withLinked(project, clips);
    if (group.empty() || anyLocked(project, group)) {
        return 0;
    }
    std::int64_t first = std::numeric_limits<std::int64_t>::max();
    for (ObjectId id : group) {
        first = std::min(first, project.clip(id)->start);
    }
    return std::max(delta, -first);
}

int limitTrackShift(const Project& project, const std::vector<ObjectId>& clips, TrackKind shiftKind,
                    int trackShift) {
    const std::vector<ObjectId> group = withLinked(project, clips);
    if (group.empty() || trackShift == 0 || anyLocked(project, group)) {
        return 0;
    }
    const auto count = static_cast<std::int64_t>(project.trackCount(shiftKind));
    std::int64_t lo = std::numeric_limits<std::int64_t>::min();
    std::int64_t hi = std::numeric_limits<std::int64_t>::max();
    for (ObjectId id : group) {
        const auto ref = project.findClip(id);
        if (project.tracks[ref->track].kind != shiftKind) {
            continue;
        }
        const auto number = static_cast<std::int64_t>(project.trackNumber(ref->track));
        lo = std::max(lo, -number);
        hi = std::min(hi, count - 1 - number);
    }
    if (lo > hi) {
        return 0;
    }
    const auto shift = static_cast<int>(std::clamp<std::int64_t>(trackShift, lo, hi));
    for (ObjectId id : group) {
        const auto ref = project.findClip(id);
        if (project.tracks[ref->track].kind != shiftKind) {
            continue;
        }
        const auto destination = project.trackIndex(
            shiftKind, static_cast<std::size_t>(
                           static_cast<std::int64_t>(project.trackNumber(ref->track)) + shift));
        if (!destination || project.tracks[*destination].locked) {
            return 0;
        }
    }
    return shift;
}

bool moveClips(Project& project, const std::vector<ObjectId>& clips, std::int64_t delta,
               int trackShift, TrackKind shiftKind) {
    const std::vector<ObjectId> group = withLinked(project, clips);
    delta = limitMove(project, group, delta);
    trackShift = limitTrackShift(project, group, shiftKind, trackShift);
    if (group.empty() || (delta == 0 && trackShift == 0)) {
        return false;
    }
    // Se sacan los clips que se mueven, se despeja su destino y se vuelven a poner.
    struct Moving {
        std::size_t track;
        Clip clip;
    };
    std::vector<Moving> moving;
    for (std::size_t t = 0; t < project.tracks.size(); ++t) {
        auto& trackClips = project.tracks[t].clips;
        if (std::none_of(trackClips.begin(), trackClips.end(),
                         [&group](const Clip& clip) { return contains(group, clip.id); })) {
            continue; // en esta pista no se mueve nada (y quizá no tiene pista de destino)
        }
        // limitTrackShift() ya comprobó que la pista de destino existe.
        std::size_t destination = t;
        if (trackShift != 0 && project.tracks[t].kind == shiftKind) {
            const auto number = static_cast<int>(project.trackNumber(t)) + trackShift;
            destination = project.trackIndex(shiftKind, static_cast<std::size_t>(number)).value();
        }
        for (const Clip& clip : trackClips) {
            if (contains(group, clip.id)) {
                Clip moved = clip;
                moved.start += delta;
                moving.push_back({destination, moved});
            }
        }
        std::erase_if(trackClips, [&group](const Clip& clip) { return contains(group, clip.id); });
    }
    Splits splits;
    for (const Moving& m : moving) {
        clearRange(project, m.track, m.clip.start, m.clip.end(), splits);
    }
    for (const Moving& m : moving) {
        insertSorted(project.tracks[m.track], m.clip);
    }
    fixLinks(project, splits);
    return true;
}

ClipTransform clampTransform(ClipTransform t) {
    auto finite = [](double value, double fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    constexpr double kMaxOffset = 1'000'000.0;
    t.x = std::clamp(finite(t.x, 0.0), -kMaxOffset, kMaxOffset);
    t.y = std::clamp(finite(t.y, 0.0), -kMaxOffset, kMaxOffset);
    t.scale = std::clamp(finite(t.scale, 1.0), kMinScale, kMaxScale);
    t.opacity = std::clamp(finite(t.opacity, 1.0), 0.0, 1.0);
    t.cropLeft = std::clamp(finite(t.cropLeft, 0.0), 0.0, kMaxCrop);
    t.cropTop = std::clamp(finite(t.cropTop, 0.0), 0.0, kMaxCrop);
    t.cropRight = std::clamp(finite(t.cropRight, 0.0), 0.0, kMaxCrop - t.cropLeft);
    t.cropBottom = std::clamp(finite(t.cropBottom, 0.0), 0.0, kMaxCrop - t.cropTop);
    ChromaKey& key = t.chromaKey;
    key.color &= 0xFFFFFFu;
    key.similarity = std::clamp(finite(key.similarity, 0.40), 0.0, kMaxChromaDistance);
    key.smoothness = std::clamp(finite(key.smoothness, 0.10), 0.0, kMaxChromaDistance);
    key.spill = std::clamp(finite(key.spill, 0.40), 0.0, kMaxChromaDistance);
    return t;
}

bool setClipTransform(Project& project, ObjectId clip, const ClipTransform& transform) {
    const auto ref = project.findClip(clip);
    if (!ref || project.tracks[ref->track].kind != TrackKind::Video ||
        project.tracks[ref->track].locked) {
        return false;
    }
    Clip& c = project.tracks[ref->track].clips[ref->index];
    const ClipTransform clamped = clampTransform(transform);
    if (c.transform == clamped) {
        return false;
    }
    c.transform = clamped;
    return true;
}

bool setClipVolume(Project& project, ObjectId clip, double volumeDb) {
    const auto ref = project.findClip(clip);
    if (!ref || project.tracks[ref->track].kind != TrackKind::Audio ||
        project.tracks[ref->track].locked || !std::isfinite(volumeDb)) {
        return false;
    }
    Clip& c = project.tracks[ref->track].clips[ref->index];
    const double clamped = std::clamp(volumeDb, kMinVolumeDb, kMaxVolumeDb);
    if (c.volumeDb == clamped) {
        return false;
    }
    c.volumeDb = clamped;
    return true;
}

bool unlinkClips(Project& project, const std::vector<ObjectId>& clips) {
    bool changed = false;
    for (ObjectId id : clips) {
        Clip* clip = findMutable(project, id);
        if (!clip || clip->link == 0 || inLockedTrack(project, id) ||
            inLockedTrack(project, clip->link)) {
            continue;
        }
        if (Clip* partner = findMutable(project, clip->link)) {
            partner->link = 0;
        }
        clip->link = 0;
        changed = true;
    }
    return changed;
}

// --- Transiciones -------------------------------------------------------------------

TransitionLimits transitionLimits(const Project& project, ObjectId transition) {
    const Transition* t = project.transition(transition);
    return t ? limitsIn(project, *t) : TransitionLimits{};
}

ObjectId addTransition(Project& project, std::size_t track, std::int64_t cut,
                       const TransitionParams& params) {
    if (track >= project.tracks.size() || project.tracks[track].kind != TrackKind::Video ||
        project.tracks[track].locked) {
        return 0;
    }
    Track& target = project.tracks[track];
    const Clip* a = nullptr;
    const Clip* b = nullptr;
    for (const Clip& clip : target.clips) {
        if (clip.end() == cut) {
            a = &clip;
        }
        if (clip.start == cut) {
            b = &clip;
        }
    }
    if (!a || !b) {
        return 0;
    }
    for (Transition& existing : target.transitions) {
        if (existing.to == b->id) {
            TransitionParams changed = params;
            changed.durationFrames = existing.length();
            existing.params = changed;
            const auto [before, after] = centered(std::max(params.durationFrames, kMinTransition),
                                                  limitsIn(project, existing));
            existing.before = before;
            existing.after = after;
            existing.params.durationFrames = existing.length();
            return existing.id;
        }
    }
    Transition t;
    t.id = newId(project);
    t.from = a->id;
    t.to = b->id;
    t.params = params;
    const auto [before, after] =
        centered(std::max(params.durationFrames, kMinTransition), limitsIn(project, t));
    if (before + after < kMinTransition) {
        return 0;
    }
    t.before = before;
    t.after = after;
    t.params.durationFrames = t.length();
    target.transitions.push_back(t);
    fixTransitions(project, {});
    return project.transition(t.id) ? t.id : 0;
}

bool removeTransition(Project& project, ObjectId transition, bool withSound) {
    const auto ref = project.findTransition(transition);
    if (!ref || project.tracks[ref->track].locked) {
        return false;
    }
    Track& track = project.tracks[ref->track];
    const ObjectId sound = track.transitions[ref->index].sound;
    track.transitions.erase(track.transitions.begin() + static_cast<std::ptrdiff_t>(ref->index));
    if (withSound && sound != 0) {
        removeClips(project, {sound}); // si su pista está bloqueada, se queda
    }
    return true;
}

bool setTransitionParams(Project& project, ObjectId transition, const TransitionParams& params) {
    Transition* t = findTransitionMutable(project, transition);
    if (!t || transitionLocked(project, transition)) {
        return false;
    }
    TransitionParams changed = params;
    changed.durationFrames = t->length();
    changed.intensity = std::clamp(changed.intensity, 0.0, 1.0);
    changed.blur = std::clamp(changed.blur, 0.0, 1.0);
    if (t->params == changed) {
        return false;
    }
    t->params = changed;
    return true;
}

bool setTransitionLength(Project& project, ObjectId transition, std::int64_t before,
                         std::int64_t after) {
    Transition* t = findTransitionMutable(project, transition);
    if (!t || transitionLocked(project, transition)) {
        return false;
    }
    const TransitionLimits limits = limitsIn(project, *t);
    before = std::clamp<std::int64_t>(before, 0, limits.maxBefore);
    after = std::clamp<std::int64_t>(after, 0, limits.maxAfter);
    if (before + after < kMinTransition || (before == t->before && after == t->after)) {
        return false;
    }
    t->before = before;
    t->after = after;
    t->params.durationFrames = t->length();
    return true;
}

ObjectId placeTransitionSound(Project& project, ObjectId transition, ObjectId soundMedia,
                              std::int64_t hitFrames, double volumeDb) {
    const Transition* t = project.transition(transition);
    const MediaItem* item = project.findMedia(soundMedia);
    if (!t || !item || !item->hasAudio || transitionLocked(project, transition)) {
        return 0;
    }
    const std::int64_t cut = project.cutOf(*t);
    const std::int64_t length = project.mediaFrames(*item);
    if (cut < 0 || length <= 0) {
        return 0;
    }
    // El golpe del sonido cae en el corte (si empezaría antes del 0, se recorta).
    const std::int64_t start = std::max<std::int64_t>(0, cut - hitFrames);
    const std::int64_t in = start - (cut - hitFrames);
    if (in >= length) {
        return 0;
    }
    const ObjectId previous = t->sound;
    if (previous != 0) {
        removeClips(project, {previous});
    }
    // Primera pista de audio sin nada en ese tramo; si no hay, una nueva debajo.
    const std::int64_t end = start + (length - in);
    std::size_t number = 0;
    for (;; ++number) {
        const auto index = project.trackIndex(TrackKind::Audio, number);
        if (!index) {
            break; // placeClip la crea
        }
        const Track& track = project.tracks[*index];
        const bool free = !track.locked &&
                          std::none_of(track.clips.begin(), track.clips.end(), [&](const Clip& c) {
                              return c.start < end && c.end() > start;
                          });
        if (free) {
            break;
        }
    }
    const ObjectId clip =
        placeClip(project, soundMedia, start, in, length - in, {.video = 0, .audio = number});
    if (clip == 0) {
        return 0;
    }
    setClipVolume(project, clip, volumeDb);
    if (Transition* mutableT = findTransitionMutable(project, transition)) {
        mutableT->sound = clip;
    }
    return clip;
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

void repairLinks(Project& project) {
    fixLinks(project, {});
}

void linkMatchingClips(Project& project) {
    for (Track& track : project.tracks) {
        for (Clip& clip : track.clips) {
            clip.link = 0;
        }
    }
    const auto videoIndex = project.trackIndex(TrackKind::Video, 0);
    const auto audioIndex = project.trackIndex(TrackKind::Audio, 0);
    if (!videoIndex || !audioIndex) {
        return;
    }
    auto& video = project.tracks[*videoIndex].clips;
    auto& audio = project.tracks[*audioIndex].clips;
    std::size_t j = 0;
    for (Clip& v : video) {
        while (j < audio.size() && audio[j].start < v.start) {
            ++j;
        }
        if (j < audio.size() && sameRange(audio[j], v)) {
            v.link = audio[j].id;
            audio[j].link = v.id;
        }
    }
}

// --- Edición con rizo -------------------------------------------------------------

std::int64_t previousEdit(const Project& project, std::int64_t frame) {
    std::int64_t best = 0;
    for (const Track& track : project.tracks) {
        if (track.locked) {
            continue;
        }
        for (const Clip& clip : track.clips) {
            for (const std::int64_t edge : {clip.start, clip.end()}) {
                if (edge < frame) {
                    best = std::max(best, edge);
                }
            }
        }
    }
    return best;
}

std::optional<std::int64_t> nextEdit(const Project& project, std::int64_t frame) {
    std::optional<std::int64_t> best;
    for (const Track& track : project.tracks) {
        if (track.locked) {
            continue;
        }
        for (const Clip& clip : track.clips) {
            for (const std::int64_t edge : {clip.start, clip.end()}) {
                if (edge > frame && (!best || edge < *best)) {
                    best = edge;
                }
            }
        }
    }
    return best;
}

namespace {

// ¿Está vacío [from, to) en todas las pistas no bloqueadas?
bool rangeFree(const Project& project, std::int64_t from, std::int64_t to) {
    for (const Track& track : project.tracks) {
        if (track.locked) {
            continue;
        }
        for (const Clip& clip : track.clips) {
            if (clip.start < to && clip.end() > from) {
                return false;
            }
        }
    }
    return true;
}

void sortMarkers(Project& project) {
    std::stable_sort(project.markers.begin(), project.markers.end(),
                     [](const Marker& a, const Marker& b) { return a.frame < b.frame; });
}

} // namespace

bool extractRange(Project& project, std::int64_t from, std::int64_t to) {
    from = std::max<std::int64_t>(0, from);
    if (to <= from) {
        return false;
    }
    const std::int64_t length = to - from;
    bool changed = false;
    Splits splits;
    for (std::size_t t = 0; t < project.tracks.size(); ++t) {
        if (project.tracks[t].locked) {
            continue;
        }
        const std::vector<Clip> before = project.tracks[t].clips;
        clearRange(project, t, from, to, splits);
        for (Clip& clip : project.tracks[t].clips) {
            if (clip.start >= to) {
                clip.start -= length;
            }
        }
        changed = changed || project.tracks[t].clips != before;
    }
    for (Marker& marker : project.markers) {
        if (marker.frame >= to) {
            marker.frame -= length;
            changed = true;
        } else if (marker.frame > from) {
            marker.frame = from; // estaba en lo que se quitó: queda en el corte
            changed = true;
        }
    }
    if (!changed) {
        return false;
    }
    sortMarkers(project);
    fixLinks(project, splits);
    return true;
}

bool rippleTrimPrevious(Project& project, std::int64_t frame) {
    if (frame <= 0) {
        return false;
    }
    return extractRange(project, previousEdit(project, frame), frame);
}

bool rippleTrimNext(Project& project, std::int64_t frame) {
    const std::optional<std::int64_t> next = nextEdit(project, frame);
    return next && extractRange(project, frame, *next);
}

RippleResult rippleDelete(Project& project, const std::vector<ObjectId>& clips) {
    std::vector<ObjectId> group = withLinked(project, clips);
    std::erase_if(group, [&project](ObjectId id) { return inLockedTrack(project, id); });
    if (group.empty()) {
        return RippleResult::Nothing;
    }
    // Tramos que ocupaban los clips quitados, unidos si se tocan o se solapan.
    std::vector<Gap> ranges;
    for (ObjectId id : group) {
        const Clip* clip = project.clip(id);
        ranges.push_back({clip->start, clip->end()});
    }
    std::sort(ranges.begin(), ranges.end(),
              [](const Gap& a, const Gap& b) { return a.start < b.start; });
    std::vector<Gap> merged;
    for (const Gap& r : ranges) {
        if (!merged.empty() && r.start <= merged.back().end) {
            merged.back().end = std::max(merged.back().end, r.end);
        } else {
            merged.push_back(r);
        }
    }
    removeClips(project, group);
    // De derecha a izquierda: cerrar un hueco no mueve los anteriores.
    bool blocked = false;
    for (auto it = merged.rbegin(); it != merged.rend(); ++it) {
        if (rangeFree(project, it->start, it->end)) {
            extractRange(project, it->start, it->end);
        } else {
            blocked = true;
        }
    }
    return blocked ? RippleResult::Blocked : RippleResult::Closed;
}

std::optional<Gap> gapAt(const Project& project, std::size_t track, std::int64_t frame) {
    if (track >= project.tracks.size() || frame < 0) {
        return std::nullopt;
    }
    std::int64_t start = 0;
    for (const Clip& clip : project.tracks[track].clips) {
        if (clip.start <= frame && frame < clip.end()) {
            return std::nullopt; // hay un clip
        }
        if (clip.end() <= frame) {
            start = std::max(start, clip.end());
        } else if (clip.start > frame) {
            return Gap{start, clip.start};
        }
    }
    return std::nullopt; // después no hay nada: no es un hueco
}

RippleResult closeGap(Project& project, std::size_t track, std::int64_t frame) {
    const std::optional<Gap> gap = gapAt(project, track, frame);
    if (!gap || project.tracks[track].locked) {
        return RippleResult::Nothing;
    }
    if (!rangeFree(project, gap->start, gap->end)) {
        return RippleResult::Blocked;
    }
    return extractRange(project, gap->start, gap->end) ? RippleResult::Closed
                                                       : RippleResult::Nothing;
}

// --- Marcadores --------------------------------------------------------------------

ObjectId addMarker(Project& project, std::int64_t frame, std::string name) {
    if (frame < 0 || std::any_of(project.markers.begin(), project.markers.end(),
                                 [frame](const Marker& m) { return m.frame == frame; })) {
        return 0;
    }
    Marker marker;
    marker.id = newId(project);
    marker.frame = frame;
    marker.name = std::move(name);
    project.markers.push_back(std::move(marker));
    const ObjectId id = project.markers.back().id;
    sortMarkers(project);
    return id;
}

bool removeMarker(Project& project, ObjectId marker) {
    return std::erase_if(project.markers, [marker](const Marker& m) { return m.id == marker; }) > 0;
}

bool renameMarker(Project& project, ObjectId marker, std::string name) {
    for (Marker& m : project.markers) {
        if (m.id == marker && m.name != name) {
            m.name = std::move(name);
            return true;
        }
    }
    return false;
}

bool moveMarker(Project& project, ObjectId marker, std::int64_t frame) {
    frame = std::max<std::int64_t>(0, frame);
    for (Marker& m : project.markers) {
        if (m.id == marker && m.frame != frame) {
            m.frame = frame;
            sortMarkers(project);
            return true;
        }
    }
    return false;
}

} // namespace sharkcut
