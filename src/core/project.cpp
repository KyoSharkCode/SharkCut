#include "core/project.h"

#include <algorithm>
#include <cmath>

namespace sharkcut {

double gainFromDb(double db) {
    if (!(db > kMinVolumeDb)) {
        return 0.0;
    }
    return std::pow(10.0, std::min(db, kMaxVolumeDb) / 20.0);
}

std::int64_t Project::length() const {
    std::int64_t result = 0;
    for (const Track& track : tracks) {
        if (!track.clips.empty()) {
            result = std::max(result, track.clips.back().end());
        }
    }
    return result;
}

std::size_t Project::trackCount(TrackKind kind) const {
    return static_cast<std::size_t>(std::count_if(
        tracks.begin(), tracks.end(), [kind](const Track& t) { return t.kind == kind; }));
}

std::optional<std::size_t> Project::trackIndex(TrackKind kind, std::size_t number) const {
    for (std::size_t t = 0; t < tracks.size(); ++t) {
        if (tracks[t].kind == kind) {
            if (number == 0) {
                return t;
            }
            --number;
        }
    }
    return std::nullopt;
}

std::size_t Project::trackNumber(std::size_t track) const {
    std::size_t number = 0;
    for (std::size_t t = 0; t < track && t < tracks.size(); ++t) {
        if (tracks[t].kind == tracks[track].kind) {
            ++number;
        }
    }
    return number;
}

std::string Project::trackName(std::size_t track) const {
    if (track >= tracks.size()) {
        return {};
    }
    return (tracks[track].kind == TrackKind::Video ? "V" : "A") +
           std::to_string(trackNumber(track) + 1);
}

bool Project::hasAudibleAudio() const {
    return std::any_of(tracks.begin(), tracks.end(), [](const Track& t) {
        return t.kind == TrackKind::Audio && !t.muted && !t.clips.empty();
    });
}

const MediaItem* Project::findMedia(ObjectId id) const {
    for (const MediaItem& item : media) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

std::optional<Project::ClipRef> Project::findClip(ObjectId id) const {
    if (id == 0) {
        return std::nullopt;
    }
    for (std::size_t t = 0; t < tracks.size(); ++t) {
        const auto& clips = tracks[t].clips;
        for (std::size_t i = 0; i < clips.size(); ++i) {
            if (clips[i].id == id) {
                return ClipRef{t, i};
            }
        }
    }
    return std::nullopt;
}

const Clip* Project::clip(ObjectId id) const {
    const auto ref = findClip(id);
    return ref ? &tracks[ref->track].clips[ref->index] : nullptr;
}

const Clip* Project::clipAt(std::size_t track, std::int64_t frame) const {
    if (track >= tracks.size()) {
        return nullptr;
    }
    const auto& clips = tracks[track].clips;
    // Primer clip que termina después de `frame`.
    const auto it = std::upper_bound(clips.begin(), clips.end(), frame,
                                     [](std::int64_t f, const Clip& c) { return f < c.end(); });
    if (it != clips.end() && it->start <= frame) {
        return &*it;
    }
    return nullptr;
}

std::int64_t Project::mediaFrames(const MediaItem& item) const {
    const FrameRate rate = settings.frameRate;
    return mulDiv(item.durationUs, rate.num, rate.den * 1'000'000, Rounding::Nearest);
}

std::int64_t Project::mediaTimeUs(const Clip& clip, std::int64_t frame) const {
    return frameToMicroseconds(clip.in + (frame - clip.start), settings.frameRate);
}

std::int64_t Project::mediaFrameIndex(const Clip& clip, std::int64_t frame) const {
    const MediaItem* item = findMedia(clip.media);
    return item ? microsecondsToFrame(mediaTimeUs(clip, frame), item->frameRate) : 0;
}

std::vector<Project::Layer> Project::videoLayersAt(std::int64_t frame) const {
    std::vector<Layer> layers;
    for (std::size_t t = 0; t < tracks.size(); ++t) {
        if (tracks[t].kind != TrackKind::Video || tracks[t].hidden) {
            continue;
        }
        const Clip* c = clipAt(t, frame);
        const MediaItem* item = c ? findMedia(c->media) : nullptr;
        if (item) {
            layers.push_back({t, c, item, mediaFrameIndex(*c, frame)});
        }
    }
    return layers;
}

} // namespace sharkcut
