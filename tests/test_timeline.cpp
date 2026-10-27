#include "core/document.h"
#include "core/edit.h"
#include "core/history.h"
#include "core/project.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sharkcut;

namespace {

MediaItem media(const char* name, std::int64_t seconds, bool video, bool audio,
                FrameRate rate = {60, 1}) {
    MediaItem item;
    item.path = std::string("/videos/") + name;
    item.name = name;
    item.durationUs = seconds * 1'000'000;
    item.frameRate = rate;
    item.width = 1920;
    item.height = 1080;
    item.hasVideo = video;
    item.hasAudio = audio;
    return item;
}

const std::vector<Clip>& video(const Project& p) {
    return p.tracks[kVideoTrack].clips;
}

const std::vector<Clip>& audio(const Project& p) {
    return p.tracks[*p.trackIndex(TrackKind::Audio, 0)].clips;
}

// Comprueba los invariantes: pistas ordenadas, sin solapes y enlaces recíprocos.
void checkInvariants(const Project& p) {
    // Primero las pistas de video y después las de audio.
    bool audioSeen = false;
    for (const Track& track : p.tracks) {
        if (track.kind == TrackKind::Audio) {
            audioSeen = true;
        }
        CHECK((track.kind == TrackKind::Audio || !audioSeen));
    }
    for (const Track& track : p.tracks) {
        for (std::size_t i = 0; i < track.clips.size(); ++i) {
            const Clip& c = track.clips[i];
            CHECK(c.length > 0);
            CHECK(c.start >= 0);
            CHECK(c.in >= 0);
            CHECK(c.id < p.nextId);
            if (i > 0) {
                CHECK(track.clips[i - 1].end() <= c.start);
            }
            if (c.link != 0) {
                const Clip* partner = p.clip(c.link);
                REQUIRE(partner != nullptr);
                CHECK(partner->link == c.id);
                CHECK(partner->start == c.start);
                CHECK(partner->length == c.length);
                CHECK(partner->in == c.in);
                CHECK(partner->media == c.media);
                const auto own = p.findClip(c.id);
                const auto other = p.findClip(partner->id);
                CHECK(p.tracks[own->track].kind != p.tracks[other->track].kind);
            }
        }
    }
}

} // namespace

TEST_CASE("Placing a clip creates linked video and audio clips", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId v = placeClip(p, a, 0);
    REQUIRE(v != 0);
    REQUIRE(video(p).size() == 1);
    REQUIRE(audio(p).size() == 1);
    CHECK(video(p)[0].id == v);
    CHECK(video(p)[0].length == 600);
    CHECK(video(p)[0].link == audio(p)[0].id);
    CHECK(p.length() == 600);
    checkInvariants(p);

    // Solo video o solo audio: una sola pista.
    const ObjectId b = addMedia(p, media("b.mp4", 5, true, false));
    const ObjectId c = addMedia(p, media("c.wav", 5, false, true));
    placeClip(p, b, 600);
    placeClip(p, c, 900);
    CHECK(video(p).size() == 2);
    CHECK(audio(p).size() == 2);
    CHECK(video(p)[1].link == 0);
    CHECK(audio(p)[1].link == 0);
    CHECK(p.length() == 1200);
    checkInvariants(p);
}

TEST_CASE("Placing a clip is limited to the media", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    CHECK(placeClip(p, a, 0, 500, 1000) != 0); // solo quedan 100 fotogramas
    CHECK(video(p)[0].length == 100);
    CHECK(placeClip(p, a, 0, 600, 10) == 0); // fuera del medio
    CHECK(placeClip(p, a, -5) == 0);
    CHECK(placeClip(p, 999, 0) == 0); // medio inexistente
}

TEST_CASE("Placing over clips overwrites them", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId b = addMedia(p, media("b.mp4", 2, true, true));
    placeClip(p, a, 0);
    placeClip(p, b, 200); // 120 fotogramas en medio de A

    REQUIRE(video(p).size() == 3);
    CHECK(video(p)[0].start == 0);
    CHECK(video(p)[0].length == 200);
    CHECK(video(p)[1].media == b);
    CHECK(video(p)[1].start == 200);
    CHECK(video(p)[2].start == 320);
    CHECK(video(p)[2].in == 320); // la parte derecha sigue donde estaba en el medio
    CHECK(video(p)[2].length == 280);
    CHECK(audio(p).size() == 3);
    // Las dos mitades de A siguen enlazadas con su audio.
    CHECK(video(p)[0].link == audio(p)[0].id);
    CHECK(video(p)[2].link == audio(p)[2].id);
    checkInvariants(p);

    // Un medio sin audio solo tapa el video: el audio de A sigue entero y se desenlaza.
    const ObjectId c = addMedia(p, media("c.mp4", 1, true, false));
    placeClip(p, c, 0);
    CHECK(video(p)[0].media == c);
    CHECK(video(p)[1].start == 60);
    CHECK(audio(p)[0].start == 0);
    CHECK(audio(p)[0].length == 200);
    CHECK(video(p)[1].link == 0);
    checkInvariants(p);
}

TEST_CASE("Splitting cuts clips at the playhead", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId first = placeClip(p, a, 0);

    CHECK_FALSE(splitAt(p, 0));   // en el borde no hay nada que partir
    CHECK_FALSE(splitAt(p, 600)); // ni al final
    REQUIRE(splitAt(p, 250));
    REQUIRE(video(p).size() == 2);
    REQUIRE(audio(p).size() == 2);
    CHECK(video(p)[0].id == first); // la parte izquierda conserva su id
    CHECK(video(p)[0].length == 250);
    CHECK(video(p)[1].start == 250);
    CHECK(video(p)[1].in == 250);
    CHECK(video(p)[1].length == 350);
    CHECK(video(p)[1].link == audio(p)[1].id);
    checkInvariants(p);

    // Solo los clips indicados (y su enlazado).
    Project q;
    const ObjectId qa = addMedia(q, media("a.mp4", 10, true, true));
    const ObjectId qb = addMedia(q, media("b.wav", 10, false, true));
    const ObjectId clipA = placeClip(q, qa, 0);
    placeClip(q, qb, 0); // tapa el audio de A
    CHECK(splitAt(q, 100, {clipA}));
    CHECK(video(q).size() == 2);
    CHECK(audio(q).size() == 1);
    checkInvariants(q);
}

TEST_CASE("Trimming the start respects media and neighbours", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    placeClip(p, a, 0, 0, 100);                        // 0–100
    const ObjectId v = placeClip(p, a, 300, 200, 200); // 300–500, medio 200–400

    CHECK(limitTrimStart(p, v, 250) == 250);
    CHECK(limitTrimStart(p, v, 50) == 100);  // no pisa el clip anterior
    CHECK(limitTrimStart(p, v, 499) == 499); // deja un fotograma
    CHECK(limitTrimStart(p, v, 900) == 499);

    REQUIRE(trimStart(p, v, 350));
    const Clip* clip = p.clip(v);
    CHECK(clip->start == 350);
    CHECK(clip->in == 250);
    CHECK(clip->length == 150);
    CHECK(audio(p)[1].start == 350); // el audio enlazado también
    CHECK(audio(p)[1].in == 250);
    checkInvariants(p);

    // Sin clip anterior: como mucho hasta el inicio del medio.
    Project q;
    const ObjectId qa = addMedia(q, media("a.mp4", 10, true, true));
    const ObjectId w = placeClip(q, qa, 500, 100, 50);
    CHECK(limitTrimStart(q, w, 0) == 400);
    CHECK_FALSE(trimStart(q, w, 500)); // sin cambios
}

TEST_CASE("Trimming the end respects media and neighbours", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId v = placeClip(p, a, 0, 400, 100); // 0–100, medio 400–500
    placeClip(p, a, 300, 0, 100);                    // 300–400

    CHECK(limitTrimEnd(p, v, 150) == 150);
    CHECK(limitTrimEnd(p, v, 250) == 200); // el medio acaba en 600
    CHECK(limitTrimEnd(p, v, 0) == 1);

    Project q = p;
    REQUIRE(trimEnd(q, v, 50));
    CHECK(q.clip(v)->length == 50);
    CHECK(audio(q)[0].length == 50);
    checkInvariants(q);

    // Con el siguiente clip más cerca que el final del medio.
    REQUIRE(moveClips(p, {video(p)[1].id}, -150)); // 150–250
    CHECK(limitTrimEnd(p, v, 400) == 150);
}

TEST_CASE("Moving clips overwrites the destination and leaves a gap", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId b = addMedia(p, media("b.mp4", 2, true, true));
    placeClip(p, a, 0);                       // 0–600
    const ObjectId vb = placeClip(p, b, 600); // 600–720

    CHECK(limitMove(p, {vb}, -1000) == -600);
    REQUIRE(moveClips(p, {vb}, -400)); // a 200–320, encima de A
    REQUIRE(video(p).size() == 3);
    CHECK(video(p)[1].id == vb);
    CHECK(video(p)[1].start == 200);
    CHECK(video(p)[2].start == 320);
    CHECK(video(p)[2].end() == 600); // el hueco que dejó B no se rellena
    CHECK(p.length() == 600);
    CHECK(audio(p)[1].start == 200);
    checkInvariants(p);

    CHECK_FALSE(moveClips(p, {vb}, 0));
    CHECK_FALSE(moveClips(p, {12345}, 10));
}

TEST_CASE("Removing a clip removes its linked clip", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId v = placeClip(p, a, 0);
    const ObjectId audioClip = audio(p)[0].id;
    REQUIRE(removeClips(p, {audioClip})); // desde el audio también vale
    CHECK(video(p).empty());
    CHECK(audio(p).empty());
    CHECK_FALSE(removeClips(p, {v}));

    placeClip(p, a, 0);
    REQUIRE(removeMedia(p, a));
    CHECK(p.media.empty());
    CHECK(video(p).empty());
}

TEST_CASE("Clip lookup and media time", "[timeline]") {
    Project p;
    // Medio a 30 fps en un proyecto a 60: 4 s = 240 fotogramas del proyecto.
    const ObjectId a = addMedia(p, media("a.mp4", 4, true, false, {30, 1}));
    CHECK(p.mediaFrames(*p.findMedia(a)) == 240);
    const ObjectId v = placeClip(p, a, 100, 60, 120);
    const Clip* clip = p.clip(v);
    CHECK(p.clipAt(kVideoTrack, 99) == nullptr);
    CHECK(p.clipAt(kVideoTrack, 100) == clip);
    CHECK(p.clipAt(kVideoTrack, 219) == clip);
    CHECK(p.clipAt(kVideoTrack, 220) == nullptr);
    CHECK(p.mediaTimeUs(*clip, 100) == 1'000'000); // in = 60 fotogramas a 60 fps
    CHECK(p.mediaTimeUs(*clip, 130) == 1'500'000);
}

TEST_CASE("History undoes and redoes edits", "[timeline]") {
    History history;
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.modified());

    ObjectId a = 0;
    REQUIRE(history.apply("Importar", [&](Project& p) {
        a = addMedia(p, media("a.mp4", 10, true, true));
        return true;
    }));
    REQUIRE(history.apply("Añadir clip", [&](Project& p) { return placeClip(p, a, 0) != 0; }));
    REQUIRE(history.apply("Dividir", [](Project& p) { return splitAt(p, 300); }));
    CHECK(video(history.project()).size() == 2);
    CHECK(history.modified());
    CHECK(history.undoLabel() == "Dividir");

    // Una edición que no cambia nada no se registra.
    CHECK_FALSE(history.apply("Dividir", [](Project& p) { return splitAt(p, 300); }));
    CHECK_FALSE(history.apply("Nada", [](Project&) { return true; }));

    REQUIRE(history.undo());
    CHECK(video(history.project()).size() == 1);
    CHECK(history.redoLabel() == "Dividir");
    REQUIRE(history.redo());
    CHECK(video(history.project()).size() == 2);

    history.markSaved();
    CHECK_FALSE(history.modified());
    history.undo();
    CHECK(history.modified());
    history.redo();
    CHECK_FALSE(history.modified());

    // Una edición nueva descarta lo que se podía rehacer.
    history.undo();
    REQUIRE(history.apply("Quitar", [](Project& p) { return removeClips(p, {video(p)[0].id}); }));
    CHECK_FALSE(history.canRedo());
    CHECK(history.modified());

    // Guardar en segundo plano: se marca el estado que se escribió, no el actual.
    const auto written = history.snapshot();
    REQUIRE(history.apply("Añadir clip", [&](Project& p) { return placeClip(p, a, 0) != 0; }));
    history.markSaved(written);
    CHECK(history.modified());
    history.undo();
    CHECK_FALSE(history.modified());

    history.reset(Project{});
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.modified());
}

TEST_CASE("History keeps a bounded number of steps", "[timeline]") {
    History history;
    ObjectId a = 0;
    history.apply("Importar", [&](Project& p) {
        a = addMedia(p, media("a.mp4", 100, true, true));
        return true;
    });
    history.apply("Añadir", [&](Project& p) { return placeClip(p, a, 0) != 0; });
    for (int i = 1; i <= static_cast<int>(History::kMaxUndo) + 20; ++i) {
        REQUIRE(history.apply("Dividir", [i](Project& p) { return splitAt(p, i * 10); }));
    }
    int steps = 0;
    while (history.undo()) {
        ++steps;
    }
    CHECK(steps == static_cast<int>(History::kMaxUndo));
}

// --- Varias pistas -------------------------------------------------------------------

namespace {

const std::vector<Clip>& trackClips(const Project& p, TrackKind kind, std::size_t number) {
    return p.tracks[*p.trackIndex(kind, number)].clips;
}

} // namespace

TEST_CASE("Tracks can be added and removed", "[timeline][tracks]") {
    Project p;
    CHECK(p.trackName(0) == "V1");
    CHECK(p.trackName(1) == "A1");
    const std::size_t v2 = addTrack(p, TrackKind::Video);
    const std::size_t a2 = addTrack(p, TrackKind::Audio);
    CHECK(v2 == 1);
    CHECK(a2 == 3);
    CHECK(p.trackName(v2) == "V2");
    CHECK(p.trackName(2) == "A1");
    CHECK(p.trackName(a2) == "A2");
    CHECK(p.trackCount(TrackKind::Video) == 2);
    CHECK(p.trackNumber(a2) == 1);
    checkInvariants(p);

    const ObjectId m = addMedia(p, media("a.mp4", 10, true, true));
    placeClip(p, m, 0, {.video = 1, .audio = 1});
    CHECK(trackClips(p, TrackKind::Video, 1).size() == 1);
    CHECK(trackClips(p, TrackKind::Audio, 1).size() == 1);

    // Quitar V2 se lleva su clip; el audio de A2 queda sin enlace.
    CHECK(removeTrack(p, v2));
    CHECK(p.trackCount(TrackKind::Video) == 1);
    REQUIRE(trackClips(p, TrackKind::Audio, 1).size() == 1);
    CHECK(trackClips(p, TrackKind::Audio, 1)[0].link == 0);
    checkInvariants(p);

    // Siempre queda al menos una pista de cada tipo.
    CHECK_FALSE(removeTrack(p, kVideoTrack));
    CHECK(removeTrack(p, *p.trackIndex(TrackKind::Audio, 0)));
    CHECK_FALSE(removeTrack(p, *p.trackIndex(TrackKind::Audio, 0)));
}

TEST_CASE("Placing on upper tracks creates them if needed", "[timeline][tracks]") {
    Project p;
    const ObjectId cam = addMedia(p, media("cam.mp4", 10, true, false));
    const ObjectId music = addMedia(p, media("music.mp3", 20, false, true));
    const ObjectId gameplay = addMedia(p, media("game.mp4", 30, true, true));
    placeClip(p, gameplay, 0);
    const ObjectId camClip = placeClip(p, cam, 60, {.video = 1, .audio = 0});
    const ObjectId musicClip = placeClip(p, music, 0, {.video = 0, .audio = 1});
    REQUIRE(camClip != 0);
    REQUIRE(musicClip != 0);
    CHECK(p.tracks.size() == 4);
    CHECK(trackClips(p, TrackKind::Video, 1)[0].id == camClip);
    CHECK(trackClips(p, TrackKind::Audio, 1)[0].id == musicClip);
    // Lo de debajo no se toca.
    CHECK(trackClips(p, TrackKind::Video, 0).size() == 1);
    CHECK(trackClips(p, TrackKind::Audio, 0).size() == 1);
    checkInvariants(p);

    // Lo que se ve en un fotograma, de abajo arriba.
    const auto layers = p.videoLayersAt(100);
    REQUIRE(layers.size() == 2);
    CHECK(layers[0].track == kVideoTrack);
    CHECK(layers[1].clip->id == camClip);
    CHECK(layers[1].mediaFrame == 40);
    CHECK(p.videoLayersAt(10).size() == 1);
    setTrackHidden(p, kVideoTrack, true);
    CHECK(p.videoLayersAt(100).size() == 1);
    CHECK(p.videoLayersAt(10).empty());
}

TEST_CASE("Clips move between tracks of their kind", "[timeline][tracks]") {
    Project p;
    addTrack(p, TrackKind::Video);
    const ObjectId m = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId v = placeClip(p, m, 0);
    const ObjectId a = p.clip(v)->link;

    CHECK(limitTrackShift(p, {v}, TrackKind::Video, 5) == 1);
    CHECK(limitTrackShift(p, {v}, TrackKind::Video, -1) == 0);
    REQUIRE(moveClips(p, {v}, 30, 1, TrackKind::Video));
    // El video sube a V2; su audio se mueve en el tiempo pero sigue en A1.
    REQUIRE(trackClips(p, TrackKind::Video, 1).size() == 1);
    CHECK(trackClips(p, TrackKind::Video, 0).empty());
    CHECK(p.clip(v)->start == 30);
    CHECK(trackClips(p, TrackKind::Audio, 0)[0].id == a);
    CHECK(p.clip(a)->start == 30);
    CHECK(p.clip(v)->link == a);
    checkInvariants(p);
}

TEST_CASE("Locked tracks are not edited", "[timeline][tracks]") {
    Project p;
    const ObjectId m = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId v = placeClip(p, m, 0);
    const ObjectId a = p.clip(v)->link;
    const std::size_t a1 = *p.trackIndex(TrackKind::Audio, 0);
    REQUIRE(setTrackLocked(p, a1, true));

    CHECK(limitMove(p, {v}, 50) == 0);
    CHECK_FALSE(moveClips(p, {v}, 50));
    CHECK(limitTrimEnd(p, v, 100) == 600);
    CHECK_FALSE(setClipVolume(p, a, -6));

    // Dividir todo parte solo lo que no está bloqueado (y el enlace se pierde).
    CHECK(splitAt(p, 300));
    CHECK(trackClips(p, TrackKind::Video, 0).size() == 2);
    CHECK(trackClips(p, TrackKind::Audio, 0).size() == 1);
    CHECK(p.clip(v)->link == 0);
    checkInvariants(p);

    // Quitar no toca la pista bloqueada.
    CHECK(removeClips(p, {a}) == false);
    CHECK(p.clip(a) != nullptr);

    // Colocar en la pista bloqueada solo coloca la otra parte.
    const ObjectId placed = placeClip(p, m, 1000);
    REQUIRE(placed != 0);
    CHECK(p.clip(placed)->link == 0);
    CHECK(trackClips(p, TrackKind::Audio, 0).size() == 1);
    checkInvariants(p);
}

TEST_CASE("Unlinked clips are edited on their own", "[timeline][tracks]") {
    Project p;
    const ObjectId m = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId v = placeClip(p, m, 0);
    const ObjectId a = p.clip(v)->link;
    REQUIRE(unlinkClips(p, {v}));
    CHECK(p.clip(v)->link == 0);
    CHECK(p.clip(a)->link == 0);
    CHECK_FALSE(unlinkClips(p, {v}));
    REQUIRE(removeClips(p, {a}));
    CHECK(p.clip(v) != nullptr);
    CHECK(trackClips(p, TrackKind::Audio, 0).empty());
    // No se vuelven a enlazar al editar.
    const ObjectId v2 = placeClip(p, m, 1000);
    const ObjectId a2 = p.clip(v2)->link;
    unlinkClips(p, {a2});
    moveClips(p, {v2}, 10);
    CHECK(p.clip(v2)->start == 1010);
    CHECK(p.clip(a2)->start == 1000);
    checkInvariants(p);
}

TEST_CASE("Splitting keeps links, transform and volume", "[timeline][tracks]") {
    Project p;
    const ObjectId m = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId v = placeClip(p, m, 0);
    const ObjectId a = p.clip(v)->link;
    ClipTransform t;
    t.scale = 2.5;
    t.x = -100;
    REQUIRE(setClipTransform(p, v, t));
    REQUIRE(setClipVolume(p, a, -12));
    REQUIRE(splitAt(p, 200));
    const Clip& right = trackClips(p, TrackKind::Video, 0)[1];
    CHECK(right.transform == t);
    REQUIRE(right.link != 0);
    CHECK(p.clip(right.link)->volumeDb == -12);
    CHECK(p.clip(v)->link == a);
    checkInvariants(p);

    // Sobrescribir solo la pista de video parte el clip y deja el audio sin pareja.
    const ObjectId still = addMedia(p, media("b.mp4", 1, true, false));
    placeClip(p, still, 50);
    checkInvariants(p);
    CHECK(p.clip(v)->link == 0);
}

TEST_CASE("Transform and volume values are limited", "[timeline][tracks]") {
    Project p;
    const ObjectId m = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId v = placeClip(p, m, 0);
    const ObjectId a = p.clip(v)->link;
    ClipTransform t;
    t.scale = 0;
    t.opacity = 7;
    t.cropLeft = 0.8;
    t.cropRight = 0.8;
    REQUIRE(setClipTransform(p, v, t));
    CHECK(p.clip(v)->transform.scale == kMinScale);
    CHECK(p.clip(v)->transform.opacity == 1.0);
    CHECK(p.clip(v)->transform.cropLeft + p.clip(v)->transform.cropRight <= kMaxCrop + 1e-9);
    CHECK_FALSE(setClipTransform(p, a, t)); // el audio no tiene transformación
    CHECK_FALSE(setClipVolume(p, v, -3));   // ni el video volumen
    CHECK(setClipVolume(p, a, -200));
    CHECK(p.clip(a)->volumeDb == kMinVolumeDb);
    CHECK(gainFromDb(kMinVolumeDb) == 0.0);
    CHECK(gainFromDb(0.0) == 1.0);
    CHECK(gainFromDb(-6.0) == Catch::Approx(0.501).margin(0.001));
}

TEST_CASE("Moving to the top or bottom track keeps the other tracks", "[timeline][tracks]") {
    // V1 y V2 (V2 vacía): subir un trozo de un clip partido a V2 y volver a bajarlo.
    Project p;
    addTrack(p, TrackKind::Video);
    addTrack(p, TrackKind::Audio);
    const ObjectId m = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId v = placeClip(p, m, 0);
    REQUIRE(splitAt(p, 300));
    const ObjectId right = trackClips(p, TrackKind::Video, 0)[1].id;
    REQUIRE(moveClips(p, {right}, 0, 1, TrackKind::Video));
    CHECK(trackClips(p, TrackKind::Video, 1).size() == 1);
    CHECK(trackClips(p, TrackKind::Video, 0).size() == 1);
    CHECK(p.clip(v) != nullptr);
    checkInvariants(p);
    REQUIRE(moveClips(p, {right}, 0, -1, TrackKind::Video));
    CHECK(trackClips(p, TrackKind::Video, 1).empty());
    CHECK(trackClips(p, TrackKind::Video, 0).size() == 2);
    checkInvariants(p);

    // Lo mismo con el audio: A1 → A2 → A1.
    const ObjectId audioRight = trackClips(p, TrackKind::Audio, 0)[1].id;
    REQUIRE(moveClips(p, {audioRight}, 0, 1, TrackKind::Audio));
    CHECK(trackClips(p, TrackKind::Audio, 1).size() == 1);
    REQUIRE(moveClips(p, {audioRight}, 0, -1, TrackKind::Audio));
    CHECK(trackClips(p, TrackKind::Audio, 1).empty());
    checkInvariants(p);
}

// --- Transiciones --------------------------------------------------------------------

namespace {

// A: medio 0–300 en 0–300; B: medio 400–600 en 300–500 (corte en 300).
Project projectWithCut(ObjectId& a, ObjectId& b) {
    Project p;
    const ObjectId m = addMedia(p, media("a.mp4", 20, true, true));
    a = placeClip(p, m, 0, 0, 300);
    b = placeClip(p, m, 300, 400, 200);
    return p;
}

TransitionParams withLength(std::int64_t frames) {
    TransitionParams params;
    params.durationFrames = frames;
    return params;
}

} // namespace

TEST_CASE("Transitions sit on a cut and mix both clips", "[timeline][transitions]") {
    ObjectId a = 0;
    ObjectId b = 0;
    Project p = projectWithCut(a, b);
    CHECK(addTransition(p, kVideoTrack, 250, withLength(30)) == 0); // ahí no hay corte
    const ObjectId t = addTransition(p, kVideoTrack, 300, withLength(30));
    REQUIRE(t != 0);
    const Transition* tr = p.transition(t);
    CHECK(tr->from == a);
    CHECK(tr->to == b);
    CHECK(tr->before == 15);
    CHECK(tr->after == 15);
    CHECK(p.cutOf(*tr) == 300);
    CHECK(p.transitionAt(kVideoTrack, 284) == nullptr);
    CHECK(p.transitionAt(kVideoTrack, 285) == tr);
    CHECK(p.transitionAt(kVideoTrack, 314) == tr);
    CHECK(p.transitionAt(kVideoTrack, 315) == nullptr);

    // En el fotograma 290, A sigue normal y B usa material de antes de su entrada.
    auto layers = p.videoLayersAt(290);
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].transition == tr);
    CHECK(layers[0].clip->id == a);
    CHECK(layers[0].mediaFrame == 290);
    CHECK(layers[0].clipB->id == b);
    CHECK(layers[0].mediaFrameB == 390);
    CHECK(layers[0].transitionFrame == 5);
    // Pasado el corte, A sigue con el material de después de su salida.
    layers = p.videoLayersAt(310);
    CHECK(layers[0].mediaFrame == 310);
    CHECK(layers[0].mediaFrameB == 410);
    CHECK(layers[0].transitionFrame == 25);

    // Otra en el mismo corte cambia la que había.
    TransitionParams other = withLength(30);
    other.type = TransitionType::PanLeft;
    CHECK(addTransition(p, kVideoTrack, 300, other) == t);
    CHECK(p.tracks[kVideoTrack].transitions.size() == 1);
    CHECK(p.transition(t)->params.type == TransitionType::PanLeft);
    checkInvariants(p);
}

TEST_CASE("Transitions without spare media repeat the first frame", "[timeline][transitions]") {
    Project p;
    const ObjectId m = addMedia(p, media("a.mp4", 20, true, true));
    placeClip(p, m, 0, 0, 300);
    placeClip(p, m, 300, 0, 300); // B empieza en el primer fotograma del medio
    REQUIRE(addTransition(p, kVideoTrack, 300, withLength(20)) != 0);
    const auto layers = p.videoLayersAt(292);
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].mediaFrameB == 0);
}

TEST_CASE("Transition length is limited by its clips", "[timeline][transitions]") {
    Project p;
    const ObjectId m = addMedia(p, media("a.mp4", 20, true, true));
    placeClip(p, m, 0, 0, 10);
    placeClip(p, m, 10, 100, 6);
    placeClip(p, m, 16, 200, 50);
    const ObjectId t = addTransition(p, kVideoTrack, 10, withLength(30));
    REQUIRE(t != 0);
    CHECK(p.transition(t)->before == 10);
    CHECK(p.transition(t)->after == 6);
    CHECK_FALSE(setTransitionLength(p, t, 100, 100)); // ya está al máximo
    CHECK_FALSE(setTransitionLength(p, t, 1, 0));     // menos de 2 fotogramas
    REQUIRE(setTransitionLength(p, t, 4, 2));
    // La del corte siguiente no puede pisar la anterior dentro del clip de 6.
    const ObjectId next = addTransition(p, kVideoTrack, 16, withLength(30));
    REQUIRE(next != 0);
    CHECK(p.transition(next)->before == 4);
    CHECK(transitionLimits(p, t).maxAfter == 2);
    checkInvariants(p);
}

TEST_CASE("Transitions follow splits and vanish when the cut opens", "[timeline][transitions]") {
    ObjectId a = 0;
    ObjectId b = 0;
    Project p = projectWithCut(a, b);
    const ObjectId t = addTransition(p, kVideoTrack, 300, withLength(30));
    REQUIRE(splitAt(p, 280)); // dentro de A, a 20 fotogramas del corte
    const Transition* tr = p.transition(t);
    REQUIRE(tr != nullptr);
    CHECK(tr->from != a);
    CHECK(p.clip(tr->from)->start == 280);
    CHECK(tr->before == 15);

    // Dividir más cerca del corte acorta la transición.
    REQUIRE(splitAt(p, 295));
    REQUIRE(p.transition(t) != nullptr);
    CHECK(p.transition(t)->before == 5);

    // Mover B (y su audio) separa los clips: la transición desaparece.
    Project moved = p;
    REQUIRE(moveClips(moved, {b}, 10));
    CHECK(moved.transition(t) == nullptr);
    // Mover los dos juntos la conserva.
    Project both = p;
    REQUIRE(moveClips(both, {p.transition(t)->from, b}, 50));
    CHECK(both.transition(t) != nullptr);
    // Quitarla.
    REQUIRE(removeTransition(p, t));
    CHECK(p.tracks[kVideoTrack].transitions.empty());
    checkInvariants(p);
}

TEST_CASE("Transition sounds go to a free audio track", "[timeline][transitions]") {
    ObjectId a = 0;
    ObjectId b = 0;
    Project p = projectWithCut(a, b);
    MediaItem whoosh = media("whoosh.wav", 1, false, true);
    const ObjectId sound = addMedia(p, whoosh);
    const ObjectId t = addTransition(p, kVideoTrack, 300, withLength(30));
    // A1 tiene el audio de los clips: el sonido va a A2 (nueva), con el golpe en el corte.
    const ObjectId clip = placeTransitionSound(p, t, sound, 20, -4.0);
    REQUIRE(clip != 0);
    REQUIRE(p.trackCount(TrackKind::Audio) == 2);
    CHECK(trackClips(p, TrackKind::Audio, 1)[0].id == clip);
    CHECK(p.clip(clip)->start == 280);
    CHECK(p.clip(clip)->volumeDb == -4.0);
    CHECK(p.transition(t)->sound == clip);

    // Otro sonido sustituye al anterior.
    const ObjectId again = placeTransitionSound(p, t, sound, 10);
    REQUIRE(again != 0);
    CHECK(p.clip(clip) == nullptr);
    CHECK(p.clip(again)->start == 290);

    // Si el usuario quita el sonido, la transición se queda sin él.
    Project withoutSound = p;
    REQUIRE(removeClips(withoutSound, {again}));
    CHECK(withoutSound.transition(t)->sound == 0);

    // Quitar la transición se lleva su sonido.
    REQUIRE(removeTransition(p, t));
    CHECK(p.clip(again) == nullptr);
    checkInvariants(p);
}

// --- Documentos: secuencias y carpetas --------------------------------------------------

TEST_CASE("Sequences share the media of their project", "[document]") {
    Project base;
    const ObjectId m = addMedia(base, media("a.mp4", 10, true, true));
    Document d = documentFromProject(base, "VOD");
    const ObjectId vod = d.sequences.front().id;
    ProjectSettings vertical;
    vertical.width = 1080;
    vertical.height = 1920;
    const ObjectId shortId = addSequence(d, "Short", vertical);
    CHECK(d.sequence(shortId)->tracks.size() == 4); // V1, V2, A1, A2

    // Editar una secuencia no toca la otra.
    REQUIRE(edit(d, vod, [m](Project& p) { return placeClip(p, m, 0) != 0; }));
    REQUIRE(edit(d, shortId, [m](Project& p) { return placeClip(p, m, 100, 0, 60) != 0; }));
    CHECK(d.view(vod).length() == 600);
    CHECK(d.view(shortId).length() == 160);
    CHECK(d.view(shortId).settings.height == 1920);
    CHECK_FALSE(edit(d, 12345, [](Project&) { return true; }));

    // Los ids no se repiten entre secuencias.
    const Project a = d.view(vod);
    const Project b = d.view(shortId);
    for (const Track& ta : a.tracks) {
        for (const Clip& c : ta.clips) {
            CHECK(b.clip(c.id) == nullptr);
        }
    }

    // Quitar el medio lo quita de todas (también de pistas bloqueadas, que siguen así).
    REQUIRE(edit(d, shortId, [](Project& p) { return setTrackLocked(p, kVideoTrack, true); }));
    REQUIRE(removeMediaEverywhere(d, m));
    CHECK(d.sequence(shortId)->tracks[kVideoTrack].locked);
    CHECK(d.media.empty());
    CHECK(d.view(vod).length() == 0);
    CHECK(d.view(shortId).length() == 0);
}

TEST_CASE("Duplicated sequences get fresh ids and keep links and transitions", "[document]") {
    Project base;
    const ObjectId m = addMedia(base, media("a.mp4", 20, true, true));
    placeClip(base, m, 0, 0, 300);
    placeClip(base, m, 300, 400, 200);
    const ObjectId t = addTransition(base, kVideoTrack, 300, TransitionParams{});
    REQUIRE(t != 0);
    Document d = documentFromProject(base, "VOD");
    const ObjectId vod = d.sequences.front().id;
    const ObjectId copy = duplicateSequence(d, vod, "Vertical", std::pair{1080, 1920});
    REQUIRE(copy != 0);
    const Project original = d.view(vod);
    const Project dup = d.view(copy);
    CHECK(dup.settings.width == 1080);
    CHECK(dup.settings.height == 1920);
    CHECK(dup.length() == original.length());
    for (const Track& track : dup.tracks) {
        for (const Clip& c : track.clips) {
            CHECK(original.clip(c.id) == nullptr);
            if (c.link != 0) {
                CHECK(dup.clip(c.link) != nullptr);
            }
        }
    }
    REQUIRE(dup.tracks[kVideoTrack].transitions.size() == 1);
    const Transition& dt = dup.tracks[kVideoTrack].transitions.front();
    CHECK(dt.id != t);
    CHECK(dup.clip(dt.from) != nullptr);
    CHECK(dup.cutOf(dt) == 300);

    CHECK(renameSequence(d, copy, "Short vertical"));
    CHECK(d.sequence(copy)->name == "Short vertical");
    CHECK(removeSequence(d, copy));
    CHECK_FALSE(removeSequence(d, vod)); // siempre queda una
}

TEST_CASE("Folders hold media and sequences", "[document]") {
    Project base;
    const ObjectId m = addMedia(base, media("a.mp4", 10, true, true));
    Document d = documentFromProject(base, "VOD");
    const ObjectId vod = d.sequences.front().id;
    const ObjectId folder = addFolder(d, "Clips");
    CHECK(moveToFolder(d, {m, vod}, folder));
    CHECK(d.media.front().folder == folder);
    CHECK(d.sequence(vod)->folder == folder);
    CHECK_FALSE(moveToFolder(d, {m}, 999)); // carpeta que no existe
    CHECK(renameFolder(d, folder, "Gameplay"));
    CHECK(removeFolder(d, folder));
    CHECK(d.media.front().folder == 0);
    CHECK(d.sequence(vod)->folder == 0);
}

TEST_CASE("The resources folder is found by its mark, not by its name", "[timeline][library]") {
    Document d;
    const ObjectId mine = addFolder(d, "Recursos de SharkCut"); // del usuario, sin marca
    const ObjectId resources = resourceFolder(d, "Recursos de SharkCut");
    CHECK(resources != mine);
    CHECK(d.folder(resources)->resources);
    CHECK(resourceFolder(d, "Otro nombre") == resources); // se reutiliza
    CHECK(d.folders.size() == 2);

    // Con otro nombre ya es una carpeta más: la siguiente vez se crea otra.
    CHECK(renameFolder(d, resources, "Mis sonidos"));
    CHECK_FALSE(d.folder(resources)->resources);
    const ObjectId again = resourceFolder(d, "Recursos de SharkCut");
    CHECK(again != resources);
    // Y si se borra, también.
    CHECK(removeFolder(d, again));
    CHECK(resourceFolder(d, "Recursos de SharkCut") != again);
}

// --- Edición con rizo y marcadores ---

TEST_CASE("Q and W remove up to the playhead and close the gap", "[timeline][ripple]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId b = addMedia(p, media("b.mp4", 10, true, true));
    placeClip(p, a, 0);   // 0–600
    placeClip(p, b, 600); // 600–1200
    addMarker(p, 900, "final");

    // Q a mitad del segundo clip: quita 600–750 y lo de después se adelanta.
    REQUIRE(rippleTrimPrevious(p, 750));
    checkInvariants(p);
    REQUIRE(video(p).size() == 2);
    CHECK(video(p)[1].start == 600);
    CHECK(video(p)[1].in == 150);
    CHECK(video(p)[1].length == 450);
    CHECK(audio(p)[1].start == 600); // el audio enlazado igual
    CHECK(video(p)[1].link == audio(p)[1].id);
    CHECK(p.markers[0].frame == 750);

    // W a mitad del primero: quita 300–600.
    REQUIRE(rippleTrimNext(p, 300));
    checkInvariants(p);
    CHECK(video(p)[0].length == 300);
    CHECK(video(p)[1].start == 300);
    CHECK(p.length() == 750);
    CHECK(p.markers[0].frame == 450);

    CHECK_FALSE(rippleTrimPrevious(p, 0));
    CHECK_FALSE(rippleTrimNext(p, 750)); // después del final no hay borde
}

TEST_CASE("Ripple edits skip locked tracks", "[timeline][ripple]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId music = addMedia(p, media("m.mp3", 30, false, true));
    placeClip(p, a, 0);
    placeClip(p, music, 0, PlaceTarget{0, 1}); // A2
    const std::size_t a2 = *p.trackIndex(TrackKind::Audio, 1);
    setTrackLocked(p, a2, true);

    REQUIRE(rippleTrimNext(p, 200)); // borde siguiente: el final del clip (600)
    checkInvariants(p);
    CHECK(video(p)[0].length == 200);
    CHECK(p.tracks[a2].clips[0].length == 1800); // la música no se toca
}

TEST_CASE("Ripple delete closes the gap only when the range is free", "[timeline][ripple]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    const ObjectId b = addMedia(p, media("b.mp4", 5, true, true));
    const ObjectId c = addMedia(p, media("c.mp4", 5, true, false));
    const ObjectId first = placeClip(p, a, 0);    // 0–600
    const ObjectId second = placeClip(p, b, 600); // 600–900
    placeClip(p, b, 900);                         // 900–1200

    CHECK(rippleDelete(p, {second}) == RippleResult::Closed);
    checkInvariants(p);
    REQUIRE(video(p).size() == 2);
    CHECK(video(p)[1].start == 600);
    CHECK(audio(p)[1].start == 600);

    // Con una facecam en V2 encima del tramo, el clip se quita pero el hueco se queda.
    placeClip(p, c, 100, PlaceTarget{1, 0});
    CHECK(rippleDelete(p, {first}) == RippleResult::Blocked);
    checkInvariants(p);
    REQUIRE(video(p).size() == 1);
    CHECK(video(p)[0].start == 600);
    CHECK(rippleDelete(p, {}) == RippleResult::Nothing);
}

TEST_CASE("Closing a gap moves everything after it", "[timeline][ripple]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 5, true, true));
    placeClip(p, a, 0);   // 0–300
    placeClip(p, a, 500); // 500–800
    REQUIRE(gapAt(p, kVideoTrack, 400).has_value());
    CHECK(gapAt(p, kVideoTrack, 400)->start == 300);
    CHECK(gapAt(p, kVideoTrack, 400)->end == 500);
    CHECK_FALSE(gapAt(p, kVideoTrack, 100)); // es un clip
    CHECK_FALSE(gapAt(p, kVideoTrack, 900)); // después no hay nada

    CHECK(closeGap(p, kVideoTrack, 400) == RippleResult::Closed);
    checkInvariants(p);
    CHECK(video(p)[1].start == 300);
    CHECK(audio(p)[1].start == 300);
    CHECK(p.length() == 600);
    CHECK(closeGap(p, kVideoTrack, 400) == RippleResult::Nothing);
}

TEST_CASE("Extracting a range keeps transitions between clips that still meet",
          "[timeline][ripple]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 10, true, true));
    placeClip(p, a, 0, 0, 300, {});
    placeClip(p, a, 300, 400, 300, {});
    REQUIRE(addTransition(p, kVideoTrack, 300, TransitionParams{}) != 0);
    // Quitar un tramo dentro del segundo clip no toca la transición del corte.
    REQUIRE(extractRange(p, 400, 500));
    checkInvariants(p);
    CHECK(p.tracks[kVideoTrack].transitions.size() == 1);
    // Recortar los dos lados del corte: los mismos clips siguen juntos y la transición
    // se queda (ajustada a lo que cabe).
    REQUIRE(extractRange(p, 250, 350));
    checkInvariants(p);
    REQUIRE(p.tracks[kVideoTrack].transitions.size() == 1);
    CHECK(video(p)[1].start == 250);
    // Quitar el clip entrante entero se la lleva.
    REQUIRE(extractRange(p, 250, 450));
    checkInvariants(p);
    CHECK(p.tracks[kVideoTrack].transitions.empty());
}

TEST_CASE("Markers are added once per frame and can be renamed and moved", "[timeline][markers]") {
    Project p;
    const ObjectId m1 = addMarker(p, 120, "Inicio");
    const ObjectId m2 = addMarker(p, 60);
    REQUIRE(m1 != 0);
    REQUIRE(m2 != 0);
    CHECK(addMarker(p, 120) == 0);
    CHECK(addMarker(p, -1) == 0);
    REQUIRE(p.markers.size() == 2);
    CHECK(p.markers[0].id == m2); // ordenados por fotograma
    CHECK(renameMarker(p, m2, "Jefe final"));
    CHECK_FALSE(renameMarker(p, m2, "Jefe final"));
    CHECK(moveMarker(p, m2, 300));
    CHECK(p.markers[1].id == m2);
    CHECK(removeMarker(p, m1));
    CHECK_FALSE(removeMarker(p, m1));
    CHECK(p.markers.size() == 1);
}

TEST_CASE("Markers live in their sequence and are copied with new ids", "[document][markers]") {
    Document d = documentFromProject(Project{}, "Uno");
    const ObjectId seq = d.sequences.front().id;
    REQUIRE(edit(d, seq, [](Project& p) { return addMarker(p, 30, "aquí") != 0; }));
    REQUIRE(d.sequences.front().markers.size() == 1);
    const ObjectId copy = duplicateSequence(d, seq, "Dos", std::nullopt);
    const Sequence* duplicated = d.sequence(copy);
    REQUIRE(duplicated->markers.size() == 1);
    CHECK(duplicated->markers[0].frame == 30);
    CHECK(duplicated->markers[0].id != d.sequences.front().markers[0].id);
}

TEST_CASE("Copied clips are pasted at the playhead overwriting what is there", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("a.mp4", 60, true, true));
    placeClip(p, a, 0, 0, 600);   // 0–600
    placeClip(p, a, 600, 0, 600); // 600–1200
    const ObjectId first = video(p).front().id;
    ClipTransform t;
    t.scale = 0.5;
    t.effects.grayscaleOn = true;
    REQUIRE(setClipTransform(p, first, t));
    p.tracks[*p.trackIndex(TrackKind::Audio, 0)].clips.front().volumeDb = -6.0;

    const Clipboard copied = copyClips(p, {first});
    REQUIRE(copied.clips.size() == 2); // con su audio
    CHECK(copied.length == 600);

    const std::vector<ObjectId> pasted = pasteClips(p, copied, 900);
    REQUIRE(pasted.size() == 2);
    checkInvariants(p);
    // 0–600, 600–900 (recortado), 900–1500 (pegado).
    REQUIRE(video(p).size() == 3);
    const Clip& v = video(p).back();
    CHECK(v.start == 900);
    CHECK(v.length == 600);
    CHECK(v.transform.scale == 0.5);
    CHECK(v.transform.effects.grayscaleOn);
    CHECK(video(p)[1].end() == 900);
    const Clip& sound = audio(p).back();
    CHECK(sound.start == 900);
    CHECK(sound.volumeDb == -6.0);
    CHECK(v.link == sound.id); // enlazados entre sí
    CHECK(sound.link == v.id);
    // Lo copiado sigue igual: se puede pegar otra vez.
    CHECK(pasteClips(p, copied, 2000).size() == 2);
    checkInvariants(p);
}

TEST_CASE("Pasting into another project adds the media and missing tracks", "[timeline]") {
    Project source;
    const ObjectId a = addMedia(source, media("a.mp4", 60, true, false));
    addTrack(source, TrackKind::Video);
    placeClip(source, a, 100, 0, 300, PlaceTarget{.video = 1}); // en V2
    const Clipboard copied = copyClips(source, {source.tracks[1].clips.front().id});

    Project target;
    REQUIRE(target.media.empty());
    const std::vector<ObjectId> pasted = pasteClips(target, copied, 0);
    REQUIRE(pasted.size() == 1);
    REQUIRE(target.media.size() == 1);
    CHECK(target.media.front().path == "/videos/a.mp4");
    const auto ref = target.findClip(pasted.front());
    REQUIRE(ref);
    CHECK(target.trackNumber(ref->track) == 1); // V2 (creada)
    CHECK(target.clip(pasted.front())->media == target.media.front().id);
    // Una segunda vez reutiliza el medio.
    pasteClips(target, copied, 1000);
    CHECK(target.media.size() == 1);
    checkInvariants(target);
}

TEST_CASE("Still image clips can be stretched both ways", "[timeline][imagen]") {
    Project p;
    MediaItem image = media("logo.png", 5, true, false);
    image.still = true;
    const ObjectId still = addMedia(p, image);
    const ObjectId vod = addMedia(p, media("vod.mp4", 60, true, true));
    // Se pone con la duración elegida para las imágenes (5 s a 60 fps).
    const ObjectId clip = placeClip(p, still, 600);
    REQUIRE(clip != 0);
    CHECK(p.clip(clip)->length == 300);
    REQUIRE(placeClip(p, vod, 1200) != 0);

    // Hacia la derecha, sin el límite del medio (solo el clip siguiente)…
    CHECK(limitTrimEnd(p, clip, 5000) == 1200);
    REQUIRE(trimEnd(p, clip, 1100));
    CHECK(p.clip(clip)->length == 500);
    // …y hacia la izquierda, aunque el medio «empiece» en el fotograma 0.
    CHECK(limitTrimStart(p, clip, 0) == 0);
    REQUIRE(trimStart(p, clip, 100));
    CHECK(p.clip(clip)->start == 100);
    CHECK(p.clip(clip)->in == -500);
    // Siempre se ve el único fotograma de la imagen.
    CHECK(p.mediaFrameIndex(*p.clip(clip), 100) == 0);
    CHECK(p.mediaFrameIndex(*p.clip(clip), 1099) == 0);
    const auto layers = p.videoLayersAt(700);
    REQUIRE(layers.size() == 1);
    CHECK(layers.front().mediaFrame == 0);

    // Un video sigue limitado por su duración.
    const ObjectId videoClip = p.clipAt(kVideoTrack, 1200)->id;
    CHECK(limitTrimEnd(p, videoClip, 100'000) == 1200 + 3600);
    CHECK(limitTrimStart(p, videoClip, 0) == 1200);
}

TEST_CASE("Splitting a clip keeps what is played", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("vod.mp4", 60, true, true));
    REQUIRE(placeClip(p, a, 0, 100, 600) != 0);
    const Project before = p;
    REQUIRE(splitAt(p, 250));
    REQUIRE(video(p).size() == 2);
    for (std::size_t t = 0; t < p.tracks.size(); ++t) {
        const std::vector<Clip> was = continuousClips(before.tracks[t]);
        const std::vector<Clip> now = continuousClips(p.tracks[t]);
        REQUIRE(was.size() == 1);
        REQUIRE(now.size() == 1);
        CHECK(now[0].start == was[0].start);
        CHECK(now[0].in == was[0].in);
        CHECK(now[0].length == was[0].length);
    }
    // Un salto en el medio, otro volumen o un hueco sí son otro tramo.
    Project jump = p;
    jump.tracks[kVideoTrack].clips[1].in += 10;
    CHECK(continuousClips(jump.tracks[kVideoTrack]).size() == 2);
    Project louder = p;
    const std::size_t audioTrack = *p.trackIndex(TrackKind::Audio, 0);
    louder.tracks[audioTrack].clips[1].volumeDb = 3.0;
    CHECK(continuousClips(louder.tracks[audioTrack]).size() == 2);
    REQUIRE(trimStart(p, video(p)[1].id, 260));
    CHECK(continuousClips(p.tracks[kVideoTrack]).size() == 2);
}

TEST_CASE("The nearest clip is found for tiny clips", "[timeline]") {
    Project p;
    const ObjectId a = addMedia(p, media("vod.mp4", 60, true, false));
    const ObjectId first = placeClip(p, a, 100, 0, 3);
    const ObjectId second = placeClip(p, a, 120, 0, 3);
    REQUIRE(first != 0);
    REQUIRE(second != 0);
    CHECK(p.nearestClip(kVideoTrack, 101, 5)->id == first);  // encima
    CHECK(p.nearestClip(kVideoTrack, 106, 5)->id == first);  // 4 fotogramas tras su final
    CHECK(p.nearestClip(kVideoTrack, 117, 5)->id == second); // 3 antes del siguiente
    CHECK(p.nearestClip(kVideoTrack, 111, 5) == nullptr);    // lejos de los dos
    CHECK(p.nearestClip(kVideoTrack, 50, 5) == nullptr);
    CHECK(p.nearestClip(kVideoTrack, 98, 5)->id == first);
    CHECK(p.nearestClip(kVideoTrack, 200, 1000)->id == second);
    CHECK(p.nearestClip(99, 100, 5) == nullptr); // pista que no existe
}

TEST_CASE("Edits at the exact end of the sequence", "[timeline]") {
    Project p;
    const ObjectId vod = addMedia(p, media("vod.mp4", 60, true, true));
    REQUIRE(placeClip(p, vod, 0, 0, 300) != 0);
    const std::int64_t end = p.length();
    REQUIRE(end == 300);
    // En el final exacto no hay nada que dividir ni que recortar hacia delante.
    CHECK_FALSE(splitAt(p, end));
    CHECK_FALSE(rippleTrimNext(p, end));
    // Pegar ahí deja el clip a continuación, sin pisar el último fotograma.
    const Clipboard copied = copyClips(p, {video(p).front().id});
    const auto pasted = pasteClips(p, copied, end);
    REQUIRE_FALSE(pasted.empty());
    REQUIRE(video(p).size() == 2);
    CHECK(video(p)[0].end() == 300);
    CHECK(video(p)[1].start == 300);
    CHECK(p.length() == 600);
}
