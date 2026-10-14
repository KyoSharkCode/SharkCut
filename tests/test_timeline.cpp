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
