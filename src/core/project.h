#pragma once

// Proyecto de SharkCut: ajustes, biblioteca de medios y línea de tiempo.
//
// Pistas en número variable: V1, V2… (cada una se dibuja encima de la anterior) y
// A1, A2… (se mezclan todas). En `tracks` van primero las de video, de V1 hacia
// arriba, y después las de audio, de A1 hacia abajo. Al colocar un medio se crea un
// clip en una pista de cada tipo que corresponda (video y/o audio) y los dos quedan
// enlazados: comparten medio, posición y tramo, y se editan siempre juntos (hasta
// que se desvinculan).
//
// Todas las posiciones y duraciones de la línea de tiempo están en fotogramas del
// proyecto (settings.frameRate). Un clip muestra el tramo [in, in + length) del medio,
// también medido en fotogramas del proyecto desde el inicio del medio: así un medio
// a 30 fps en un proyecto a 60 fps no necesita conversiones al editar.

#include "core/time.h"
#include "core/transition.h"

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
    ObjectId folder = 0; // carpeta de la biblioteca (0 = raíz)

    friend bool operator==(const MediaItem&, const MediaItem&) = default;
};

enum class TrackKind { Video, Audio };

// Colocación de un clip de video en el lienzo. Sin transformar (los valores por
// defecto), el video se ajusta al lienzo sin deformarse y centrado.
struct ClipTransform {
    double x = 0.0;     // desplazamiento del centro en píxeles del lienzo (+x derecha)
    double y = 0.0;     // (+y abajo)
    double scale = 1.0; // 1 = ajustado al lienzo
    double opacity = 1.0;
    // Recorte de cada lado, en fracción del ancho o alto del video (0 = nada).
    double cropLeft = 0.0;
    double cropTop = 0.0;
    double cropRight = 0.0;
    double cropBottom = 0.0;

    friend bool operator==(const ClipTransform&, const ClipTransform&) = default;
};

inline constexpr double kMinScale = 0.01;
inline constexpr double kMaxScale = 20.0;
inline constexpr double kMaxCrop = 0.95; // entre dos lados opuestos, como mucho

// Volumen de un clip de audio en dB. kMinVolumeDb o menos equivale a silencio.
inline constexpr double kMinVolumeDb = -60.0;
inline constexpr double kMaxVolumeDb = 15.0;

// Ganancia lineal de un volumen en dB (0 si es kMinVolumeDb o menos).
double gainFromDb(double db);

struct Clip {
    ObjectId id = 0;
    ObjectId media = 0;
    ObjectId link = 0;       // clip enlazado de la otra pista; 0 = ninguno
    std::int64_t start = 0;  // primer fotograma en la línea de tiempo
    std::int64_t in = 0;     // desfase dentro del medio
    std::int64_t length = 0; // fotogramas (siempre > 0)
    ClipTransform transform; // solo clips de video
    double volumeDb = 0.0;   // solo clips de audio

    std::int64_t end() const { return start + length; } // primer fotograma excluido
    std::int64_t out() const { return in + length; }    // fin del tramo en el medio

    friend bool operator==(const Clip&, const Clip&) = default;
};

// Transición en el corte entre dos clips seguidos de una pista de video (A termina
// justo donde empieza B). Ocupa [corte − before, corte + after): en ese tramo A sigue
// más allá de su final y B empieza antes de su inicio, con el material que sobra del
// medio (si no hay, se repite su último o su primer fotograma). Ningún clip se mueve,
// así que el audio y las demás pistas siguen sincronizados (como en Premiere).
struct Transition {
    ObjectId id = 0;
    ObjectId from = 0;       // clip saliente (A)
    ObjectId to = 0;         // clip entrante (B)
    std::int64_t before = 0; // fotogramas antes del corte
    std::int64_t after = 0;  // fotogramas después del corte
    TransitionParams params; // params.durationFrames = before + after
    ObjectId sound = 0;      // clip de audio con su efecto de sonido (0 = ninguno)

    std::int64_t length() const { return before + after; }

    friend bool operator==(const Transition&, const Transition&) = default;
};

struct Track {
    TrackKind kind = TrackKind::Video;
    std::vector<Clip> clips;               // ordenados por `start` y sin solaparse
    std::vector<Transition> transitions{}; // solo video; ordenadas por corte
    bool hidden = false;                   // video: no se ve (ni en la vista previa ni al exportar)
    bool muted = false;                    // audio: no suena
    bool locked = false;                   // sus clips no se pueden editar

    friend bool operator==(const Track&, const Track&) = default;
};

// V1 es siempre la primera pista.
inline constexpr std::size_t kVideoTrack = 0;

struct ProjectSettings {
    FrameRate frameRate{60, 1};
    int width = 1920;
    int height = 1080;
    // Mientras la línea de tiempo está vacía, el primer clip con video fija el lienzo
    // y/o los fps. Al elegirlos en "Nuevo proyecto" se desactivan.
    bool adoptCanvas = true;
    bool adoptFrameRate = true;

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

    // Una capa de video visible en un fotograma (ver videoLayersAt).
    struct Layer {
        std::size_t track = 0;
        const Clip* clip = nullptr; // el clip (en una transición, el saliente)
        const MediaItem* media = nullptr;
        std::int64_t mediaFrame = 0; // fotograma del medio que se ve
        // Durante una transición: el clip entrante y en qué punto de ella se está.
        const Transition* transition = nullptr;
        const Clip* clipB = nullptr;
        const MediaItem* mediaB = nullptr;
        std::int64_t mediaFrameB = 0;
        std::int64_t transitionFrame = 0; // 0 … length() − 1
    };

    struct TransitionRef {
        std::size_t track = 0;
        std::size_t index = 0;
    };

    // Final de la línea de tiempo (fin del último clip de cualquier pista).
    std::int64_t length() const;

    // --- Pistas ---
    std::size_t trackCount(TrackKind kind) const;
    // Índice en `tracks` de la pista `number` de ese tipo (0 = V1 o A1).
    std::optional<std::size_t> trackIndex(TrackKind kind, std::size_t number) const;
    // Número de la pista dentro de su tipo (0 = V1 o A1).
    std::size_t trackNumber(std::size_t track) const;
    // "V1", "A2"…
    std::string trackName(std::size_t track) const;
    // Hay algún clip de audio en una pista que no está silenciada.
    bool hasAudibleAudio() const;

    const MediaItem* findMedia(ObjectId id) const;
    std::optional<ClipRef> findClip(ObjectId id) const;
    const Clip* clip(ObjectId id) const;

    // Clip de la pista `track` que se ve en `frame`, o nullptr si hay un hueco.
    const Clip* clipAt(std::size_t track, std::int64_t frame) const;

    // --- Transiciones ---
    std::optional<TransitionRef> findTransition(ObjectId id) const;
    const Transition* transition(ObjectId id) const;
    // Fotograma del corte (donde empieza el clip entrante); -1 si no existe.
    std::int64_t cutOf(const Transition& transition) const;
    // Transición de la pista que cubre `frame`, o nullptr.
    const Transition* transitionAt(std::size_t track, std::int64_t frame) const;
    // Transición en el corte donde empieza (o termina) un clip, o nullptr.
    const Transition* transitionInto(ObjectId clip) const;
    const Transition* transitionOutOf(ObjectId clip) const;

    // Duración del medio en fotogramas del proyecto.
    std::int64_t mediaFrames(const MediaItem& item) const;

    // Instante del medio (µs desde su inicio) que se ve en el fotograma `frame` de
    // la línea de tiempo, si `frame` cae dentro del clip.
    std::int64_t mediaTimeUs(const Clip& clip, std::int64_t frame) const;

    // Fotograma del medio (en sus propios fps) que se ve en `frame`. Fuera del clip
    // (en una transición) sigue el medio, limitado a su primer y último fotograma.
    std::int64_t mediaFrameIndex(const Clip& clip, std::int64_t frame) const;

    // Lo que se ve en `frame`, de abajo (V1) arriba: un clip (o dos, en una
    // transición) por pista de video visible, sin los que apuntan a un medio que no
    // existe.
    std::vector<Layer> videoLayersAt(std::int64_t frame) const;

    friend bool operator==(const Project&, const Project&) = default;
};

} // namespace sharkcut
