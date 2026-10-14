#include "core/project_file.h"

#include "core/edit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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
void validate(Project& project, int version) {
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

    if (project.trackCount(TrackKind::Video) == 0 || project.trackCount(TrackKind::Audio) == 0) {
        throw ProjectError("El proyecto debe tener al menos una pista de video y una de audio");
    }
    // Primero las de video (de V1 hacia arriba) y después las de audio.
    std::stable_partition(project.tracks.begin(), project.tracks.end(),
                          [](const Track& t) { return t.kind == TrackKind::Video; });
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
        for (Clip& clip : track.clips) {
            clip.transform = clampTransform(clip.transform);
            if (!std::isfinite(clip.volumeDb)) {
                clip.volumeDb = 0.0;
            }
            clip.volumeDb = std::clamp(clip.volumeDb, kMinVolumeDb, kMaxVolumeDb);
        }
        std::sort(track.clips.begin(), track.clips.end(),
                  [](const Clip& a, const Clip& b) { return a.start < b.start; });
        for (std::size_t i = 1; i < track.clips.size(); ++i) {
            if (track.clips[i].start < track.clips[i - 1].end()) {
                throw ProjectError("Hay clips solapados en una pista del proyecto");
            }
        }
        for (Transition& t : track.transitions) {
            claim(t.id);
            t.before = std::max<std::int64_t>(0, t.before);
            t.after = std::max<std::int64_t>(0, t.after);
            t.params.intensity =
                std::clamp(std::isfinite(t.params.intensity) ? t.params.intensity : 0.6, 0.0, 1.0);
            t.params.blur =
                std::clamp(std::isfinite(t.params.blur) ? t.params.blur : 0.6, 0.0, 1.0);
        }
    }

    const ObjectId maxId = ids.empty() ? 0 : *ids.rbegin();
    project.nextId = std::max(project.nextId, maxId + 1);
    if (version < 2) {
        linkMatchingClips(project); // los enlaces de la versión 1 se deducían así
    } else {
        repairLinks(project); // también quita las transiciones que no están en un corte
    }
}

std::string toString(std::string_view text) {
    return std::string(text.begin(), text.end());
}

json transitionToJson(const Transition& t) {
    return {
        {"id", t.id},
        {"from", t.from},
        {"to", t.to},
        {"before", t.before},
        {"after", t.after},
        {"type", toString(transitionInfo(t.params.type).id)},
        {"intensity", t.params.intensity},
        {"blur", t.params.blur},
        {"easing", toString(kEasings[static_cast<std::size_t>(t.params.easing)].id)},
        {"sound", t.sound},
    };
}

Transition transitionFromJson(const json& value) {
    Transition t;
    t.id = value.at("id").get<ObjectId>();
    t.from = value.at("from").get<ObjectId>();
    t.to = value.at("to").get<ObjectId>();
    t.before = value.at("before").get<std::int64_t>();
    t.after = value.at("after").get<std::int64_t>();
    const std::string type = value.at("type").get<std::string>();
    const auto info = std::find_if(kTransitions.begin(), kTransitions.end(),
                                   [&type](const TransitionInfo& i) { return i.id == type; });
    if (info == kTransitions.end()) {
        throw ProjectError("Transición desconocida en el proyecto: " + type);
    }
    t.params = info->defaults;
    t.params.intensity = value.value("intensity", t.params.intensity);
    t.params.blur = value.value("blur", t.params.blur);
    const std::string easing = value.value("easing", std::string());
    for (const EasingInfo& e : kEasings) {
        if (e.id == easing) {
            t.params.easing = e.easing;
        }
    }
    t.params.durationFrames = t.before + t.after;
    t.sound = value.value("sound", ObjectId{0});
    return t;
}

json transformToJson(const ClipTransform& t) {
    return {
        {"x", t.x},
        {"y", t.y},
        {"scale", t.scale},
        {"opacity", t.opacity},
        {"crop", json::array({t.cropLeft, t.cropTop, t.cropRight, t.cropBottom})},
    };
}

ClipTransform transformFromJson(const json& value) {
    ClipTransform t;
    t.x = value.value("x", 0.0);
    t.y = value.value("y", 0.0);
    t.scale = value.value("scale", 1.0);
    t.opacity = value.value("opacity", 1.0);
    if (const auto crop = value.find("crop"); crop != value.end()) {
        t.cropLeft = crop->at(0).get<double>();
        t.cropTop = crop->at(1).get<double>();
        t.cropRight = crop->at(2).get<double>();
        t.cropBottom = crop->at(3).get<double>();
    }
    return t;
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
        {"adoptCanvas", project.settings.adoptCanvas},
        {"adoptFrameRate", project.settings.adoptFrameRate},
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
        const bool video = track.kind == TrackKind::Video;
        json clips = json::array();
        for (const Clip& clip : track.clips) {
            json entry = {
                {"id", clip.id},       {"media", clip.media}, {"link", clip.link},
                {"start", clip.start}, {"in", clip.in},       {"length", clip.length},
            };
            if (video && clip.transform != ClipTransform{}) {
                entry["transform"] = transformToJson(clip.transform);
            }
            if (!video && clip.volumeDb != 0.0) {
                entry["volumeDb"] = clip.volumeDb;
            }
            clips.push_back(std::move(entry));
        }
        json entry = {{"kind", kindName(track.kind)}, {"clips", std::move(clips)}};
        if (track.hidden) {
            entry["hidden"] = true;
        }
        if (track.muted) {
            entry["muted"] = true;
        }
        if (track.locked) {
            entry["locked"] = true;
        }
        if (!track.transitions.empty()) {
            json transitions = json::array();
            for (const Transition& t : track.transitions) {
                transitions.push_back(transitionToJson(t));
            }
            entry["transitions"] = std::move(transitions);
        }
        tracks.push_back(std::move(entry));
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
    int version = 0;
    try {
        const json root = json::parse(text.begin(), text.end());
        version = root.at("version").get<int>();
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
        // Proyectos anteriores a estos campos: el formato ya quedó fijado.
        project.settings.adoptCanvas = settings.value("adoptCanvas", false);
        project.settings.adoptFrameRate = settings.value("adoptFrameRate", false);

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
            track.hidden = track.kind == TrackKind::Video && entry.value("hidden", false);
            track.muted = track.kind == TrackKind::Audio && entry.value("muted", false);
            track.locked = entry.value("locked", false);
            for (const json& c : entry.at("clips")) {
                Clip clip;
                clip.id = c.at("id").get<ObjectId>();
                clip.media = c.at("media").get<ObjectId>();
                clip.link = c.value("link", ObjectId{0});
                clip.start = c.at("start").get<std::int64_t>();
                clip.in = c.at("in").get<std::int64_t>();
                clip.length = c.at("length").get<std::int64_t>();
                if (const auto t = c.find("transform"); t != c.end()) {
                    clip.transform = transformFromJson(*t);
                }
                clip.volumeDb = c.value("volumeDb", 0.0);
                track.clips.push_back(clip);
            }
            if (const auto list = entry.find("transitions"); list != entry.end()) {
                for (const json& t : *list) {
                    track.transitions.push_back(transitionFromJson(t));
                }
            }
            project.tracks.push_back(std::move(track));
        }
        project.nextId = root.value("nextId", ObjectId{1});
    } catch (const json::exception& e) {
        throw ProjectError(std::string("El archivo de proyecto está dañado (") + e.what() + ")");
    }
    validate(project, version);
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
