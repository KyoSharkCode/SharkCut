#include "core/edit.h"
#include "core/motion_anchor.h"
#include "media/motion_tracker.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using namespace sharkcut;
using Catch::Approx;

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 360;
constexpr int kSquare = 48;

// Fondo con ruido suave y un cuadro con textura propia (su esquina en `left`, `top`);
// `gain` cambia el brillo de todo (el seguimiento no debe notarlo).
VideoFrame frameWithSquare(double left, double top, double gain, std::uint32_t seed) {
    VideoFrame frame;
    frame.width = kWidth;
    frame.height = kHeight;
    frame.y.resize(static_cast<std::size_t>(kWidth) * kHeight);
    frame.uv.assign(static_cast<std::size_t>(frame.uvWidth()) * 2 * frame.uvHeight(), 128);
    std::mt19937 noise(seed);
    std::uniform_int_distribution<int> grain(-6, 6);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            double value = 90.0 + 30.0 * std::sin(x * 0.01) + grain(noise);
            const double sx = x - left;
            const double sy = y - top;
            if (sx >= 0 && sy >= 0 && sx < kSquare && sy < kSquare) {
                // Textura del cuadro: anillos y rayas, la misma en cada fotograma.
                value = 128.0 + 60.0 * std::sin(sx * 0.45) * std::cos(sy * 0.3) +
                        40.0 * std::sin(std::hypot(sx - 20, sy - 14) * 0.6);
            }
            frame.y[static_cast<std::size_t>(y) * kWidth + x] =
                static_cast<std::uint8_t>(std::clamp(value * gain, 0.0, 255.0));
        }
    }
    return frame;
}

} // namespace

TEST_CASE("Motion tracker follows a moving textured square", "[motion]") {
    double left = 100.0;
    double top = 80.0;
    const VideoFrame first = frameWithSquare(left, top, 1.0, 1);
    MotionTracker tracker(
        first, {left / kWidth, top / kHeight, double(kSquare) / kWidth, double(kSquare) / kHeight});
    CHECK(tracker.start().x * kWidth == Approx(left + kSquare / 2.0));
    for (int i = 1; i <= 60; ++i) {
        // Movimiento irregular, a veces rápido, con cambios de brillo.
        left += 4.0 + 3.0 * std::sin(i * 0.3);
        top += 2.5 * std::cos(i * 0.2);
        const double gain = 1.0 + 0.15 * std::sin(i * 0.5);
        const TrackPoint p = tracker.track(frameWithSquare(left, top, gain, 1 + i));
        INFO("fotograma " << i);
        CHECK(p.score >= kTrackLostScore);
        CHECK(std::abs(p.x * kWidth - (left + kSquare / 2.0)) < 1.5);
        CHECK(std::abs(p.y * kHeight - (top + kSquare / 2.0)) < 1.5);
    }
}

TEST_CASE("Motion tracker reports a lost region", "[motion]") {
    const VideoFrame first = frameWithSquare(200, 150, 1.0, 1);
    MotionTracker tracker(first, {200.0 / kWidth, 150.0 / kHeight, double(kSquare) / kWidth,
                                  double(kSquare) / kHeight});
    // El cuadro desaparece: solo queda el fondo.
    const VideoFrame empty = frameWithSquare(-1000, -1000, 1.0, 2);
    CHECK(tracker.track(empty).score < kTrackLostScore);
    CHECK_THROWS(MotionTracker(first, {0.5, 0.5, 0.001, 0.001}));
}

TEST_CASE("Media points are placed on the canvas like the compositor", "[motion]") {
    ProjectSettings settings; // 1920 × 1080
    ClipTransform t;
    const CanvasPoint center = mediaPointOnCanvas(settings, 1920, 1080, t, 0.5, 0.5);
    CHECK(center.x == Approx(960));
    CHECK(center.y == Approx(540));
    // Video vertical (9:16) en lienzo 16:9: ocupa todo el alto, centrado.
    const CanvasPoint corner = mediaPointOnCanvas(settings, 1080, 1920, t, 0.0, 0.0);
    CHECK(corner.x == Approx(960 - 1080.0 * 1080 / 1920 / 2));
    CHECK(corner.y == Approx(0));
    // Mitad de tamaño, desplazado y girado 90° (horario): el borde derecho queda abajo.
    t.scale = 0.5;
    t.x = 100;
    t.rotation = 90;
    const CanvasPoint right = mediaPointOnCanvas(settings, 1920, 1080, t, 1.0, 0.5);
    CHECK(right.x == Approx(1060).margin(1e-6));
    CHECK(right.y == Approx(540 + 480));
    const CanvasPoint back = canvasPointOnMedia(settings, 1920, 1080, t, right.x, right.y);
    CHECK(back.x == Approx(1.0));
    CHECK(back.y == Approx(0.5));
}

TEST_CASE("Fitting to the canvas takes the rotation into account", "[motion]") {
    ProjectSettings vertical;
    vertical.width = 1080;
    vertical.height = 1920;
    // Un 16:9 girado 90° en un lienzo 9:16: lo ocupa entero al ajustarlo.
    ClipTransform t;
    t.rotation = 90.0;
    const ClipTransform fit = fitToCanvas(vertical, 1920, 1080, t, false);
    CHECK(fit.scale == Approx(1920.0 / 1080.0));
    CHECK(fit.x == Approx(0.0).margin(1e-9));
    CHECK(fit.y == Approx(0.0).margin(1e-9));
    CHECK(fit.rotation == 90.0);
    const CanvasPoint topLeft = mediaPointOnCanvas(vertical, 1920, 1080, fit, 0.0, 0.0);
    CHECK(topLeft.x == Approx(1080.0));
    CHECK(topLeft.y == Approx(0.0).margin(1e-6));
    // Sin girar, como antes: entero con bandas, o cubriéndolo.
    CHECK(fitToCanvas(vertical, 1920, 1080, {}, false).scale == Approx(1.0));
    CHECK(fitToCanvas(vertical, 1920, 1080, {}, true).scale == Approx(1920.0 / 607.5));

    // Con un recorte, lo que queda se centra también después del giro.
    ClipTransform cropped;
    cropped.rotation = 90.0;
    cropped.cropLeft = 0.5; // solo la mitad derecha del video
    const ClipTransform half = fitToCanvas(vertical, 1920, 1080, cropped, true);
    const CanvasPoint middle = mediaPointOnCanvas(vertical, 1920, 1080, half, 0.75, 0.5);
    CHECK(middle.x == Approx(540.0));
    CHECK(middle.y == Approx(960.0));
}

TEST_CASE("Anchoring adds position keyframes that follow the region", "[motion]") {
    Project p;
    MediaItem item;
    item.path = "/medios/a.mp4";
    item.durationUs = 20'000'000;
    item.hasVideo = true;
    item.width = 1920;
    item.height = 1080;
    const ObjectId media = addMedia(p, item);
    placeClip(p, media, 0, 0, 600);
    const ObjectId target = p.tracks[kVideoTrack].clips.front().id;
    addTrack(p, TrackKind::Video);
    PlaceTarget above;
    above.video = 1; // V2
    const ObjectId sticker = placeClip(p, media, 100, 0, 200, above);
    REQUIRE(sticker != 0);
    ClipTransform small = p.clip(sticker)->transform;
    small.scale = 0.2;
    small.x = -300;
    REQUIRE(setClipTransformAt(p, sticker, small, 100));

    // La zona va a la derecha 0.01 del ancho por fotograma de 120 a 170 y después sube
    // en curva hasta 200.
    std::vector<AnchorSample> samples;
    for (std::int64_t f = 120; f <= 200; ++f) {
        const double u = 0.3 + 0.01 * (std::min<std::int64_t>(f, 170) - 120);
        const double v = f <= 170 ? 0.5 : 0.5 - 0.1 * std::sin((f - 170) * 0.1);
        samples.push_back({f, u, v});
    }
    REQUIRE(anchorClip(p, sticker, target, samples));
    const Clip& c = *p.clip(sticker);
    CHECK(transformAt(c, 120).x == Approx(-300));
    CHECK(transformAt(c, 170).x == Approx(-300 + 0.5 * 1920));
    CHECK(transformAt(c, 150).y == Approx(0));
    // Los keyframes que sobran (la recta) se quitan, pero el recorrido es el mismo.
    CHECK(c.animation.of(AnimProperty::X).size() == 3); // 120, 170 (giro) y 200
    CHECK(c.animation.of(AnimProperty::Y).size() < 31);
    for (const AnchorSample& s : samples) {
        const double expected = -1080.0 * (0.5 - s.v);
        CHECK(std::abs(transformAt(c, s.frame).y - expected) <= kAnchorTolerance + 1e-9);
    }
    CHECK(transformAt(c, 110).x == Approx(-300)); // antes del tramo, como estaba
    // Un clip no se ancla a sí mismo.
    CHECK_FALSE(anchorClip(p, sticker, sticker, samples));
}
