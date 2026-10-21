#include "core/motion_anchor.h"

#include "core/edit.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sharkcut {

namespace {

// Tamaño del video ajustado al lienzo sin deformar (como el compositor): ocupa todo el
// ancho o todo el alto.
std::pair<double, double> fitSize(const ProjectSettings& settings, int mediaWidth,
                                  int mediaHeight) {
    const double canvasW = settings.width;
    const double canvasH = settings.height;
    if (mediaWidth <= 0 || mediaHeight <= 0) {
        return {canvasW, canvasH};
    }
    const double aspect = static_cast<double>(mediaWidth) / mediaHeight;
    if (aspect < canvasW / canvasH) {
        return {canvasH * aspect, canvasH};
    }
    return {canvasW, canvasW / aspect};
}

// Quita los keyframes que sobran: los que la línea entre sus vecinos (que se quedan) ya
// pasa a menos de `tolerance` (Ramer–Douglas–Peucker). Así un recorrido de miles de
// fotogramas se queda en los keyframes que hacen falta.
void simplify(std::vector<Keyframe>& keys, double tolerance) {
    if (keys.size() < 3) {
        return;
    }
    std::vector<bool> keep(keys.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<std::size_t, std::size_t>> pending{{0, keys.size() - 1}};
    while (!pending.empty()) {
        const auto [a, b] = pending.back();
        pending.pop_back();
        double worst = 0.0;
        std::size_t worstIndex = a;
        const double span = static_cast<double>(keys[b].frame - keys[a].frame);
        for (std::size_t i = a + 1; i < b; ++i) {
            const double t = static_cast<double>(keys[i].frame - keys[a].frame) / span;
            const double line = keys[a].value + t * (keys[b].value - keys[a].value);
            const double error = std::abs(keys[i].value - line);
            if (error > worst) {
                worst = error;
                worstIndex = i;
            }
        }
        if (worst > tolerance) {
            keep[worstIndex] = true;
            pending.emplace_back(a, worstIndex);
            pending.emplace_back(worstIndex, b);
        }
    }
    std::size_t out = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keep[i]) {
            keys[out++] = keys[i];
        }
    }
    keys.resize(out);
}

} // namespace

CanvasPoint mediaPointOnCanvas(const ProjectSettings& settings, int mediaWidth, int mediaHeight,
                               const ClipTransform& t, double u, double v) {
    const auto [fitW, fitH] = fitSize(settings, mediaWidth, mediaHeight);
    const double localX = (u - 0.5) * fitW * t.scale;
    const double localY = (v - 0.5) * fitH * t.scale;
    // Giro horario (con y hacia abajo) alrededor del centro del video.
    const double angle = t.rotation * std::numbers::pi / 180.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {settings.width / 2.0 + t.x + localX * c - localY * s,
            settings.height / 2.0 + t.y + localX * s + localY * c};
}

CanvasPoint canvasPointOnMedia(const ProjectSettings& settings, int mediaWidth, int mediaHeight,
                               const ClipTransform& t, double x, double y) {
    const auto [fitW, fitH] = fitSize(settings, mediaWidth, mediaHeight);
    const double dx = x - settings.width / 2.0 - t.x;
    const double dy = y - settings.height / 2.0 - t.y;
    const double angle = t.rotation * std::numbers::pi / 180.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double localX = dx * c + dy * s;
    const double localY = -dx * s + dy * c;
    const double scaleX = fitW * t.scale;
    const double scaleY = fitH * t.scale;
    return {scaleX > 0 ? 0.5 + localX / scaleX : 0.5, scaleY > 0 ? 0.5 + localY / scaleY : 0.5};
}

bool anchorClip(Project& project, ObjectId id, ObjectId targetId,
                const std::vector<AnchorSample>& samples) {
    const auto ref = project.findClip(id);
    const Clip* target = project.clip(targetId);
    if (!ref || !target || id == targetId || samples.empty() ||
        project.tracks[ref->track].kind != TrackKind::Video || project.tracks[ref->track].locked) {
        return false;
    }
    const MediaItem* media = project.findMedia(target->media);
    if (!media) {
        return false;
    }
    Clip& clip = project.tracks[ref->track].clips[ref->index];
    std::vector<AnchorSample> inside;
    for (const AnchorSample& s : samples) {
        if (s.frame >= clip.start && s.frame < clip.end()) {
            inside.push_back(s);
        }
    }
    if (inside.empty()) {
        return false;
    }
    std::sort(inside.begin(), inside.end(),
              [](const AnchorSample& a, const AnchorSample& b) { return a.frame < b.frame; });
    const auto pointAt = [&](const AnchorSample& s) {
        return mediaPointOnCanvas(project.settings, media->width, media->height,
                                  transformAt(*target, s.frame), s.u, s.v);
    };
    const CanvasPoint origin = pointAt(inside.front());
    const ClipTransform base = transformAt(clip, inside.front().frame);

    // Fuera los keyframes de posición del tramo; uno por fotograma seguido.
    ClipAnimation animation = clip.animation;
    const std::int64_t from = clip.mediaFrameAt(inside.front().frame);
    const std::int64_t to = clip.mediaFrameAt(inside.back().frame);
    for (AnimProperty p : {AnimProperty::X, AnimProperty::Y}) {
        auto& keys = animation.of(p);
        std::erase_if(keys, [&](const Keyframe& k) { return k.frame >= from && k.frame <= to; });
    }
    std::vector<Keyframe> pathX;
    std::vector<Keyframe> pathY;
    for (const AnchorSample& s : inside) {
        const CanvasPoint point = pointAt(s);
        const std::int64_t frame = clip.mediaFrameAt(s.frame);
        pathX.push_back({frame, base.x + (point.x - origin.x), Easing::Linear});
        pathY.push_back({frame, base.y + (point.y - origin.y), Easing::Linear});
    }
    simplify(pathX, kAnchorTolerance);
    simplify(pathY, kAnchorTolerance);
    auto& keysX = animation.of(AnimProperty::X);
    auto& keysY = animation.of(AnimProperty::Y);
    keysX.insert(keysX.end(), pathX.begin(), pathX.end());
    keysY.insert(keysY.end(), pathY.begin(), pathY.end());
    for (AnimProperty p : {AnimProperty::X, AnimProperty::Y}) {
        auto& keys = animation.of(p);
        std::stable_sort(keys.begin(), keys.end(),
                         [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
        keys.erase(
            std::unique(keys.begin(), keys.end(),
                        [](const Keyframe& a, const Keyframe& b) { return a.frame == b.frame; }),
            keys.end());
    }
    if (animation == clip.animation) {
        return false;
    }
    clip.animation = std::move(animation);
    return true;
}

} // namespace sharkcut
