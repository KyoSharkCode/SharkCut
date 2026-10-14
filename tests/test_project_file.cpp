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
    CHECK(loaded == original);

    // Guardar otra vez reemplaza el archivo.
    saveProject(loaded, file);
    CHECK(loadProject(file) == original);
}

TEST_CASE("Project file has a version field", "[project]") {
    const std::string json = projectToJson(Project{}, {});
    CHECK(json.find("\"version\": 1") != std::string::npos);
    CHECK(projectFromJson(json, {}) == Project{});
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
    newer.replace(newer.find("\"version\": 1"), 12, "\"version\": 99");
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
    CHECK(loaded.nextId == p.nextId);
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
