#include "core/clip_preset.h"

#include "core/edit.h"

#include <algorithm>

namespace sharkcut {

namespace {

bool between(AnimProperty p, AnimProperty first, AnimProperty last) {
    return static_cast<int>(p) >= static_cast<int>(first) &&
           static_cast<int>(p) <= static_cast<int>(last);
}

} // namespace

bool ClipPreset::animated() const {
    return std::any_of(keys.begin(), keys.end(), [](const auto& k) { return !k.empty(); });
}

bool presetIncludes(const PresetParts& parts, AnimProperty p) {
    switch (p) {
    case AnimProperty::X:
    case AnimProperty::Y:
    case AnimProperty::Scale:
    case AnimProperty::Rotation:
        return parts.placement;
    case AnimProperty::Opacity:
        return parts.opacity;
    default:
        break;
    }
    if (between(p, AnimProperty::CropLeft, AnimProperty::CropBottom)) {
        return parts.crop;
    }
    if (between(p, AnimProperty::Grayscale, AnimProperty::Tint)) {
        return parts.effects;
    }
    return false;
}

ClipPreset presetFromClip(const Clip& clip, std::int64_t frame, FrameRate rate,
                          const PresetParts& parts, PresetAnchor anchor, std::string name) {
    ClipPreset preset;
    preset.name = std::move(name);
    preset.parts = parts;
    preset.anchor = anchor;
    preset.transform = transformAt(clip, frame); // lo que se ve en `frame`
    if (!parts.animation) {
        return preset;
    }
    const std::int64_t anchorFrame = anchor == PresetAnchor::Start ? clip.start : clip.end();
    for (std::size_t i = 0; i < kAnimPropertyCount; ++i) {
        if (!presetIncludes(parts, static_cast<AnimProperty>(i))) {
            continue;
        }
        for (const Keyframe& k : clip.animation.keys[i]) {
            const std::int64_t timeline = clip.start + (k.frame - clip.in);
            preset.keys[i].push_back(
                {rescale(timeline - anchorFrame, rate.frameDuration(), kMicroseconds), k.value,
                 k.easing});
        }
    }
    return preset;
}

bool applyPreset(Project& project, ObjectId id, const ClipPreset& preset) {
    const auto ref = project.findClip(id);
    if (!ref || project.tracks[ref->track].kind != TrackKind::Video ||
        project.tracks[ref->track].locked) {
        return false;
    }
    Clip& clip = project.tracks[ref->track].clips[ref->index];
    const FrameRate rate = project.settings.frameRate;
    ClipTransform t = clip.transform;
    ClipAnimation animation = clip.animation;
    if (preset.parts.effects) {
        t.effects = preset.transform.effects; // qué efectos lleva (y sus valores)
    }
    if (preset.parts.chroma) {
        t.chromaKey = preset.transform.chromaKey;
    }
    const std::int64_t anchorFrame = preset.anchor == PresetAnchor::Start ? clip.start : clip.end();
    for (std::size_t i = 0; i < kAnimPropertyCount; ++i) {
        const auto p = static_cast<AnimProperty>(i);
        if (!presetIncludes(preset.parts, p)) {
            continue;
        }
        transformField(t, p) = transformField(preset.transform, p);
        auto& keys = animation.of(p);
        keys.clear();
        if (!preset.parts.animation) {
            continue;
        }
        for (const PresetKeyframe& k : preset.keys[i]) {
            const std::int64_t frame =
                anchorFrame + rescale(k.offsetUs, kMicroseconds, rate.frameDuration());
            keys.push_back({clip.in + (frame - clip.start), k.value, k.easing});
        }
        std::stable_sort(keys.begin(), keys.end(),
                         [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
        keys.erase(
            std::unique(keys.begin(), keys.end(),
                        [](const Keyframe& a, const Keyframe& b) { return a.frame == b.frame; }),
            keys.end());
    }
    t = clampTransform(t);
    if (t == clip.transform && animation == clip.animation) {
        return false;
    }
    clip.transform = t;
    clip.animation = std::move(animation);
    return true;
}

} // namespace sharkcut
