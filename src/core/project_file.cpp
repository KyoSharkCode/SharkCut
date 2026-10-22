#include "core/project_file.h"

#include "core/clip_preset.h"

#include "core/edit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
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

using IdSet = std::set<ObjectId>;

void claim(IdSet& ids, ObjectId id) {
    if (id == 0 || !ids.insert(id).second) {
        throw ProjectError("Identificador repetido o no válido en el proyecto");
    }
}

// Comprueba que una secuencia leída es coherente; corrige lo que es seguro corregir.
// `project` es su vista (con la biblioteca del documento).
void validateSequence(Project& project, int version, IdSet& ids) {
    const ProjectSettings& s = project.settings;
    if (s.width <= 0 || s.height <= 0 || s.width > kMaxCanvas || s.height > kMaxCanvas) {
        throw ProjectError("Resolución no válida en el proyecto");
    }
    if (project.trackCount(TrackKind::Video) == 0 || project.trackCount(TrackKind::Audio) == 0) {
        throw ProjectError("El proyecto debe tener al menos una pista de video y una de audio");
    }
    // Primero las de video (de V1 hacia arriba) y después las de audio.
    std::stable_partition(project.tracks.begin(), project.tracks.end(),
                          [](const Track& t) { return t.kind == TrackKind::Video; });
    for (Track& track : project.tracks) {
        for (const Clip& clip : track.clips) {
            claim(ids, clip.id);
            const MediaItem* item = clip.isEffect() ? nullptr : project.findMedia(clip.media);
            if (!clip.isEffect() && !item) {
                throw ProjectError("Un clip hace referencia a un medio que no existe");
            }
            // Una imagen fija se alarga también hacia la izquierda: su `in` puede ser < 0.
            if (clip.length <= 0 || clip.start < 0 || (clip.in < 0 && !(item && item->still))) {
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
            claim(ids, t.id);
            t.before = std::max<std::int64_t>(0, t.before);
            t.after = std::max<std::int64_t>(0, t.after);
            t.params.intensity =
                std::clamp(std::isfinite(t.params.intensity) ? t.params.intensity : 0.6, 0.0, 1.0);
            t.params.blur =
                std::clamp(std::isfinite(t.params.blur) ? t.params.blur : 0.6, 0.0, 1.0);
        }
    }
    for (Marker& marker : project.markers) {
        claim(ids, marker.id);
        marker.frame = std::max<std::int64_t>(0, marker.frame);
    }
    std::stable_sort(project.markers.begin(), project.markers.end(),
                     [](const Marker& a, const Marker& b) { return a.frame < b.frame; });
    for (CaptionTrack& track : project.captionTracks) {
        track.style = clampCaptionStyle(track.style);
        for (Caption& c : track.captions) {
            claim(ids, c.id);
            if (c.length <= 0 || c.start < 0) {
                throw ProjectError("Subtítulo con posición o duración no válida en el proyecto");
            }
            if (c.style) {
                c.style = clampCaptionStyle(*c.style);
            }
            std::erase_if(c.words, [&c](const CaptionWord& w) {
                return w.start < 0 || w.end <= w.start || w.end > c.length;
            });
        }
        std::stable_sort(track.captions.begin(), track.captions.end(),
                         [](const Caption& a, const Caption& b) { return a.start < b.start; });
        for (std::size_t i = 1; i < track.captions.size(); ++i) {
            if (track.captions[i].start < track.captions[i - 1].end()) {
                throw ProjectError("Hay subtítulos solapados en una pista del proyecto");
            }
        }
    }
    if (version < 2) {
        linkMatchingClips(project); // los enlaces de la versión 1 se deducían así
    } else {
        repairLinks(project); // también quita las transiciones que no están en un corte
    }
}

// Comprueba el documento entero (biblioteca, carpetas y cada secuencia).
void validate(Document& document, int version) {
    IdSet ids;
    for (const MediaItem& item : document.media) {
        claim(ids, item.id);
        if (item.durationUs < 0) {
            throw ProjectError("Duración de medio no válida en el proyecto");
        }
    }
    for (const Folder& folder : document.folders) {
        claim(ids, folder.id);
    }
    if (document.sequences.empty()) {
        throw ProjectError("El proyecto no tiene ninguna secuencia");
    }
    for (Sequence& sequence : document.sequences) {
        claim(ids, sequence.id);
        if (sequence.folder != 0 && !document.folder(sequence.folder)) {
            sequence.folder = 0;
        }
        Project view = document.view(sequence.id);
        validateSequence(view, version, ids);
        sequence.settings = view.settings;
        sequence.tracks = view.tracks;
        sequence.markers = view.markers;
        sequence.captionTracks = view.captionTracks;
    }
    for (MediaItem& item : document.media) {
        if (item.folder != 0 && !document.folder(item.folder)) {
            item.folder = 0;
        }
    }
    const ObjectId maxId = ids.empty() ? 0 : *ids.rbegin();
    document.nextId = std::max(document.nextId, maxId + 1);
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

// Transición-clip: {type, duration, intensity, blur, easing}.
json effectToJson(const TransitionParams& p) {
    return {
        {"type", toString(transitionInfo(p.type).id)},
        {"duration", p.durationFrames},
        {"intensity", p.intensity},
        {"blur", p.blur},
        {"easing", toString(kEasings[static_cast<std::size_t>(p.easing)].id)},
    };
}

TransitionParams effectFromJson(const json& value) {
    const std::string type = value.at("type").get<std::string>();
    const auto info = std::find_if(kTransitions.begin(), kTransitions.end(),
                                   [&type](const TransitionInfo& i) { return i.id == type; });
    if (info == kTransitions.end()) {
        throw ProjectError("Transición desconocida en el proyecto: " + type);
    }
    TransitionParams p = info->defaults;
    p.durationFrames = std::max<std::int64_t>(2, value.value("duration", p.durationFrames));
    p.intensity = std::clamp(value.value("intensity", p.intensity), 0.0, 1.0);
    p.blur = std::clamp(value.value("blur", p.blur), 0.0, 1.0);
    const std::string easing = value.value("easing", std::string());
    for (const EasingInfo& e : kEasings) {
        if (e.id == easing) {
            p.easing = e.easing;
        }
    }
    return p;
}

std::string colorToHex(std::uint32_t color) {
    char text[8];
    std::snprintf(text, sizeof text, "#%06X", static_cast<unsigned>(color & 0xFFFFFFu));
    return text;
}

std::uint32_t colorFromHex(const std::string& text, std::uint32_t fallback) {
    if (text.size() != 7 || text[0] != '#') {
        return fallback;
    }
    std::uint32_t color = 0;
    for (std::size_t i = 1; i < text.size(); ++i) {
        const char c = text[i];
        int digit = 0;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return fallback;
        }
        color = color * 16 + static_cast<std::uint32_t>(digit);
    }
    return color;
}

json transformToJson(const ClipTransform& t) {
    json result = {
        {"x", t.x},
        {"y", t.y},
        {"scale", t.scale},
        {"rotation", t.rotation},
        {"opacity", t.opacity},
        {"crop", json::array({t.cropLeft, t.cropTop, t.cropRight, t.cropBottom})},
    };
    if (t.effects != ClipEffects{}) {
        const ClipEffects& e = t.effects;
        result["effects"] = {
            {"grayscaleOn", e.grayscaleOn},
            {"grayscale", e.grayscale},
            {"blurOn", e.blurOn},
            {"blur", e.blur},
            {"cornersOn", e.cornersOn},
            {"corners", e.corners},
            {"shadowOn", e.shadowOn},
            {"shadowColor", colorToHex(e.shadowColor)},
            {"shadowOpacity", e.shadowOpacity},
            {"shadowSoftness", e.shadowSoftness},
            {"shadowX", e.shadowX},
            {"shadowY", e.shadowY},
            {"shadowSpread", e.shadowSpread},
        };
    }
    if (t.chromaKey != ChromaKey{}) {
        const ChromaKey& k = t.chromaKey;
        result["chromaKey"] = {
            {"enabled", k.enabled},       {"color", colorToHex(k.color)},
            {"similarity", k.similarity}, {"smoothness", k.smoothness},
            {"spill", k.spill},
        };
    }
    return result;
}

ClipTransform transformFromJson(const json& value) {
    ClipTransform t;
    t.x = value.value("x", 0.0);
    t.y = value.value("y", 0.0);
    t.scale = value.value("scale", 1.0);
    t.rotation = value.value("rotation", 0.0);
    t.opacity = value.value("opacity", 1.0);
    if (const auto crop = value.find("crop"); crop != value.end()) {
        t.cropLeft = crop->at(0).get<double>();
        t.cropTop = crop->at(1).get<double>();
        t.cropRight = crop->at(2).get<double>();
        t.cropBottom = crop->at(3).get<double>();
    }
    if (const auto fx = value.find("effects"); fx != value.end()) {
        const ClipEffects d;
        ClipEffects& e = t.effects;
        e.grayscaleOn = fx->value("grayscaleOn", false);
        e.grayscale = fx->value("grayscale", d.grayscale);
        e.blurOn = fx->value("blurOn", false);
        e.blur = fx->value("blur", d.blur);
        e.cornersOn = fx->value("cornersOn", false);
        e.corners = fx->value("corners", d.corners);
        e.shadowOn = fx->value("shadowOn", false);
        e.shadowColor = colorFromHex(fx->value("shadowColor", std::string()), d.shadowColor);
        e.shadowOpacity = fx->value("shadowOpacity", d.shadowOpacity);
        e.shadowSoftness = fx->value("shadowSoftness", d.shadowSoftness);
        e.shadowX = fx->value("shadowX", d.shadowX);
        e.shadowY = fx->value("shadowY", d.shadowY);
        e.shadowSpread = fx->value("shadowSpread", d.shadowSpread);
    }
    if (const auto key = value.find("chromaKey"); key != value.end()) {
        const ChromaKey defaults;
        ChromaKey& k = t.chromaKey;
        k.enabled = key->value("enabled", false);
        k.color = colorFromHex(key->value("color", std::string()), defaults.color);
        k.similarity = key->value("similarity", defaults.similarity);
        k.smoothness = key->value("smoothness", defaults.smoothness);
        k.spill = key->value("spill", defaults.spill);
    }
    return t;
}

// Animación: {"x": [[fotograma, valor, "curva"], …], …}, solo las propiedades animadas.
json animationToJson(const ClipAnimation& animation) {
    json result = json::object();
    for (std::size_t i = 0; i < kAnimPropertyCount; ++i) {
        const auto& keys = animation.keys[i];
        if (keys.empty()) {
            continue;
        }
        json list = json::array();
        for (const Keyframe& k : keys) {
            list.push_back(json::array(
                {k.frame, k.value, std::string(kEasings[static_cast<std::size_t>(k.easing)].id)}));
        }
        result[std::string(kAnimPropertyIds[i])] = std::move(list);
    }
    return result;
}

ClipAnimation animationFromJson(const json& value) {
    ClipAnimation animation;
    for (auto it = value.begin(); it != value.end(); ++it) {
        const auto property = animPropertyFromId(it.key());
        if (!property) {
            continue; // propiedad de una versión más nueva: se ignora
        }
        auto& keys = animation.of(*property);
        for (const json& k : it.value()) {
            Keyframe key;
            key.frame = k.at(0).get<std::int64_t>();
            key.value = k.at(1).get<double>();
            if (k.size() > 2) {
                key.easing = easingFromId(k.at(2).get<std::string>()).value_or(Easing::Linear);
            }
            if (std::isfinite(key.value)) {
                keys.push_back(key);
            }
        }
        // Ordenados y sin dos en el mismo fotograma.
        std::stable_sort(keys.begin(), keys.end(),
                         [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
        keys.erase(
            std::unique(keys.begin(), keys.end(),
                        [](const Keyframe& a, const Keyframe& b) { return a.frame == b.frame; }),
            keys.end());
    }
    return animation;
}

json captionStyleToJson(const CaptionStyle& st) {
    return {
        {"font", st.font},
        {"size", st.size},
        {"bold", st.bold},
        {"italic", st.italic},
        {"uppercase", st.uppercase},
        {"color", colorToHex(st.color)},
        {"outlineColor", colorToHex(st.outlineColor)},
        {"outline", st.outline},
        {"shadowColor", colorToHex(st.shadowColor)},
        {"shadow", st.shadow},
        {"boxColor", colorToHex(st.boxColor)},
        {"box", st.box},
        {"x", st.x},
        {"y", st.y},
        {"maxWidth", st.maxWidth},
    };
}

CaptionStyle captionStyleFromJson(const json& v) {
    const CaptionStyle d;
    CaptionStyle st;
    st.font = v.value("font", d.font);
    st.size = v.value("size", d.size);
    st.bold = v.value("bold", d.bold);
    st.italic = v.value("italic", d.italic);
    st.uppercase = v.value("uppercase", d.uppercase);
    st.color = colorFromHex(v.value("color", std::string()), d.color);
    st.outlineColor = colorFromHex(v.value("outlineColor", std::string()), d.outlineColor);
    st.outline = v.value("outline", d.outline);
    st.shadowColor = colorFromHex(v.value("shadowColor", std::string()), d.shadowColor);
    st.shadow = v.value("shadow", d.shadow);
    st.boxColor = colorFromHex(v.value("boxColor", std::string()), d.boxColor);
    st.box = v.value("box", d.box);
    st.x = v.value("x", d.x);
    st.y = v.value("y", d.y);
    st.maxWidth = v.value("maxWidth", d.maxWidth);
    return st;
}

json captionTracksToJson(const std::vector<CaptionTrack>& tracks) {
    json result = json::array();
    for (const CaptionTrack& track : tracks) {
        json captions = json::array();
        for (const Caption& c : track.captions) {
            json entry = {{"id", c.id}, {"start", c.start}, {"length", c.length}, {"text", c.text}};
            if (c.style) {
                entry["style"] = captionStyleToJson(*c.style);
            }
            if (!c.words.empty()) {
                // [inicio, fin, texto] por palabra: compacto (miles de subtítulos).
                json words = json::array();
                for (const CaptionWord& w : c.words) {
                    words.push_back(json::array({w.start, w.end, w.text}));
                }
                entry["words"] = std::move(words);
            }
            captions.push_back(std::move(entry));
        }
        json entry = {{"style", captionStyleToJson(track.style)},
                      {"captions", std::move(captions)}};
        if (track.hidden) {
            entry["hidden"] = true;
        }
        if (track.locked) {
            entry["locked"] = true;
        }
        result.push_back(std::move(entry));
    }
    return result;
}

std::vector<CaptionTrack> captionTracksFromJson(const json& value) {
    std::vector<CaptionTrack> tracks;
    for (const json& entry : value) {
        CaptionTrack track;
        track.style = captionStyleFromJson(entry.value("style", json::object()));
        track.hidden = entry.value("hidden", false);
        track.locked = entry.value("locked", false);
        for (const json& c : entry.at("captions")) {
            Caption caption;
            caption.id = c.at("id").get<ObjectId>();
            caption.start = c.at("start").get<std::int64_t>();
            caption.length = c.at("length").get<std::int64_t>();
            caption.text = c.value("text", std::string());
            if (const auto st = c.find("style"); st != c.end()) {
                caption.style = captionStyleFromJson(*st);
            }
            for (const json& w : c.value("words", json::array())) {
                caption.words.push_back({w.at(0).get<std::int64_t>(), w.at(1).get<std::int64_t>(),
                                         w.at(2).get<std::string>()});
            }
            track.captions.push_back(std::move(caption));
        }
        tracks.push_back(std::move(track));
    }
    return tracks;
}

} // namespace

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8FromPath(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

namespace {

json settingsToJson(const ProjectSettings& settings) {
    return {
        {"frameRate", rateToJson(settings.frameRate)},
        {"width", settings.width},
        {"height", settings.height},
        {"adoptCanvas", settings.adoptCanvas},
        {"adoptFrameRate", settings.adoptFrameRate},
    };
}

ProjectSettings settingsFromJson(const json& value) {
    ProjectSettings settings;
    settings.frameRate = rateFromJson(value.at("frameRate"));
    settings.width = value.at("width").get<int>();
    settings.height = value.at("height").get<int>();
    // Proyectos anteriores a estos campos: el formato ya quedó fijado.
    settings.adoptCanvas = value.value("adoptCanvas", false);
    settings.adoptFrameRate = value.value("adoptFrameRate", false);
    return settings;
}

json tracksToJson(const std::vector<Track>& tracks) {
    json result = json::array();
    for (const Track& track : tracks) {
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
            if (video && !clip.animation.empty()) {
                entry["animation"] = animationToJson(clip.animation);
            }
            if (!video && clip.volumeDb != 0.0) {
                entry["volumeDb"] = clip.volumeDb;
            }
            if (video && clip.effect) {
                entry["effect"] = effectToJson(*clip.effect);
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
        result.push_back(std::move(entry));
    }
    return result;
}

std::vector<Track> tracksFromJson(const json& value) {
    std::vector<Track> tracks;
    for (const json& entry : value) {
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
            if (const auto a = c.find("animation"); a != c.end()) {
                clip.animation = animationFromJson(*a);
            }
            clip.volumeDb = c.value("volumeDb", 0.0);
            if (const auto e = c.find("effect"); e != c.end() && track.kind == TrackKind::Video) {
                clip.effect = effectFromJson(*e);
                clip.media = 0;
            }
            track.clips.push_back(clip);
        }
        if (const auto list = entry.find("transitions"); list != entry.end()) {
            for (const json& t : *list) {
                track.transitions.push_back(transitionFromJson(t));
            }
        }
        tracks.push_back(std::move(track));
    }
    return tracks;
}

} // namespace

std::string documentToJson(const Document& document, const std::filesystem::path& projectFile,
                           const Workspace& workspace) {
    json root;
    root["app"] = "SharkCut";
    root["version"] = kProjectFileVersion;

    json media = json::array();
    for (const MediaItem& item : document.media) {
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
        if (item.folder != 0) {
            entry["folder"] = item.folder;
        }
        if (item.still) {
            entry["still"] = true;
        }
        const std::string relative = relativeMediaPath(item.path, projectFile);
        if (!relative.empty()) {
            entry["relativePath"] = relative;
        }
        media.push_back(std::move(entry));
    }
    root["media"] = std::move(media);

    json folders = json::array();
    for (const Folder& folder : document.folders) {
        folders.push_back({{"id", folder.id}, {"name", folder.name}});
    }
    root["folders"] = std::move(folders);

    json sequences = json::array();
    for (const Sequence& sequence : document.sequences) {
        json entry = {
            {"id", sequence.id},
            {"name", sequence.name},
            {"settings", settingsToJson(sequence.settings)},
            {"tracks", tracksToJson(sequence.tracks)},
        };
        if (sequence.folder != 0) {
            entry["folder"] = sequence.folder;
        }
        if (!sequence.markers.empty()) {
            json markers = json::array();
            for (const Marker& marker : sequence.markers) {
                markers.push_back(
                    {{"id", marker.id}, {"frame", marker.frame}, {"name", marker.name}});
            }
            entry["markers"] = std::move(markers);
        }
        if (!sequence.captionTracks.empty()) {
            entry["captionTracks"] = captionTracksToJson(sequence.captionTracks);
        }
        sequences.push_back(std::move(entry));
    }
    root["sequences"] = std::move(sequences);
    root["openSequences"] = workspace.openSequences;
    root["activeSequence"] = workspace.activeSequence;
    root["nextId"] = document.nextId;

    try {
        return root.dump(2) + "\n";
    } catch (const json::exception&) {
        throw ProjectError("El proyecto contiene texto que no es UTF-8 válido");
    }
}

LoadedDocument documentFromJson(std::string_view text, const std::filesystem::path& projectFile) {
    LoadedDocument loaded;
    Document& document = loaded.document;
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
            item.folder = entry.value("folder", ObjectId{0});
            item.still = entry.value("still", false);
            // El proyecto se movió con sus medios: buscar por la ruta relativa.
            const std::string relative = entry.value("relativePath", std::string());
            if (!relative.empty() && !dir.empty() && !fileExists(pathFromUtf8(item.path))) {
                const auto candidate = (dir / pathFromUtf8(relative)).lexically_normal();
                if (fileExists(candidate)) {
                    item.path = utf8FromPath(candidate);
                }
            }
            document.media.push_back(std::move(item));
        }
        document.nextId = root.value("nextId", ObjectId{1});

        if (version < 4) {
            // Antes de la versión 4 había una sola secuencia, en la raíz del archivo.
            Sequence sequence;
            sequence.name =
                projectFile.empty() ? std::string("Secuencia 1") : utf8FromPath(projectFile.stem());
            sequence.settings = settingsFromJson(root.at("settings"));
            sequence.tracks = tracksFromJson(root.at("tracks"));
            document.sequences.push_back(std::move(sequence));
        } else {
            for (const json& entry : root.value("folders", json::array())) {
                document.folders.push_back(
                    {entry.at("id").get<ObjectId>(), entry.value("name", std::string())});
            }
            for (const json& entry : root.at("sequences")) {
                Sequence sequence;
                sequence.id = entry.at("id").get<ObjectId>();
                sequence.name = entry.value("name", std::string());
                sequence.folder = entry.value("folder", ObjectId{0});
                sequence.settings = settingsFromJson(entry.at("settings"));
                sequence.tracks = tracksFromJson(entry.at("tracks"));
                // Marcadores: campo opcional de la versión 4 (no existía al crearla).
                for (const json& m : entry.value("markers", json::array())) {
                    sequence.markers.push_back({m.at("id").get<ObjectId>(),
                                                m.at("frame").get<std::int64_t>(),
                                                m.value("name", std::string())});
                }
                // Subtítulos: desde la versión 6.
                if (const auto c = entry.find("captionTracks"); c != entry.end()) {
                    sequence.captionTracks = captionTracksFromJson(*c);
                }
                document.sequences.push_back(std::move(sequence));
            }
            loaded.workspace.openSequences = root.value("openSequences", std::vector<ObjectId>{});
            loaded.workspace.activeSequence = root.value("activeSequence", ObjectId{0});
        }
    } catch (const json::exception& e) {
        throw ProjectError(std::string("El archivo de proyecto está dañado (") + e.what() + ")");
    }
    if (version < 4) {
        // La secuencia necesita un id que no use nada más del archivo.
        ObjectId maxId = document.nextId;
        for (const MediaItem& item : document.media) {
            maxId = std::max(maxId, item.id + 1);
        }
        for (const Track& track : document.sequences.front().tracks) {
            for (const Clip& clip : track.clips) {
                maxId = std::max(maxId, clip.id + 1);
            }
            for (const Transition& t : track.transitions) {
                maxId = std::max(maxId, t.id + 1);
            }
        }
        document.sequences.front().id = maxId;
        document.nextId = maxId + 1;
    }
    validate(document, version);

    // Pestañas abiertas: solo secuencias que existen; al menos una.
    Workspace& ws = loaded.workspace;
    std::erase_if(ws.openSequences, [&document](ObjectId id) { return !document.sequence(id); });
    if (ws.openSequences.empty()) {
        ws.openSequences.push_back(document.sequences.front().id);
    }
    if (std::find(ws.openSequences.begin(), ws.openSequences.end(), ws.activeSequence) ==
        ws.openSequences.end()) {
        ws.activeSequence = ws.openSequences.front();
    }
    return loaded;
}

std::string projectToJson(const Project& project, const std::filesystem::path& projectFile) {
    const Document document = documentFromProject(project, "Secuencia 1");
    return documentToJson(document, projectFile,
                          {{document.sequences.front().id}, document.sequences.front().id});
}

Project projectFromJson(std::string_view text, const std::filesystem::path& projectFile) {
    const LoadedDocument loaded = documentFromJson(text, projectFile);
    Project project = loaded.document.view(loaded.workspace.activeSequence);
    project.nextId = loaded.document.nextId;
    return project;
}

namespace {

void writeAtomically(const std::string& text, const std::filesystem::path& file) {
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

std::string readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw ProjectError("No se pudo abrir el proyecto " + utf8FromPath(file));
    }
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) {
        throw ProjectError("No se pudo leer el proyecto " + utf8FromPath(file));
    }
    return text;
}

} // namespace

void saveDocument(const Document& document, const Workspace& workspace,
                  const std::filesystem::path& file) {
    writeAtomically(documentToJson(document, file, workspace), file);
}

LoadedDocument loadDocument(const std::filesystem::path& file) {
    return documentFromJson(readFile(file), file);
}

void saveProject(const Project& project, const std::filesystem::path& file) {
    writeAtomically(projectToJson(project, file), file);
}

Project loadProject(const std::filesystem::path& file) {
    return projectFromJson(readFile(file), file);
}

// --- Ajustes guardados de clips (clip_preset.h) -------------------------------------

std::string presetsToJson(const std::vector<ClipPreset>& presets) {
    json list = json::array();
    for (const ClipPreset& p : presets) {
        json keys = json::object();
        for (std::size_t i = 0; i < kAnimPropertyCount; ++i) {
            if (p.keys[i].empty()) {
                continue;
            }
            json k = json::array();
            for (const PresetKeyframe& key : p.keys[i]) {
                k.push_back(
                    json::array({key.offsetUs, key.value,
                                 std::string(kEasings[static_cast<std::size_t>(key.easing)].id)}));
            }
            keys[std::string(kAnimPropertyIds[i])] = std::move(k);
        }
        list.push_back({
            {"name", p.name},
            {"parts",
             {{"placement", p.parts.placement},
              {"opacity", p.parts.opacity},
              {"crop", p.parts.crop},
              {"effects", p.parts.effects},
              {"chroma", p.parts.chroma},
              {"animation", p.parts.animation}}},
            {"anchor", p.anchor == PresetAnchor::End ? "end" : "start"},
            {"transform", transformToJson(p.transform)},
            {"keys", std::move(keys)},
        });
    }
    try {
        return list.dump();
    } catch (const json::exception&) {
        return "[]";
    }
}

std::vector<ClipPreset> presetsFromJson(std::string_view text) {
    std::vector<ClipPreset> presets;
    json list;
    try {
        list = json::parse(text.begin(), text.end());
    } catch (const json::exception&) {
        return presets;
    }
    if (!list.is_array()) {
        return presets;
    }
    for (const json& entry : list) {
        try {
            ClipPreset p;
            p.name = entry.at("name").get<std::string>();
            const json parts = entry.value("parts", json::object());
            p.parts.placement = parts.value("placement", true);
            p.parts.opacity = parts.value("opacity", true);
            p.parts.crop = parts.value("crop", true);
            p.parts.effects = parts.value("effects", true);
            p.parts.chroma = parts.value("chroma", true);
            p.parts.animation = parts.value("animation", true);
            p.anchor = entry.value("anchor", std::string()) == "end" ? PresetAnchor::End
                                                                     : PresetAnchor::Start;
            p.transform =
                clampTransform(transformFromJson(entry.value("transform", json::object())));
            const json keys = entry.value("keys", json::object());
            for (auto it = keys.begin(); it != keys.end(); ++it) {
                const auto property = animPropertyFromId(it.key());
                if (!property) {
                    continue;
                }
                for (const json& k : it.value()) {
                    PresetKeyframe key;
                    key.offsetUs = k.at(0).get<std::int64_t>();
                    key.value = k.at(1).get<double>();
                    if (k.size() > 2) {
                        key.easing =
                            easingFromId(k.at(2).get<std::string>()).value_or(Easing::Linear);
                    }
                    if (std::isfinite(key.value)) {
                        p.keys[static_cast<std::size_t>(*property)].push_back(key);
                    }
                }
            }
            if (!p.name.empty()) {
                presets.push_back(std::move(p));
            }
        } catch (const json::exception&) {
            // ajuste dañado: se ignora
        }
    }
    return presets;
}

} // namespace sharkcut
