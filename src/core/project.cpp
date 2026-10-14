#include "core/project.h"

#include <algorithm>

namespace sharkcut {

std::int64_t Project::length() const {
    std::int64_t result = 0;
    for (const Track& track : tracks) {
        if (!track.clips.empty()) {
            result = std::max(result, track.clips.back().end());
        }
    }
    return result;
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

} // namespace sharkcut
