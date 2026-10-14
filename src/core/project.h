#pragma once

// Proyecto de SharkCut: ajustes, biblioteca de medios y línea de tiempo.
//
// Fase 2: una pista de video (V1) y una de audio (A1). Al colocar un medio se crea
// un clip en cada pista que corresponda (video y/o audio) y los dos quedan
// enlazados: comparten medio, posición y tramo, y se editan siempre juntos.
//
// Todas las posiciones y duraciones de la línea de tiempo están en fotogramas del
// proyecto (settings.frameRate). Un clip muestra el tramo [in, in + length) del medio,
// también medido en fotogramas del proyecto desde el inicio del medio: así un medio
// a 30 fps en un proyecto a 60 fps no necesita conversiones al editar.

#include "core/time.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sharkcut {

using ObjectId = std::uint64_t; // identificador único dentro del proyecto; 0 = ninguno

struct MediaItem {
    ObjectId id = 0;
    std::string path; // ruta absoluta en UTF-8
    std::string name; // nombre para mostrar (normalmente el del archivo)
    std::int64_t durationUs = 0;
    FrameRate frameRate{60, 1}; // del video (o la del proyecto si solo hay audio)
    int width = 0;
    int height = 0;
    bool hasVideo = false;
    bool hasAudio = false;

    friend bool operator==(const MediaItem&, const MediaItem&) = default;
};

enum class TrackKind { Video, Audio };

struct Clip {
    ObjectId id = 0;
    ObjectId media = 0;
    ObjectId link = 0;       // clip enlazado de la otra pista; 0 = ninguno
    std::int64_t start = 0;  // primer fotograma en la línea de tiempo
    std::int64_t in = 0;     // desfase dentro del medio
    std::int64_t length = 0; // fotogramas (siempre > 0)

    std::int64_t end() const { return start + length; } // primer fotograma excluido
    std::int64_t out() const { return in + length; }    // fin del tramo en el medio

    friend bool operator==(const Clip&, const Clip&) = default;
};

struct Track {
    TrackKind kind = TrackKind::Video;
    std::vector<Clip> clips; // ordenados por `start` y sin solaparse

    friend bool operator==(const Track&, const Track&) = default;
};

// Índices de las pistas de la fase 2.
inline constexpr std::size_t kVideoTrack = 0;
inline constexpr std::size_t kAudioTrack = 1;

struct ProjectSettings {
    FrameRate frameRate{60, 1};
    int width = 1920;
    int height = 1080;

    friend bool operator==(const ProjectSettings&, const ProjectSettings&) = default;
};

struct Project {
    ProjectSettings settings;
    std::vector<MediaItem> media;
    std::vector<Track> tracks{Track{TrackKind::Video, {}}, Track{TrackKind::Audio, {}}};
    ObjectId nextId = 1;

    struct ClipRef {
        std::size_t track = 0;
        std::size_t index = 0;
    };

    // Final de la línea de tiempo (fin del último clip de cualquier pista).
    std::int64_t length() const;

    const MediaItem* findMedia(ObjectId id) const;
    std::optional<ClipRef> findClip(ObjectId id) const;
    const Clip* clip(ObjectId id) const;

    // Clip de la pista `track` que se ve en `frame`, o nullptr si hay un hueco.
    const Clip* clipAt(std::size_t track, std::int64_t frame) const;

    // Duración del medio en fotogramas del proyecto.
    std::int64_t mediaFrames(const MediaItem& item) const;

    // Instante del medio (µs desde su inicio) que se ve en el fotograma `frame` de
    // la línea de tiempo, si `frame` cae dentro del clip.
    std::int64_t mediaTimeUs(const Clip& clip, std::int64_t frame) const;

    friend bool operator==(const Project&, const Project&) = default;
};

} // namespace sharkcut
