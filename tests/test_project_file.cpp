#include "core/edit.h"
#include "core/project_file.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace sharkcut;
namespace fs = std::filesystem;

namespace {

// Carpeta temporal propia de cada test; se borra al terminar.
struct TempDir {
    fs::path path;
    explicit TempDir(const char* name) {
        path = fs::temp_directory_path() / (std::string("sharkcut-test-") + name);
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

// Mismo contenido (al guardarse como documento, la secuencia gasta un id más).
bool sameContent(const Project& a, const Project& b) {
    return a.settings == b.settings && a.media == b.media && a.tracks == b.tracks &&
           a.nextId <= b.nextId + 1 && b.nextId <= a.nextId + 1;
}

void touch(const fs::path& file) {
    fs::create_directories(file.parent_path());
    std::ofstream(file) << "x";
}

Project sampleProject(const std::string& mediaPath) {
    Project p;
    p.settings.frameRate = {30000, 1001};
    p.settings.width = 1080;
    p.settings.height = 1920;
    p.settings.adoptCanvas = false; // lienzo elegido en "Nuevo proyecto"
    MediaItem item;
    item.path = mediaPath;
    item.name = "Directo ñandú.mp4";
    item.durationUs = 12'345'678;
    item.frameRate = {60, 1};
    item.width = 1920;
    item.height = 1080;
    item.hasVideo = true;
    item.hasAudio = true;
    const ObjectId a = addMedia(p, item);
    placeClip(p, a, 0, 10, 200);
    placeClip(p, a, 250, 50, 100);
    splitAt(p, 100);
    return p;
}

} // namespace

TEST_CASE("Project survives a save and load round trip", "[project]") {
    TempDir dir("roundtrip");
    const fs::path media = dir.path / u8"vídeos" / u8"Directo ñandú.mp4";
    touch(media);
    const Project original = sampleProject(utf8FromPath(media));
    const fs::path file = dir.path / u8"Proyecto ñ.sharkp";

    saveProject(original, file);
    CHECK(fs::exists(file));
    CHECK_FALSE(fs::exists(fs::path(file).concat(".tmp")));

    const Project loaded = loadProject(file);
    CHECK(sameContent(loaded, original));

    // Guardar otra vez reemplaza el archivo.
    saveProject(loaded, file);
    CHECK(sameContent(loadProject(file), loaded));
}

TEST_CASE("Project file has a version field", "[project]") {
    const std::string json = projectToJson(Project{}, {});
    CHECK(json.find("\"version\": 6") != std::string::npos);
    CHECK(sameContent(projectFromJson(json, {}), Project{}));
}

TEST_CASE("Moved projects find their media by relative path", "[project]") {
    TempDir dir("relative");
    const fs::path oldRoot = dir.path / "antes";
    const fs::path newRoot = dir.path / "despues";
    const fs::path oldMedia = oldRoot / "clips" / "a.mp4";
    touch(oldMedia);
    saveProject(sampleProject(utf8FromPath(oldMedia)), oldRoot / "p.sharkp");

    // Se mueve la carpeta entera: la ruta absoluta guardada ya no existe.
    fs::rename(oldRoot, newRoot);
    const Project loaded = loadProject(newRoot / "p.sharkp");
    REQUIRE(loaded.media.size() == 1);
    CHECK(pathFromUtf8(loaded.media[0].path) == (newRoot / "clips" / "a.mp4").lexically_normal());
}

TEST_CASE("Invalid project files are rejected", "[project]") {
    const std::string valid = projectToJson(sampleProject("/no/existe.mp4"), {});
    CHECK_NOTHROW(projectFromJson(valid, {}));

    CHECK_THROWS_AS(projectFromJson("{ esto no es json", {}), ProjectError);
    CHECK_THROWS_AS(projectFromJson("{}", {}), ProjectError);

    std::string newer = valid;
    newer.replace(newer.find("\"version\": 6"), 12, "\"version\": 99");
    CHECK_THROWS_AS(projectFromJson(newer, {}), ProjectError);

    // Clips solapados.
    std::string overlap = valid;
    const auto pos = overlap.find("\"start\": 250");
    REQUIRE(pos != std::string::npos);
    overlap.replace(pos, 12, "\"start\": 150");
    CHECK_THROWS_AS(projectFromJson(overlap, {}), ProjectError);

    // Clip que apunta a un medio que no existe.
    std::string orphan = valid;
    const auto mediaPos = orphan.find("\"media\": 1");
    REQUIRE(mediaPos != std::string::npos);
    orphan.replace(mediaPos, 10, "\"media\": 77");
    CHECK_THROWS_AS(projectFromJson(orphan, {}), ProjectError);

    CHECK_THROWS_AS(loadProject("/no/existe/proyecto.sharkp"), ProjectError);
}

TEST_CASE("Loading repairs the next id", "[project]") {
    Project p = sampleProject("/x.mp4");
    std::string json = projectToJson(p, {});
    // Un archivo editado a mano con nextId demasiado bajo.
    json.replace(json.find("\"nextId\": "), 10, "\"nextId\": 1, \"old\": ");
    const Project loaded = projectFromJson(json, {});
    CHECK(loaded.nextId > p.nextId); // ningún id repetido
    CHECK(loaded.tracks == p.tracks);
}

TEST_CASE("Older project files keep their format", "[project]") {
    // Sin los campos adopt*: el formato ya estaba fijado y no debe cambiar al añadir clips.
    const std::string json = R"({"version": 1,
        "settings": {"frameRate": [30, 1], "width": 1080, "height": 1920},
        "media": [], "tracks": [{"kind": "video", "clips": []}, {"kind": "audio", "clips": []}]})";
    const Project p = projectFromJson(json, {});
    CHECK_FALSE(p.settings.adoptCanvas);
    CHECK_FALSE(p.settings.adoptFrameRate);
    CHECK(Project{}.settings.adoptCanvas);
}

TEST_CASE("Version 2 keeps tracks, links, transforms and volume", "[project]") {
    Project p = sampleProject("/x.mp4");
    const ObjectId media = p.media[0].id;
    // Facecam en V2 (sin su audio) y música en A2, con la pista A2 silenciada.
    const ObjectId cam = placeClip(p, media, 0, 0, 120, {.video = 1, .audio = 1});
    REQUIRE(cam != 0);
    const ObjectId camAudio = p.clip(cam)->link;
    REQUIRE(unlinkClips(p, {cam}));
    REQUIRE(removeClips(p, {camAudio}));
    ClipTransform t;
    t.x = 270.5;
    t.y = -640;
    t.scale = 0.35;
    t.opacity = 0.8;
    t.cropLeft = 0.7;
    t.cropBottom = 0.6;
    REQUIRE(setClipTransform(p, cam, t));
    const ObjectId gameplay = p.tracks[kVideoTrack].clips[0].id;
    REQUIRE(setClipVolume(p, p.clip(gameplay)->link, -6.5));
    REQUIRE(setTrackMuted(p, *p.trackIndex(TrackKind::Audio, 1), true));
    REQUIRE(setTrackLocked(p, kVideoTrack, true));
    REQUIRE(setTrackHidden(p, *p.trackIndex(TrackKind::Video, 1), true));
    REQUIRE(p.tracks.size() == 4);

    const Project loaded = projectFromJson(projectToJson(p, {}), {});
    CHECK(sameContent(loaded, p));
    CHECK(loaded.clip(cam)->link == 0);
    CHECK(loaded.clip(cam)->transform == t);
    CHECK(loaded.clip(p.clip(gameplay)->link)->volumeDb == -6.5);
}

TEST_CASE("Version 1 files open with linked clips", "[project]") {
    const std::string json = R"({"version": 1,
        "settings": {"frameRate": [60, 1], "width": 1920, "height": 1080},
        "media": [{"id": 1, "path": "/a.mp4", "durationUs": 10000000, "frameRate": [60, 1],
                   "video": true, "audio": true}],
        "tracks": [{"kind": "video", "clips": [{"id": 2, "media": 1, "link": 3, "start": 0,
                                                 "in": 0, "length": 100}]},
                   {"kind": "audio", "clips": [{"id": 3, "media": 1, "link": 2, "start": 0,
                                                 "in": 0, "length": 100}]}],
        "nextId": 4})";
    const Project p = projectFromJson(json, {});
    REQUIRE(p.tracks.size() == 2);
    CHECK(p.clip(2)->link == 3);
    CHECK(p.clip(3)->link == 2);
    CHECK(p.clip(2)->transform == ClipTransform{});
    CHECK(p.clip(3)->volumeDb == 0.0);
}

TEST_CASE("Version 2 files drop broken links and sort tracks", "[project]") {
    // A1 antes que V1 en el archivo, y un enlace a un clip de otra posición.
    const std::string json = R"({"version": 2,
        "settings": {"frameRate": [60, 1], "width": 1920, "height": 1080},
        "media": [{"id": 1, "path": "/a.mp4", "durationUs": 10000000, "frameRate": [60, 1],
                   "video": true, "audio": true}],
        "tracks": [{"kind": "audio", "clips": [{"id": 3, "media": 1, "link": 2, "start": 5,
                                                 "in": 0, "length": 100, "volumeDb": 99}]},
                   {"kind": "video", "clips": [{"id": 2, "media": 1, "link": 3, "start": 0,
                                                 "in": 0, "length": 100,
                                                 "transform": {"scale": 0, "opacity": 2}}]}],
        "nextId": 4})";
    const Project p = projectFromJson(json, {});
    REQUIRE(p.tracks.size() == 2);
    CHECK(p.tracks[0].kind == TrackKind::Video);
    CHECK(p.tracks[1].kind == TrackKind::Audio);
    CHECK(p.clip(2)->link == 0);
    CHECK(p.clip(3)->link == 0);
    CHECK(p.clip(2)->transform.scale == kMinScale);
    CHECK(p.clip(2)->transform.opacity == 1.0);
    CHECK(p.clip(3)->volumeDb == kMaxVolumeDb);
}

TEST_CASE("Version 3 keeps transitions and their sound", "[project]") {
    Project p = sampleProject("/x.mp4");
    // sampleProject deja dos clips seguidos en V1 (partidos en el fotograma 100).
    TransitionParams params = transitionInfo(TransitionType::ShakeDistort).defaults;
    params.durationFrames = 20;
    params.intensity = 0.25;
    params.easing = Easing::EaseOutBack;
    const ObjectId t = addTransition(p, kVideoTrack, 100, params);
    REQUIRE(t != 0);
    MediaItem whoosh;
    whoosh.path = "/sfx/whoosh.wav";
    whoosh.name = "whoosh.wav";
    whoosh.durationUs = 600'000;
    whoosh.hasAudio = true;
    const ObjectId sound = addMedia(p, whoosh);
    REQUIRE(placeTransitionSound(p, t, sound, 18, -3.0) != 0);

    const Project loaded = projectFromJson(projectToJson(p, {}), {});
    CHECK(sameContent(loaded, p));
    const Transition* lt = loaded.transition(t);
    REQUIRE(lt != nullptr);
    CHECK(lt->params.type == TransitionType::ShakeDistort);
    CHECK(lt->params.easing == Easing::EaseOutBack);
    CHECK(lt->params.intensity == 0.25);
    CHECK(lt->length() == 20);
    CHECK(loaded.clip(lt->sound) != nullptr);
}

TEST_CASE("Transitions outside a cut are dropped when loading", "[project]") {
    Project p = sampleProject("/x.mp4");
    REQUIRE(addTransition(p, kVideoTrack, 100, {}) != 0);
    std::string json = projectToJson(p, {});
    // Un archivo editado a mano: la transición apunta a clips que no están seguidos.
    const auto pos = json.find("\"to\": ");
    REQUIRE(pos != std::string::npos);
    const auto end = json.find(',', pos);
    json.replace(pos, end - pos,
                 "\"to\": " + std::to_string(p.tracks[kVideoTrack].clips.back().id));
    const Project loaded = projectFromJson(json, {});
    CHECK(loaded.tracks[kVideoTrack].transitions.empty());
}

// --- Documentos: varias secuencias y carpetas ------------------------------------------

namespace {

Document sampleDocument() {
    Document d = documentFromProject(sampleProject("/x.mp4"), "VOD completo");
    const ObjectId vod = d.sequences.front().id;
    ProjectSettings vertical;
    vertical.width = 1080;
    vertical.height = 1920;
    vertical.adoptCanvas = false;
    const ObjectId shorts = addFolder(d, "Shorts");
    addSequence(d, "Short 1", vertical, shorts);
    duplicateSequence(d, vod, "Short 2", std::pair{1080, 1920});
    moveToFolder(d, {d.media.front().id}, shorts);
    return d;
}

} // namespace

TEST_CASE("Version 4 keeps sequences, folders and open tabs", "[project]") {
    const Document d = sampleDocument();
    REQUIRE(d.sequences.size() == 3);
    const Workspace ws{{d.sequences[0].id, d.sequences[2].id}, d.sequences[2].id};
    const LoadedDocument loaded = documentFromJson(documentToJson(d, {}, ws), {});
    CHECK(loaded.document == d);
    CHECK(loaded.workspace.openSequences == ws.openSequences);
    CHECK(loaded.workspace.activeSequence == ws.activeSequence);
}

TEST_CASE("Older files open as a document with one sequence", "[project]") {
    const std::string json = R"({"version": 3,
        "settings": {"frameRate": [60, 1], "width": 1920, "height": 1080},
        "media": [{"id": 1, "path": "/a.mp4", "durationUs": 10000000, "frameRate": [60, 1],
                   "video": true, "audio": true}],
        "tracks": [{"kind": "video", "clips": [{"id": 2, "media": 1, "link": 0, "start": 0,
                                                 "in": 0, "length": 100}]},
                   {"kind": "audio", "clips": []}],
        "nextId": 3})";
    const LoadedDocument loaded = documentFromJson(json, "/proyectos/Mi VOD.sharkp");
    REQUIRE(loaded.document.sequences.size() == 1);
    const Sequence& s = loaded.document.sequences.front();
    CHECK(s.name == "Mi VOD");
    CHECK(s.id >= 3);
    CHECK(loaded.document.nextId > s.id);
    CHECK(loaded.workspace.activeSequence == s.id);
    CHECK(loaded.document.view(s.id).clip(2) != nullptr);
}

TEST_CASE("Broken workspace entries are ignored", "[project]") {
    const Document d = sampleDocument();
    const LoadedDocument loaded =
        documentFromJson(documentToJson(d, {}, {{999, d.sequences[1].id}, 998}), {});
    CHECK(loaded.workspace.openSequences == std::vector<ObjectId>{d.sequences[1].id});
    CHECK(loaded.workspace.activeSequence == d.sequences[1].id);
}

TEST_CASE("Markers survive a round trip and broken ones are fixed", "[project][markers]") {
    Document d = sampleDocument();
    const ObjectId seq = d.sequences[1].id;
    REQUIRE(edit(d, seq, [](Project& p) {
        return addMarker(p, 90, "Jefe") != 0 && addMarker(p, 10, "Inicio") != 0;
    }));
    const LoadedDocument loaded = documentFromJson(documentToJson(d, {}, {}), {});
    CHECK(loaded.document == d);

    // Fotograma negativo: se lleva al 0. Id repetido: el archivo no vale.
    std::string text = documentToJson(d, {}, {});
    const auto pos = text.find("\"frame\": 10");
    REQUIRE(pos != std::string::npos);
    text.replace(pos, 11, "\"frame\": -5");
    const LoadedDocument fixed = documentFromJson(text, {});
    CHECK(fixed.document.sequence(seq)->markers.front().frame == 0);
}

TEST_CASE("Chroma key survives a round trip and bad values are clamped", "[project][croma]") {
    Project p = sampleProject("/medios/croma.mp4");
    const ObjectId clip = p.tracks[kVideoTrack].clips.front().id;
    ClipTransform t;
    t.scale = 0.5;
    t.chromaKey.enabled = true;
    t.chromaKey.color = 0x12A34F;
    t.chromaKey.similarity = 0.25;
    t.chromaKey.smoothness = 0.12;
    t.chromaKey.spill = 0.2;
    REQUIRE(setClipTransform(p, clip, t));

    const std::string json = projectToJson(p, {});
    CHECK(json.find("#12A34F") != std::string::npos);
    const Project loaded = projectFromJson(json, {});
    CHECK(loaded.clip(clip)->transform == t);

    // Sin croma, el clip no guarda nada de él.
    Project plain = sampleProject("/medios/croma.mp4");
    CHECK(projectToJson(plain, {}).find("chromaKey") == std::string::npos);

    // Valores fuera de rango (archivo editado a mano): se ajustan al abrir.
    ClipTransform bad = t;
    bad.chromaKey.similarity = 7.0;
    bad.chromaKey.spill = -1.0;
    bad.chromaKey.color = 0xFF123456u;
    const ClipTransform fixed = clampTransform(bad);
    CHECK(fixed.chromaKey.similarity == kMaxChromaDistance);
    CHECK(fixed.chromaKey.spill == 0.0);
    CHECK(fixed.chromaKey.color == 0x123456u);
}
