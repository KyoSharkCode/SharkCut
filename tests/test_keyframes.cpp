#include "core/edit.h"
#include "core/project_file.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sharkcut;
using Catch::Approx;

namespace {

// Un clip de video de 10 s (600 fotogramas a 60 fps) que empieza en 0.
struct OneClip {
    Project p;
    ObjectId clip = 0;
    OneClip() {
        MediaItem item;
        item.path = "/medios/a.mp4";
        item.durationUs = 20'000'000;
        item.hasVideo = true;
        item.width = 1920;
        item.height = 1080;
        const ObjectId media = addMedia(p, item);
        placeClip(p, media, 0, 0, 600);
        clip = p.tracks[kVideoTrack].clips.front().id;
    }
    const Clip& c() const { return *p.clip(clip); }
};

} // namespace

TEST_CASE("Keyframes interpolate with their easing", "[keyframes]") {
    const std::vector<Keyframe> keys{
        {0, 0.0, Easing::Linear}, {100, 10.0, Easing::Hold}, {200, 20.0, Easing::Linear}};
    CHECK(keyframeValue(keys, -50) == 0.0);
    CHECK(keyframeValue(keys, 50) == Approx(5.0));
    CHECK(keyframeValue(keys, 150) == Approx(10.0)); // mantener: salta al llegar
    CHECK(keyframeValue(keys, 200) == Approx(20.0));
    CHECK(keyframeValue(keys, 999) == 20.0);
    const std::vector<Keyframe> eased{{0, 0.0, Easing::EaseInCubic}, {100, 1.0, Easing::Linear}};
    CHECK(keyframeValue(eased, 50) == Approx(0.125));
}

TEST_CASE("Changing an animated property adds a keyframe", "[keyframes]") {
    OneClip t;
    // Sin animar: el valor fijo cambia.
    ClipTransform moved = t.c().transform;
    moved.x = 100;
    REQUIRE(setClipTransformAt(t.p, t.clip, moved, 0));
    CHECK(t.c().transform.x == 100);
    CHECK(t.c().animation.empty());

    // Animar la escala: keyframe en 0 con su valor; cambiarla en 120 añade otro.
    REQUIRE(setPropertyAnimated(t.p, t.clip, AnimProperty::Scale, true, 0));
    ClipTransform zoom = transformAt(t.c(), 120);
    zoom.scale = 2.0;
    zoom.x = 300; // la x no está animada: cambia su valor fijo
    REQUIRE(setClipTransformAt(t.p, t.clip, zoom, 120));
    REQUIRE(t.c().animation.of(AnimProperty::Scale).size() == 2);
    CHECK(transformAt(t.c(), 60).scale == Approx(1.5));
    CHECK(transformAt(t.c(), 500).scale == Approx(2.0));
    CHECK(transformAt(t.c(), 500).x == 300);

    // Dejar de animar: se queda con el valor que tenía en ese momento.
    REQUIRE(setPropertyAnimated(t.p, t.clip, AnimProperty::Scale, false, 60));
    CHECK(t.c().animation.empty());
    CHECK(t.c().transform.scale == Approx(1.5));
}

TEST_CASE("Removing the last keyframe keeps its value", "[keyframes]") {
    OneClip t;
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::Opacity, 30, 0.25));
    CHECK(t.c().animation.animated(AnimProperty::Opacity));
    CHECK(removeKeyframe(t.p, t.clip, AnimProperty::Opacity, 30));
    CHECK_FALSE(t.c().animation.animated(AnimProperty::Opacity));
    CHECK(t.c().transform.opacity == Approx(0.25));
    CHECK_FALSE(removeKeyframe(t.p, t.clip, AnimProperty::Opacity, 30));
}

TEST_CASE("Keyframes stay with the content when trimming or splitting", "[keyframes]") {
    OneClip t;
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::X, 100, 0.0));
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::X, 300, 200.0));
    const double at200 = transformAt(t.c(), 200).x;
    CHECK(at200 == Approx(100.0));

    // Dividir en 200: las dos mitades siguen igual en cada fotograma.
    REQUIRE(splitAt(t.p, 200));
    const auto& clips = t.p.tracks[kVideoTrack].clips;
    REQUIRE(clips.size() == 2);
    CHECK(transformAt(clips[0], 150).x == Approx(50.0));
    CHECK(transformAt(clips[1], 250).x == Approx(150.0));

    // Recortar el inicio de la segunda: el contenido (y su animación) no se mueve.
    const ObjectId second = clips[1].id;
    REQUIRE(trimStart(t.p, second, 240));
    CHECK(transformAt(*t.p.clip(second), 250).x == Approx(150.0));
}

TEST_CASE("Keyframe easing and moves", "[keyframes]") {
    OneClip t;
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::X, 0, 0.0));
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::Y, 0, 0.0));
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::X, 100, 100.0));
    REQUIRE(setKeyframeEasing(t.p, t.clip, 0, Easing::Hold));
    CHECK(t.c().animation.of(AnimProperty::Y).front().easing == Easing::Hold);
    CHECK(transformAt(t.c(), 50).x == 0.0);

    REQUIRE(moveKeyframes(t.p, t.clip, 100, 200));
    CHECK(t.c().animation.of(AnimProperty::X).back().frame == 200);
    // Un keyframe en medio del tramo hereda la curva del anterior.
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::X, 120, 5.0));
    CHECK(t.c().animation.of(AnimProperty::X)[1].easing == Easing::Hold);

    // Pista bloqueada: nada cambia.
    REQUIRE(setTrackLocked(t.p, kVideoTrack, true));
    CHECK_FALSE(setKeyframe(t.p, t.clip, AnimProperty::X, 300, 1.0));
}

TEST_CASE("Animation survives a project round trip", "[keyframes][project]") {
    OneClip t;
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::Scale, 0, 1.0));
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::Scale, 90, 1.4));
    REQUIRE(setKeyframeEasing(t.p, t.clip, 0, Easing::EaseOutCubic));
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::CropLeft, 10, 0.1));
    const Project loaded = projectFromJson(projectToJson(t.p, {}), {});
    CHECK(loaded.clip(t.clip)->animation == t.c().animation);
}

TEST_CASE("Rotation is animated, saved and kept in range", "[keyframes]") {
    OneClip t;
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::Rotation, 0, 0.0));
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::Rotation, 120, 720.0)); // dos vueltas
    CHECK(transformAt(t.c(), 60).rotation == Approx(360.0));
    ClipTransform still = t.c().transform;
    still.rotation = 1e9;
    CHECK(clampTransform(still).rotation == kMaxRotation);
    REQUIRE(setPropertyAnimated(t.p, t.clip, AnimProperty::Rotation, false, 30));
    CHECK(t.c().transform.rotation == Approx(180.0));
    const Project loaded = projectFromJson(projectToJson(t.p, {}), {});
    CHECK(loaded.clip(t.clip)->transform.rotation == Approx(180.0));
}

TEST_CASE("Effect values can be animated", "[keyframes][efectos]") {
    OneClip t;
    ClipTransform blurred = t.c().transform;
    blurred.effects.blurOn = true;
    REQUIRE(setClipTransform(t.p, t.clip, blurred));
    // El desenfoque entra de 0 a 40 px en un segundo.
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::Blur, 0, 0.0));
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::Blur, 60, 40.0));
    CHECK(transformAt(t.c(), 30).effects.blur == Approx(20.0));
    CHECK(transformAt(t.c(), 30).effects.blurOn);
    // Un cambio en otro momento crea otro keyframe (como con la posición).
    ClipTransform later = transformAt(t.c(), 120);
    later.effects.blur = 10.0;
    REQUIRE(setClipTransformAt(t.p, t.clip, later, 120));
    CHECK(t.c().animation.of(AnimProperty::Blur).size() == 3);
    // Fuera de rango se limita al calcularlo.
    REQUIRE(setKeyframe(t.p, t.clip, AnimProperty::ShadowOpacity, 0, 5.0));
    CHECK(transformAt(t.c(), 0).effects.shadowOpacity == 1.0);
    CHECK(animPropertyFromId("shadowSoftness") == AnimProperty::ShadowSoftness);
    const Project loaded = projectFromJson(projectToJson(t.p, {}), {});
    CHECK(loaded.clip(t.clip)->animation == t.c().animation);
}
