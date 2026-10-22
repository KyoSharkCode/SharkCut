#include "core/project.h"

#include "core/edit.h" // clampTransform

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
    for (const CaptionTrack& track : captionTracks) {
        if (!track.captions.empty()) {
            result = std::max(result, track.captions.back().end());
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
    if (!item || item->still) {
        return 0;
    }
    const std::int64_t index = microsecondsToFrame(mediaTimeUs(clip, frame), item->frameRate);
    const std::int64_t last =
        std::max<std::int64_t>(0, microsecondsToFrame(item->durationUs, item->frameRate) - 1);
    return std::clamp<std::int64_t>(index, 0, last);
}

std::optional<Project::TransitionRef> Project::findTransition(ObjectId id) const {
    if (id == 0) {
        return std::nullopt;
    }
    for (std::size_t t = 0; t < tracks.size(); ++t) {
        const auto& list = tracks[t].transitions;
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i].id == id) {
                return TransitionRef{t, i};
            }
        }
    }
    return std::nullopt;
}

const Transition* Project::transition(ObjectId id) const {
    const auto ref = findTransition(id);
    return ref ? &tracks[ref->track].transitions[ref->index] : nullptr;
}

std::int64_t Project::cutOf(const Transition& t) const {
    const Clip* b = clip(t.to);
    return b ? b->start : -1;
}

const Transition* Project::transitionAt(std::size_t track, std::int64_t frame) const {
    if (track >= tracks.size()) {
        return nullptr;
    }
    for (const Transition& t : tracks[track].transitions) {
        const std::int64_t cut = cutOf(t);
        if (cut >= 0 && frame >= cut - t.before && frame < cut + t.after) {
            return &t;
        }
    }
    return nullptr;
}

const Transition* Project::transitionInto(ObjectId clipId) const {
    for (const Track& track : tracks) {
        for (const Transition& t : track.transitions) {
            if (t.to == clipId) {
                return &t;
            }
        }
    }
    return nullptr;
}

const Transition* Project::transitionOutOf(ObjectId clipId) const {
    for (const Track& track : tracks) {
        for (const Transition& t : track.transitions) {
            if (t.from == clipId) {
                return &t;
            }
        }
    }
    return nullptr;
}

std::vector<Project::Layer> Project::videoLayersAt(std::int64_t frame) const {
    std::vector<Layer> layers;
    for (std::size_t t = 0; t < tracks.size(); ++t) {
        if (tracks[t].kind != TrackKind::Video || tracks[t].hidden) {
            continue;
        }
        if (const Transition* tr = transitionAt(t, frame)) {
            const Clip* a = clip(tr->from);
            const Clip* b = clip(tr->to);
            const MediaItem* itemA = a ? findMedia(a->media) : nullptr;
            const MediaItem* itemB = b ? findMedia(b->media) : nullptr;
            if (itemA && itemB) {
                Layer layer;
                layer.track = t;
                layer.clip = a;
                layer.media = itemA;
                layer.mediaFrame = mediaFrameIndex(*a, frame);
                layer.transition = tr;
                layer.clipB = b;
                layer.mediaB = itemB;
                layer.mediaFrameB = mediaFrameIndex(*b, frame);
                layer.transitionFrame = frame - (b->start - tr->before);
                layers.push_back(layer);
                continue;
            }
        }
        const Clip* c = clipAt(t, frame);
        if (c && c->isEffect()) {
            Layer layer;
            layer.track = t;
            layer.clip = c;
            layer.effectFrame = c->mediaFrameAt(frame);
            layers.push_back(layer);
            continue;
        }
        const MediaItem* item = c ? findMedia(c->media) : nullptr;
        if (item) {
            Layer layer;
            layer.track = t;
            layer.clip = c;
            layer.media = item;
            layer.mediaFrame = mediaFrameIndex(*c, frame);
            layers.push_back(layer);
        }
    }
    return layers;
}

// --- Subtítulos -------------------------------------------------------------------

std::optional<Project::CaptionRef> Project::findCaption(ObjectId id) const {
    if (id == 0) {
        return std::nullopt;
    }
    for (std::size_t t = 0; t < captionTracks.size(); ++t) {
        const auto& list = captionTracks[t].captions;
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i].id == id) {
                return CaptionRef{t, i};
            }
        }
    }
    return std::nullopt;
}

const Caption* Project::caption(ObjectId id) const {
    const auto ref = findCaption(id);
    return ref ? &captionTracks[ref->track].captions[ref->index] : nullptr;
}

const Caption* Project::captionAt(std::size_t track, std::int64_t frame) const {
    if (track >= captionTracks.size()) {
        return nullptr;
    }
    const auto& list = captionTracks[track].captions;
    // El primero que termina después de `frame` (están ordenados y sin solaparse).
    const auto it = std::upper_bound(list.begin(), list.end(), frame,
                                     [](std::int64_t f, const Caption& c) { return f < c.end(); });
    return it != list.end() && it->start <= frame ? &*it : nullptr;
}

const CaptionStyle& Project::styleOf(std::size_t track, const Caption& caption) const {
    return caption.style ? *caption.style : captionTracks[track].style;
}

// --- Animación ---------------------------------------------------------------------

std::optional<AnimProperty> animPropertyFromId(std::string_view id) {
    for (std::size_t i = 0; i < kAnimPropertyCount; ++i) {
        if (kAnimPropertyIds[i] == id) {
            return static_cast<AnimProperty>(i);
        }
    }
    return std::nullopt;
}

double& transformField(ClipTransform& t, AnimProperty p) {
    switch (p) {
    case AnimProperty::X:
        return t.x;
    case AnimProperty::Y:
        return t.y;
    case AnimProperty::Scale:
        return t.scale;
    case AnimProperty::Opacity:
        return t.opacity;
    case AnimProperty::CropLeft:
        return t.cropLeft;
    case AnimProperty::CropTop:
        return t.cropTop;
    case AnimProperty::CropRight:
        return t.cropRight;
    case AnimProperty::CropBottom:
        return t.cropBottom;
    case AnimProperty::Rotation:
        return t.rotation;
    case AnimProperty::Grayscale:
        return t.effects.grayscale;
    case AnimProperty::Blur:
        return t.effects.blur;
    case AnimProperty::Corners:
        return t.effects.corners;
    case AnimProperty::ShadowOpacity:
        return t.effects.shadowOpacity;
    case AnimProperty::ShadowSoftness:
        return t.effects.shadowSoftness;
    case AnimProperty::ShadowSpread:
        return t.effects.shadowSpread;
    case AnimProperty::ShadowX:
        return t.effects.shadowX;
    case AnimProperty::ShadowY:
        return t.effects.shadowY;
    }
    return t.x;
}

double transformField(const ClipTransform& t, AnimProperty p) {
    return transformField(const_cast<ClipTransform&>(t), p);
}

bool ClipAnimation::empty() const {
    return std::all_of(keys.begin(), keys.end(), [](const auto& k) { return k.empty(); });
}

double keyframeValue(const std::vector<Keyframe>& keys, double mediaFrame) {
    if (keys.empty()) {
        return 0.0;
    }
    if (mediaFrame <= static_cast<double>(keys.front().frame)) {
        return keys.front().value;
    }
    if (mediaFrame >= static_cast<double>(keys.back().frame)) {
        return keys.back().value;
    }
    // El último keyframe en o antes de `mediaFrame` (están ordenados).
    const auto next =
        std::upper_bound(keys.begin(), keys.end(), mediaFrame, [](double f, const Keyframe& k) {
            return f < static_cast<double>(k.frame);
        });
    const Keyframe& a = *(next - 1);
    const Keyframe& b = *next;
    const double t =
        (mediaFrame - static_cast<double>(a.frame)) / static_cast<double>(b.frame - a.frame);
    return a.value + (b.value - a.value) * applyEasing(a.easing, t);
}

ClipTransform transformAt(const Clip& clip, std::int64_t frame) {
    if (clip.animation.empty()) {
        return clip.transform;
    }
    ClipTransform t = clip.transform;
    const auto mediaFrame = static_cast<double>(clip.mediaFrameAt(frame));
    for (std::size_t i = 0; i < kAnimPropertyCount; ++i) {
        const auto p = static_cast<AnimProperty>(i);
        if (clip.animation.animated(p)) {
            transformField(t, p) = keyframeValue(clip.animation.of(p), mediaFrame);
        }
    }
    return clampTransform(t);
}

} // namespace sharkcut
