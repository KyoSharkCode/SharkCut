#include "core/clip_preset.h"
#include "core/edit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sharkcut;
using Catch::Approx;

namespace {

// Dos clips de video a 60 fps: uno de 10 s (0–600) y otro de 4 s (700–940), este con el
// medio empezando en el fotograma 100.
struct TwoClips {
    Project p;
    ObjectId first = 0;
    ObjectId second = 0;
    TwoClips() {
        MediaItem item;
        item.path = "/medios/a.mp4";
        item.durationUs = 60'000'000;
        item.hasVideo = true;
        item.width = 1920;
        item.height = 1080;
        const ObjectId media = addMedia(p, item);
        placeClip(p, media, 0, 0, 600);
        placeClip(p, media, 700, 100, 240);
        first = p.tracks[kVideoTrack].clips[0].id;
        second = p.tracks[kVideoTrack].clips[1].id;
    }
    const Clip& c(ObjectId id) const { return *p.clip(id); }
};

// Anima la opacidad del clip de 0 a 1 en su primer segundo (desde `start`, en la línea).
void fadeIn(Project& p, ObjectId id, std::int64_t start) {
    REQUIRE(setPropertyAnimated(p, id, AnimProperty::Opacity, true, start));
    REQUIRE(setKeyframe(p, id, AnimProperty::Opacity, start, 0.0));
    REQUIRE(setKeyframe(p, id, AnimProperty::Opacity, start + 60, 1.0));
}

} // namespace

TEST_CASE("A preset anchored at the start keeps its timing on another clip", "[presets]") {
    TwoClips t;
    fadeIn(t.p, t.first, 0);
    ClipTransform look = t.c(t.first).transform;
    look.scale = 1.5;
    look.effects.blurOn = true;
    look.effects.blur = 30.0;
    REQUIRE(setClipTransformAt(t.p, t.first, look, 200));

    const ClipPreset preset = presetFromClip(t.c(t.first), 200, t.p.settings.frameRate, {},
                                             PresetAnchor::Start, "Entrada");
    CHECK(preset.animated());
    REQUIRE(preset.keys[static_cast<int>(AnimProperty::Opacity)].size() == 2);
    CHECK(preset.keys[static_cast<int>(AnimProperty::Opacity)][1].offsetUs == 1'000'000);

    REQUIRE(applyPreset(t.p, t.second, preset));
    const Clip& b = t.c(t.second);
    CHECK(b.transform.scale == Approx(1.5));
    CHECK(b.transform.effects.blurOn);
    CHECK(b.transform.effects.blur == Approx(30.0));
    CHECK(transformAt(b, 700).opacity == Approx(0.0));
    CHECK(transformAt(b, 730).opacity == Approx(0.5));
    CHECK(transformAt(b, 760).opacity == Approx(1.0));
    // Los keyframes van en la posición del medio del clip nuevo.
    CHECK(b.animation.of(AnimProperty::Opacity).front().frame == 100);
    // Aplicarlo otra vez no cambia nada.
    CHECK_FALSE(applyPreset(t.p, t.second, preset));
}

TEST_CASE("A preset anchored at the end follows the end of a shorter clip", "[presets]") {
    TwoClips t;
    // Fundido de salida en el último segundo del primer clip (540–600).
    REQUIRE(setPropertyAnimated(t.p, t.first, AnimProperty::Opacity, true, 540)); // 1 en 540
    REQUIRE(setKeyframe(t.p, t.first, AnimProperty::Opacity, 600, 0.0));

    const ClipPreset preset =
        presetFromClip(t.c(t.first), 0, t.p.settings.frameRate, {}, PresetAnchor::End, "Salida");
    CHECK(preset.keys[static_cast<int>(AnimProperty::Opacity)][0].offsetUs == -1'000'000);
    REQUIRE(applyPreset(t.p, t.second, preset));
    const Clip& b = t.c(t.second);
    CHECK(transformAt(b, 800).opacity == Approx(1.0));
    CHECK(transformAt(b, 910).opacity == Approx(0.5)); // el clip acaba en 940
    CHECK(transformAt(b, 940).opacity == Approx(0.0));
}

TEST_CASE("A preset only changes the parts it includes", "[presets]") {
    TwoClips t;
    ClipTransform look = t.c(t.first).transform;
    look.x = 250;
    look.opacity = 0.4;
    look.cropLeft = 0.1;
    look.effects.grayscaleOn = true;
    look.chromaKey.enabled = true;
    REQUIRE(setClipTransformAt(t.p, t.first, look, 0));
    fadeIn(t.p, t.first, 0);

    ClipTransform other = t.c(t.second).transform;
    other.x = -80;
    other.cropLeft = 0.3;
    REQUIRE(setClipTransformAt(t.p, t.second, other, 700));

    PresetParts parts;
    parts.placement = false;
    parts.crop = false;
    parts.animation = false;
    const ClipPreset preset =
        presetFromClip(t.c(t.first), 120, t.p.settings.frameRate, parts, PresetAnchor::Start, "B");
    CHECK_FALSE(preset.animated());
    REQUIRE(applyPreset(t.p, t.second, preset));
    const Clip& b = t.c(t.second);
    CHECK(b.transform.x == Approx(-80));        // colocación: no va en el ajuste
    CHECK(b.transform.cropLeft == Approx(0.3)); // recorte: tampoco
    CHECK(b.transform.opacity == Approx(1.0));  // el valor que se veía en 120, sin animación
    CHECK(b.transform.effects.grayscaleOn);
    CHECK(b.transform.chromaKey.enabled);
    CHECK(b.animation.empty());
}

TEST_CASE("Presets are not applied to locked or missing clips", "[presets]") {
    TwoClips t;
    const ClipPreset preset =
        presetFromClip(t.c(t.first), 0, t.p.settings.frameRate, {}, PresetAnchor::Start, "A");
    t.p.tracks[kVideoTrack].locked = true;
    CHECK_FALSE(applyPreset(t.p, t.second, preset));
    CHECK_FALSE(applyPreset(t.p, 999'999, preset));
}

TEST_CASE("Presets survive a JSON round trip", "[presets]") {
    TwoClips t;
    fadeIn(t.p, t.first, 0);
    ClipTransform look = t.c(t.first).transform;
    look.rotation = 45;
    look.effects.shadowOn = true;
    look.effects.shadowColor = 0x112233;
    REQUIRE(setClipTransformAt(t.p, t.first, look, 0));
    REQUIRE(setKeyframeEasing(t.p, t.first, 0, Easing::EaseOutCubic));

    PresetParts parts;
    parts.chroma = false;
    std::vector<ClipPreset> presets{
        presetFromClip(t.c(t.first), 0, t.p.settings.frameRate, parts, PresetAnchor::End, "Uno"),
        presetFromClip(t.c(t.second), 700, t.p.settings.frameRate, {}, PresetAnchor::Start,
                       "Dos ñ")};
    const std::vector<ClipPreset> back = presetsFromJson(presetsToJson(presets));
    CHECK(back == presets);
    CHECK(presetsFromJson("no es json").empty());
    CHECK(presetsFromJson(R"([{"name": 3}, {"name": "Vale"}])").size() == 1);
}
