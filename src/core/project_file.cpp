#include "core/project_file.h"

#include "core/edit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace sharkcut {

using nlohmann::json;

namespace {

constexpr std::int64_t kMaxRatePart = (std::int64_t{1} << 31) - 1;
constexpr int kMaxCanvas = 16384;

json rateToJson(FrameRate rate) {
    return json::array({rate.num, rate.den});
}

FrameRate rateFromJson(const json& value) {
    const FrameRate rate{value.at(0).get<std::int64_t>(), value.at(1).get<std::int64_t>()};
    if (rate.num <= 0 || rate.den <= 0 || rate.num > kMaxRatePart || rate.den > kMaxRatePart) {
        throw ProjectError("Velocidad de fotogramas no válida en el proyecto");
    }
    return rate;
}

const char* kindName(TrackKind kind) {
    return kind == TrackKind::Video ? "video" : "audio";
}

std::filesystem::path projectDir(const std::filesystem::path& projectFile) {
    return projectFile.empty() ? std::filesystem::path() : projectFile.parent_path();
}

std::string relativeMediaPath(const std::string& mediaPath,
                              const std::filesystem::path& projectFile) {
    const std::filesystem::path dir = projectDir(projectFile);
    const std::filesystem::path media = pathFromUtf8(mediaPath);
    if (dir.empty() || !dir.is_absolute() || !media.is_absolute()) {
        return {};
    }
    const std::filesystem::path relative = media.lexically_relative(dir);
    if (relative.empty()) {
        return {}; // otra unidad: no hay ruta relativa
    }
    const auto text = relative.generic_u8string();
    return std::string(text.begin(), text.end());
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code ignored;
    return std::filesystem::exists(path, ignored);
}

// Comprueba que el proyecto leído es coherente; corrige lo que es seguro corregir.
void validate(Project& project) {
    const ProjectSettings& s = project.settings;
    if (s.width <= 0 || s.height <= 0 || s.width > kMaxCanvas || s.height > kMaxCanvas) {
        throw ProjectError("Resolución no válida en el proyecto");
    }

    std::set<ObjectId> ids;
    auto claim = [&ids](ObjectId id) {
        if (id == 0 || !ids.insert(id).second) {
            throw ProjectError("Identificador repetido o no válido en el proyecto");
        }
    };
    for (const MediaItem& item : project.media) {
        claim(item.id);
        if (item.durationUs < 0) {
            throw ProjectError("Duración de medio no válida en el proyecto");
        }
    }

    if (project.tracks.size() != 2 || project.tracks[kVideoTrack].kind != TrackKind::Video ||
        project.tracks[kAudioTrack].kind != TrackKind::Audio) {
        throw ProjectError("El proyecto debe tener una pista de video y una de audio");
    }
    for (Track& track : project.tracks) {
        for (const Clip& clip : track.clips) {
            claim(clip.id);
            if (!project.findMedia(clip.media)) {
                throw ProjectError("Un clip hace referencia a un medio que no existe");
            }
            if (clip.length <= 0 || clip.start < 0 || clip.in < 0) {
                throw ProjectError("Clip con posición o duración no válida en el proyecto");
            }
        }
        std::sort(track.clips.begin(), track.clips.end(),
                  [](const Clip& a, const Clip& b) { return a.start < b.start; });
        for (std::size_t i = 1; i < track.clips.size(); ++i) {
            if (track.clips[i].start < track.clips[i - 1].end()) {
                throw ProjectError("Hay clips solapados en una pista del proyecto");
            }
        }
    }

    const ObjectId maxId = ids.empty() ? 0 : *ids.rbegin();
    project.nextId = std::max(project.nextId, maxId + 1);
    relinkClips(project);
}

} // namespace

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8FromPath(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string projectToJson(const Project& project, const std::filesystem::path& projectFile) {
    json root;
    root["app"] = "SharkCut";
    root["version"] = kProjectFileVersion;
    root["settings"] = {
        {"frameRate", rateToJson(project.settings.frameRate)},
        {"width", project.settings.width},
        {"height", project.settings.height},
    };

    json media = json::array();
    for (const MediaItem& item : project.media) {
        json entry = {
            {"id", item.id},
            {"path", item.path},
            {"name", item.name},
            {"durationUs", item.durationUs},
            {"frameRate", rateToJson(item.frameRate)},
            {"width", item.width},
            {"height", item.height},
            {"video", item.hasVideo},
            {"audio", item.hasAudio},
        };
        const std::string relative = relativeMediaPath(item.path, projectFile);
        if (!relative.empty()) {
            entry["relativePath"] = relative;
        }
        media.push_back(std::move(entry));
    }
    root["media"] = std::move(media);

    json tracks = json::array();
    for (const Track& track : project.tracks) {
        json clips = json::array();
        for (const Clip& clip : track.clips) {
            clips.push_back({
                {"id", clip.id},
                {"media", clip.media},
                {"link", clip.link},
                {"start", clip.start},
                {"in", clip.in},
                {"length", clip.length},
            });
        }
        tracks.push_back({{"kind", kindName(track.kind)}, {"clips", std::move(clips)}});
    }
    root["tracks"] = std::move(tracks);
    root["nextId"] = project.nextId;

    try {
        return root.dump(2) + "\n";
    } catch (const json::exception&) {
        throw ProjectError("El proyecto contiene texto que no es UTF-8 válido");
    }
}

Project projectFromJson(std::string_view text, const std::filesystem::path& projectFile) {
    Project project;
    try {
        const json root = json::parse(text.begin(), text.end());
        const int version = root.at("version").get<int>();
        if (version > kProjectFileVersion) {
            throw ProjectError("Este proyecto se guardó con una versión más nueva de SharkCut");
        }
        if (version < 1) {
            throw ProjectError("Versión de proyecto no válida");
        }

        const json& settings = root.at("settings");
        project.settings.frameRate = rateFromJson(settings.at("frameRate"));
        project.settings.width = settings.at("width").get<int>();
        project.settings.height = settings.at("height").get<int>();

        const std::filesystem::path dir = projectDir(projectFile);
        for (const json& entry : root.at("media")) {
            MediaItem item;
            item.id = entry.at("id").get<ObjectId>();
            item.path = entry.at("path").get<std::string>();
            item.name = entry.value("name", std::string());
            item.durationUs = entry.at("durationUs").get<std::int64_t>();
            item.frameRate = rateFromJson(entry.at("frameRate"));
            item.width = entry.value("width", 0);
            item.height = entry.value("height", 0);
            item.hasVideo = entry.at("video").get<bool>();
            item.hasAudio = entry.at("audio").get<bool>();
            // El proyecto se movió con sus medios: buscar por la ruta relativa.
            const std::string relative = entry.value("relativePath", std::string());
            if (!relative.empty() && !dir.empty() && !fileExists(pathFromUtf8(item.path))) {
                const auto candidate = (dir / pathFromUtf8(relative)).lexically_normal();
                if (fileExists(candidate)) {
                    item.path = utf8FromPath(candidate);
                }
            }
            project.media.push_back(std::move(item));
        }

        project.tracks.clear();
        for (const json& entry : root.at("tracks")) {
            Track track;
            const std::string kind = entry.at("kind").get<std::string>();
            if (kind == "video") {
                track.kind = TrackKind::Video;
            } else if (kind == "audio") {
                track.kind = TrackKind::Audio;
            } else {
                throw ProjectError("Tipo de pista desconocido: " + kind);
            }
            for (const json& c : entry.at("clips")) {
                Clip clip;
                clip.id = c.at("id").get<ObjectId>();
                clip.media = c.at("media").get<ObjectId>();
                clip.start = c.at("start").get<std::int64_t>();
                clip.in = c.at("in").get<std::int64_t>();
                clip.length = c.at("length").get<std::int64_t>();
                track.clips.push_back(clip);
            }
            project.tracks.push_back(std::move(track));
        }
        project.nextId = root.value("nextId", ObjectId{1});
    } catch (const json::exception& e) {
        throw ProjectError(std::string("El archivo de proyecto está dañado (") + e.what() + ")");
    }
    validate(project);
    return project;
}

void saveProject(const Project& project, const std::filesystem::path& file) {
    const std::string text = projectToJson(project, file);
    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw ProjectError("No se pudo escribir el proyecto en " + utf8FromPath(file));
        }
    }
    std::error_code error;
    std::filesystem::rename(temp, file, error); // reemplaza el anterior de una vez
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw ProjectError("No se pudo guardar el proyecto en " + utf8FromPath(file));
    }
}

Project loadProject(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw ProjectError("No se pudo abrir el proyecto " + utf8FromPath(file));
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) {
        throw ProjectError("No se pudo leer el proyecto " + utf8FromPath(file));
    }
    return projectFromJson(text, file);
}

} // namespace sharkcut
