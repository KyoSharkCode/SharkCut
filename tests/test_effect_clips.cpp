#include "core/edit.h"
#include "core/history.h"
#include "core/project_file.h"

#include <catch2/catch_test_macros.hpp>

using namespace sharkcut;

namespace {

// V1 con dos clips seguidos (0–300 y 300–600) de un medio de video.
struct TwoCuts {
    Project p;
    TwoCuts() {
        MediaItem item;
        item.path = "/medios/a.mp4";
        item.durationUs = 60'000'000;
        item.hasVideo = true;
        item.width = 1920;
        item.height = 1080;
        const ObjectId media = addMedia(p, item);
        placeClip(p, media, 0, 0, 300);
        placeClip(p, media, 300, 600, 300);
    }
    TransitionParams zoom(std::int64_t frames = 30) const {
        TransitionParams params = transitionInfo(TransitionType::ZoomIn).defaults;
        params.durationFrames = frames;
        return params;
    }
};

// Transición en 100–130 con su sonido (golpe en la mitad) enlazado.
struct WithSound : TwoCuts {
    ObjectId effect = 0;
    ObjectId sound = 0;
    WithSound() {
        MediaItem whoosh;
        whoosh.path = "/sfx/whoosh.wav";
        whoosh.durationUs = 1'000'000; // 60 fotogramas a 60 fps
        whoosh.hasAudio = true;
        const ObjectId media = addMedia(p, whoosh);
        effect = placeEffect(p, kVideoTrack, 100, zoom(30));
        sound = placeSoundHit(p, media, 115, 20, 0.0);
        linkEffectSound(p, effect, sound);
    }
};

const Clip* onlyEffect(const Project& p) {
    for (const Track& t : p.tracks) {
        for (const Clip& c : t.clips) {
            if (c.isEffect()) {
                return &c;
            }
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Effect clips go to a free video track and never cover clips", "[effects]") {
    TwoCuts t;
    const std::size_t videoTracks = t.p.trackCount(TrackKind::Video);
    // Sobre V1 (ocupada): va a la primera pista de encima libre.
    const ObjectId id = placeEffect(t.p, kVideoTrack, 285, t.zoom());
    REQUIRE(id != 0);
    const auto ref = t.p.findClip(id);
    REQUIRE(ref);
    CHECK(ref->track != kVideoTrack);
    CHECK(t.p.tracks[kVideoTrack].clips.size() == 2); // nada tapado
    const Clip& effect = *t.p.clip(id);
    CHECK(effect.start == 285);
    CHECK(effect.length == 30);
    CHECK(effect.media == 0);
    // Otra en el mismo tramo: otra pista más (se crea si hace falta).
    const ObjectId second = placeEffect(t.p, kVideoTrack, 290, t.zoom());
    CHECK(t.p.findClip(second)->track != ref->track);
    CHECK(t.p.trackCount(TrackKind::Video) >= videoTracks);
}

TEST_CASE("Effect clips are layers above what they affect", "[effects]") {
    TwoCuts t;
    const ObjectId id = placeEffect(t.p, kVideoTrack, 285, t.zoom());
    const auto layers = t.p.videoLayersAt(300);
    REQUIRE(layers.size() == 2);
    CHECK(layers[0].media != nullptr); // el video de V1
    CHECK(layers[1].clip->id == id);
    CHECK(layers[1].media == nullptr);
    CHECK(layers[1].effectFrame == 15); // a la mitad
    CHECK(t.p.videoLayersAt(400).size() == 1);
}

TEST_CASE("Splitting or trimming an effect clip keeps its timing", "[effects]") {
    TwoCuts t;
    const ObjectId id = placeEffect(t.p, kVideoTrack, 100, t.zoom(40));
    REQUIRE(splitAt(t.p, 120, {id}));
    const auto layers = t.p.videoLayersAt(125);
    REQUIRE(layers.size() == 2);
    CHECK(layers[1].effectFrame == 25); // sigue donde iba
    // No se alarga más allá de su duración (como un medio).
    CHECK(limitTrimEnd(t.p, id, 500) == 120);
    CHECK(limitTrimStart(t.p, id, 0) == 100);
}

TEST_CASE("Changing an effect clip duration shows the whole effect", "[effects]") {
    TwoCuts t;
    const ObjectId id = placeEffect(t.p, kVideoTrack, 100, t.zoom(30));
    REQUIRE(trimStart(t.p, id, 110));
    TransitionParams longer = t.p.clip(id)->effect.value();
    longer.durationFrames = 60;
    longer.type = TransitionType::Rotate;
    REQUIRE(setClipEffect(t.p, id, longer));
    const Clip& c = *t.p.clip(id);
    CHECK(c.in == 0);
    CHECK(c.length == 60);
    CHECK(c.effect->type == TransitionType::Rotate);
}

TEST_CASE("Effect clips are copied, pasted and saved", "[effects]") {
    TwoCuts t;
    TransitionParams params = t.zoom(24);
    params.type = TransitionType::ShakeDistort;
    params.intensity = 0.3;
    const ObjectId id = placeEffect(t.p, kVideoTrack, 50, params);
    const Clipboard copied = copyClips(t.p, {id});
    const auto pasted = pasteClips(t.p, copied, 400);
    REQUIRE(pasted.size() == 1);
    CHECK(t.p.clip(pasted.front())->effect == params);

    const Project back = projectFromJson(projectToJson(t.p, "/p/a.sharkp"), "/p/a.sharkp");
    std::size_t effects = 0;
    for (const Track& track : back.tracks) {
        for (const Clip& c : track.clips) {
            if (c.isEffect()) {
                ++effects;
                CHECK(c.effect == params);
                CHECK(c.media == 0);
            }
        }
    }
    CHECK(effects == 2);
    CHECK(onlyEffect(back) != nullptr);
}

TEST_CASE("An effect clip and its sound are linked", "[effects]") {
    WithSound t;
    REQUIRE(t.effect != 0);
    REQUIRE(t.sound != 0);
    CHECK(t.p.clip(t.effect)->link == t.sound);
    CHECK(t.p.clip(t.sound)->link == t.effect);
    CHECK(t.p.clip(t.sound)->start == 95);
    // Solo una transición y un clip de audio: dos clips de video no se enlazan así.
    const ObjectId first = t.p.tracks[kVideoTrack].clips.front().id;
    CHECK_FALSE(linkEffectSound(t.p, t.effect, first));
    // Las ediciones posteriores conservan el enlace (no comparten tramo).
    repairLinks(t.p);
    CHECK(t.p.clip(t.effect)->link == t.sound);
}

TEST_CASE("Moving or removing an effect clip takes its sound along", "[effects]") {
    WithSound t;
    REQUIRE(moveClips(t.p, {t.effect}, 50));
    CHECK(t.p.clip(t.effect)->start == 150);
    CHECK(t.p.clip(t.sound)->start == 145);
    // También al arrastrar el sonido.
    REQUIRE(moveClips(t.p, {t.sound}, -10));
    CHECK(t.p.clip(t.effect)->start == 140);
    CHECK(t.p.clip(t.sound)->start == 135);

    const Clipboard copied = copyClips(t.p, {t.effect});
    CHECK(copied.clips.size() == 2);
    const auto pasted = pasteClips(t.p, copied, 400);
    REQUIRE(pasted.size() == 2);
    CHECK(t.p.clip(pasted[0])->link == pasted[1]);

    REQUIRE(removeClips(t.p, {t.effect}));
    CHECK(t.p.clip(t.effect) == nullptr);
    CHECK(t.p.clip(t.sound) == nullptr);
}

TEST_CASE("Trimming an effect clip keeps its sound length", "[effects]") {
    WithSound t;
    const std::int64_t soundLength = t.p.clip(t.sound)->length;
    REQUIRE(trimEnd(t.p, t.effect, 120));
    CHECK(t.p.clip(t.effect)->length == 20);
    CHECK(t.p.clip(t.sound)->length == soundLength);
    REQUIRE(trimStart(t.p, t.effect, 105));
    CHECK(t.p.clip(t.sound)->start == 95);
    CHECK(t.p.clip(t.effect)->link == t.sound);
}

TEST_CASE("An effect clip and its sound can be unlinked", "[effects]") {
    History history;
    history.apply("Preparar", [](Project& p) {
        p = WithSound().p;
        return true;
    });
    const WithSound ids;
    REQUIRE(history.apply("Desvincular", [&](Project& p) { return unlinkClips(p, {ids.effect}); }));
    REQUIRE(history.apply("Mover", [&](Project& p) { return moveClips(p, {ids.effect}, 50); }));
    CHECK(history.project().clip(ids.sound)->start == 95); // ya no la sigue
    REQUIRE(history.undo());
    REQUIRE(history.undo());
    CHECK(history.project().clip(ids.effect)->link == ids.sound);
}

TEST_CASE("The link between an effect clip and its sound is saved", "[effects]") {
    WithSound t;
    const Project back = projectFromJson(projectToJson(t.p, "/p/a.sharkp"), "/p/a.sharkp");
    REQUIRE(back.clip(t.effect) != nullptr);
    CHECK(back.clip(t.effect)->link == t.sound);
    CHECK(back.clip(t.sound)->link == t.effect);
}

TEST_CASE("An effect clip can be centred exactly on the playhead", "[effects]") {
    TwoCuts t;
    const TransitionParams params = t.zoom(31);
    // El cabezal a mitad de un clip, lejos del corte: su punto medio queda ahí.
    const std::int64_t playhead = 140;
    const std::int64_t start = effectStartCenteredAt(playhead, params.durationFrames);
    CHECK(start + params.durationFrames / 2 == playhead);
    const ObjectId id = placeEffect(t.p, kVideoTrack, start, params);
    REQUIRE(id != 0);
    CHECK(t.p.clip(id)->start == 125);
    // Con el sonido: su golpe en el mismo fotograma.
    MediaItem whoosh;
    whoosh.path = "/sfx/whoosh.wav";
    whoosh.durationUs = 1'000'000;
    whoosh.hasAudio = true;
    const ObjectId media = addMedia(t.p, whoosh);
    const ObjectId sound = placeSoundHit(t.p, media, playhead, 20, 0.0);
    REQUIRE(sound != 0);
    CHECK(t.p.clip(sound)->start + 20 == playhead);
    // Al principio de la secuencia no empieza antes del 0.
    CHECK(effectStartCenteredAt(5, 30) == 0);
}
