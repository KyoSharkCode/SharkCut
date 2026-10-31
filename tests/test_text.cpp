#include "core/document.h"
#include "core/edit.h"
#include "core/project_file.h"

#include <catch2/catch_test_macros.hpp>

using namespace sharkcut;

namespace {

TextClip hola() {
    TextClip t;
    t.text = "Hola\na todos";
    t.style.size = 90;
    t.style.color = 0xFFD800;
    t.style.align = -1;
    return t;
}

const Clip& onlyClip(const Project& p, std::size_t track) {
    REQUIRE(p.tracks[track].clips.size() == 1);
    return p.tracks[track].clips.front();
}

} // namespace

TEST_CASE("Text clips go to a free video track and stretch like images", "[text]") {
    Project p;
    p.tracks = defaultTracks(); // V1, V2, A1, A2
    MediaItem video;
    video.path = "/medios/a.mp4";
    video.durationUs = 10'000'000;
    video.hasVideo = true;
    const ObjectId media = addMedia(p, video);
    REQUIRE(placeClip(p, media, 0, {0, 0}) != 0);

    // V1 está ocupada en ese tramo: va a V2.
    const ObjectId text = placeText(p, 0, 60, 120, hola());
    REQUIRE(text != 0);
    const auto ref = p.findClip(text);
    REQUIRE(ref);
    CHECK(ref->track == 1);
    const Clip& c = onlyClip(p, 1);
    CHECK(c.isText());
    CHECK_FALSE(c.hasMedia());
    CHECK(c.media == 0);
    CHECK(c.text->text == "Hola\na todos");

    // Se alarga sin límite, también hacia la izquierda.
    CHECK(trimEnd(p, text, 60 + 100'000));
    CHECK(p.clip(text)->end() == 60 + 100'000);
    CHECK(trimStart(p, text, 0));
    CHECK(p.clip(text)->start == 0);

    // Se ve como una capa sin medio.
    const auto layers = p.videoLayersAt(10);
    REQUIRE(layers.size() == 2);
    CHECK(layers[1].clip->id == text);
    CHECK(layers[1].media == nullptr);

    // Cambiar el texto o el estilo (con los límites del estilo).
    TextClip other = hola();
    other.text = "Adiós";
    other.style.size = 1e6;
    CHECK(setClipText(p, text, other));
    CHECK(p.clip(text)->text->text == "Adiós");
    CHECK(p.clip(text)->text->style.size == 400.0);
    other.style.align = 7; // fuera de rango: se ajusta
    REQUIRE(setClipText(p, text, other));
    CHECK(p.clip(text)->text->style.align == 1);
    CHECK_FALSE(setClipText(p, text, *p.clip(text)->text));           // igual: nada que cambiar
    CHECK_FALSE(setClipText(p, p.tracks[0].clips.front().id, other)); // no es un texto
}

TEST_CASE("Text clips are split, copied and saved with their text", "[text]") {
    Project p;
    p.tracks = defaultTracks();
    const ObjectId text = placeText(p, 0, 0, 200, hola());
    REQUIRE(text != 0);
    ClipTransform moved;
    moved.y = -300;
    REQUIRE(setClipTransform(p, text, moved));

    REQUIRE(splitAt(p, 100));
    REQUIRE(p.tracks[0].clips.size() == 2);
    CHECK(p.tracks[0].clips[1].text == p.tracks[0].clips[0].text);

    const Clipboard copied = copyClips(p, {text});
    const auto pasted = pasteClips(p, copied, 500);
    REQUIRE(pasted.size() == 1);
    CHECK(p.clip(pasted.front())->text->text == "Hola\na todos");
    CHECK(p.clip(pasted.front())->media == 0);

    const Project loaded = projectFromJson(projectToJson(p, {}), {});
    CHECK(loaded.tracks == p.tracks);
    CHECK(loaded.clip(text)->text->style.color == 0xFFD800u);
    CHECK(loaded.clip(text)->text->style.align == -1);
    CHECK(loaded.clip(text)->transform.y == -300);
}

TEST_CASE("Transforms scale their canvas pixels for another canvas size", "[project]") {
    ClipTransform t;
    t.x = 100;
    t.y = -40;
    t.scale = 0.5;
    t.rotation = 30;
    t.effects.blur = 20;
    t.effects.shadowY = 12;
    const ClipTransform half = scaledTransform(t, 0.5);
    CHECK(half.x == 50);
    CHECK(half.y == -20);
    CHECK(half.scale == 0.5); // relativa al lienzo: no cambia
    CHECK(half.rotation == 30);
    CHECK(half.effects.blur == 10);
    CHECK(half.effects.shadowY == 6);
}
