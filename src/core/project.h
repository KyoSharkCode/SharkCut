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

#include "core/easing.h"
#include "core/time.h"
#include "core/transition.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
    // Imagen fija (PNG, JPG…): siempre se ve el mismo fotograma y sus clips se alargan
    // sin límite, hacia los dos lados. `durationUs` es la duración al ponerla en la línea
    // de tiempo (la de las imágenes nuevas se elige en la app).
    bool still = false;

    friend bool operator==(const MediaItem&, const MediaItem&) = default;
};

enum class TrackKind { Video, Audio };

// Pantalla verde (croma): vuelve transparente el color de fondo del clip para ver lo
// que hay debajo. Se compara el tono relativo al brillo (Cb/Cr de BT.709 entre la
// luminancia), así las zonas más iluminadas o en sombra del fondo también se quitan. Las
// distancias son relativas a la saturación del color quitado: 1 = tan lejos como un gris,
// que nunca se quita.
struct ChromaKey {
    bool enabled = false;
    std::uint32_t color = 0x00FF00; // color que se quita, 0xRRGGBB
    double similarity = 0.40;       // hasta esta distancia del color: transparente
    double smoothness = 0.10;       // franja de paso a opaco a partir de ahí
    double spill = 0.40;            // franja en la que se quita el reflejo del color

    friend bool operator==(const ChromaKey&, const ChromaKey&) = default;
};

inline constexpr double kMaxChromaDistance = 1.0;

// Efectos de imagen de un clip (pestaña Efectos). Cada uno se añade y se quita (`…On`) y
// conserva sus valores. Las medidas en píxeles son del lienzo de la secuencia (al exportar
// a otro tamaño se escalan).
struct ClipEffects {
    bool grayscaleOn = false;
    double grayscale = 1.0; // 0 = color, 1 = blanco y negro
    bool blurOn = false;
    double blur = 12.0; // radio del desenfoque
    bool cornersOn = false;
    double corners = 40.0; // radio de las esquinas
    // Sombra o resplandor detrás del clip; sigue su forma (también su transparencia).
    bool shadowOn = false;
    std::uint32_t shadowColor = 0x000000;
    double shadowOpacity = 0.6;
    double shadowSoftness = 30.0; // difuminado
    double shadowX = 0.0;         // desplazamiento
    double shadowY = 12.0;
    double shadowSpread = 0.0; // cuánto crece la forma antes de difuminarse

    friend bool operator==(const ClipEffects&, const ClipEffects&) = default;
};

inline constexpr double kMaxBlur = 200.0;
inline constexpr double kMaxEffectPixels = 2000.0;

// Colocación de un clip de video en el lienzo. Sin transformar (los valores por
// defecto), el video se ajusta al lienzo sin deformarse y centrado.
struct ClipTransform {
    double x = 0.0;        // desplazamiento del centro en píxeles del lienzo (+x derecha)
    double y = 0.0;        // (+y abajo)
    double scale = 1.0;    // 1 = ajustado al lienzo
    double rotation = 0.0; // grados, en el sentido de las agujas del reloj (alrededor del centro)
    double opacity = 1.0;
    // Recorte de cada lado, en fracción del ancho o alto del video (0 = nada).
    double cropLeft = 0.0;
    double cropTop = 0.0;
    double cropRight = 0.0;
    double cropBottom = 0.0;
    ChromaKey chromaKey;   // efecto del clip; Restablecer transformación no lo toca
    ClipEffects effects{}; // ídem

    friend bool operator==(const ClipTransform&, const ClipTransform&) = default;
};

inline constexpr double kMinScale = 0.01;
inline constexpr double kMaxScale = 20.0;
inline constexpr double kMaxRotation = 3600.0; // grados (10 vueltas a cada lado)
inline constexpr double kMaxCrop = 0.95;       // entre dos lados opuestos, como mucho

// --- Animación (keyframes) ----------------------------------------------------------

// Propiedades de un clip de video que se pueden animar.
enum class AnimProperty {
    X,
    Y,
    Scale,
    Opacity,
    CropLeft,
    CropTop,
    CropRight,
    CropBottom,
    Rotation,
    // Valores de los efectos (ClipEffects)
    Grayscale,
    Blur,
    Corners,
    ShadowOpacity,
    ShadowSoftness,
    ShadowSpread,
    ShadowX,
    ShadowY,
};
inline constexpr std::size_t kAnimPropertyCount = 17;
// Identificadores estables (archivo de proyecto, interfaz), en el orden del enum.
inline constexpr std::array<std::string_view, kAnimPropertyCount> kAnimPropertyIds{
    "x",       "y",         "scale",         "opacity",        "cropLeft",
    "cropTop", "cropRight", "cropBottom",    "rotation",       "grayscale",
    "blur",    "corners",   "shadowOpacity", "shadowSoftness", "shadowSpread",
    "shadowX", "shadowY"};
std::optional<AnimProperty> animPropertyFromId(std::string_view id);
// El campo de la transformación que anima la propiedad.
double& transformField(ClipTransform& transform, AnimProperty property);
double transformField(const ClipTransform& transform, AnimProperty property);

// Un valor en un momento. `frame` es la posición en el medio (en fotogramas del proyecto
// desde el inicio del medio, como Clip::in): así la animación sigue pegada al contenido al
// recortar el clip o al dividirlo.
struct Keyframe {
    std::int64_t frame = 0;
    double value = 0.0;
    Easing easing = Easing::Linear; // curva hasta el siguiente

    friend bool operator==(const Keyframe&, const Keyframe&) = default;
};

// Keyframes de cada propiedad (ordenados por fotograma). Una propiedad con keyframes está
// animada: su valor fijo en ClipTransform no se usa.
struct ClipAnimation {
    std::array<std::vector<Keyframe>, kAnimPropertyCount> keys{};

    bool animated(AnimProperty p) const { return !keys[static_cast<std::size_t>(p)].empty(); }
    bool empty() const;
    std::vector<Keyframe>& of(AnimProperty p) { return keys[static_cast<std::size_t>(p)]; }
    const std::vector<Keyframe>& of(AnimProperty p) const {
        return keys[static_cast<std::size_t>(p)];
    }

    friend bool operator==(const ClipAnimation&, const ClipAnimation&) = default;
};

// Valor de una lista de keyframes en `mediaFrame`: antes del primero, el primero; después
// del último, el último; entre dos, con la curva del primero.
double keyframeValue(const std::vector<Keyframe>& keys, double mediaFrame);

// Volumen de un clip de audio en dB. kMinVolumeDb o menos equivale a silencio.
inline constexpr double kMinVolumeDb = -60.0;
inline constexpr double kMaxVolumeDb = 15.0;

// Ganancia lineal de un volumen en dB (0 si es kMinVolumeDb o menos).
double gainFromDb(double db);

struct Clip {
    ObjectId id = 0;
    ObjectId media = 0;
    // Clip enlazado de la otra pista (0 = ninguno): el video y el audio de un medio, con
    // el mismo tramo, o una transición-clip y su efecto de sonido (cada uno con el suyo).
    // Los enlazados se mueven, se copian y se quitan juntos.
    ObjectId link = 0;
    std::int64_t start = 0;    // primer fotograma en la línea de tiempo
    std::int64_t in = 0;       // desfase dentro del medio
    std::int64_t length = 0;   // fotogramas (siempre > 0)
    ClipTransform transform;   // solo clips de video
    ClipAnimation animation{}; // solo clips de video
    double volumeDb = 0.0;     // solo clips de audio
    // Transición como clip (pistas de video, sin medio: media = 0): en su tramo se aplica
    // a todo lo que hay debajo (como una capa de ajuste). Su efecto dura
    // effect->durationFrames y avanza con `in`, como un medio: al cortarla o recortarla,
    // cada trozo sigue donde iba.
    std::optional<TransitionParams> effect{};

    std::int64_t end() const { return start + length; } // primer fotograma excluido
    std::int64_t out() const { return in + length; }    // fin del tramo en el medio
    // Posición en el medio de un fotograma de la línea de tiempo (la de los keyframes).
    std::int64_t mediaFrameAt(std::int64_t frame) const { return in + (frame - start); }
    bool isEffect() const { return effect.has_value(); }

    friend bool operator==(const Clip&, const Clip&) = default;
};

// Transición en el corte entre dos clips seguidos de una pista de video (A termina
// justo donde empieza B). Ocupa [corte − before, corte + after): en ese tramo A sigue
// más allá de su final y B empieza antes de su inicio, con el material que sobra del
// medio (si no hay, se repite su último o su primer fotograma). Ningún clip se mueve,
// así que el audio y las demás pistas siguen sincronizados.
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

// Los clips de una pista tal como se leen: dos seguidos del mismo medio y sin salto en él
// (las dos mitades de un corte, con el mismo efecto y volumen) cuentan como uno, con el id
// del primero. Dividir un clip no cambia el resultado (salvo los ids).
std::vector<Clip> continuousClips(const Track& track);

// Marcador de la secuencia (tecla M): un punto con nombre en la regla, para volver a él
// o recordar algo ("aquí empieza la partida").
struct Marker {
    ObjectId id = 0;
    std::int64_t frame = 0;
    std::string name;

    friend bool operator==(const Marker&, const Marker&) = default;
};

// --- Subtítulos -------------------------------------------------------------------
//
// Van en pistas propias (S1, S2…), aparte de las de video y audio, y se dibujan encima
// de todo. Cada subtítulo es un texto con su tramo; puede guardar el momento de cada
// palabra (de la transcripción automática) para volver a dividirlo con otras medidas.

// Aspecto de un subtítulo. Las medidas en píxeles son para un lienzo de 1080 de lado
// corto (1920×1080 o 1080×1920); con otros tamaños se escalan.
struct CaptionStyle {
    std::string font = "Arial";
    double size = 64.0; // alto de letra
    bool bold = true;
    bool italic = false;
    bool uppercase = false;
    std::uint32_t color = 0xFFFFFF;        // 0xRRGGBB
    std::uint32_t outlineColor = 0x000000; // contorno
    double outline = 6.0;                  // grosor del contorno (0 = sin contorno)
    std::uint32_t shadowColor = 0x000000;
    double shadow = 0.0;               // opacidad de la sombra (0 = sin sombra)
    std::uint32_t boxColor = 0x000000; // caja de fondo
    double box = 0.0;                  // opacidad de la caja (0 = sin caja)
    double x = 0.5;                    // centro del texto, en fracción del lienzo
    double y = 0.82;
    double maxWidth = 0.86; // ancho máximo del texto (fracción); las líneas se parten

    friend bool operator==(const CaptionStyle&, const CaptionStyle&) = default;
};

// Palabra de un subtítulo: dónde empieza y acaba, en fotogramas desde el inicio del
// subtítulo.
struct CaptionWord {
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::string text;

    friend bool operator==(const CaptionWord&, const CaptionWord&) = default;
};

struct Caption {
    ObjectId id = 0;
    std::int64_t start = 0;
    std::int64_t length = 0;           // fotogramas (siempre > 0)
    std::string text;                  // con '\n' entre líneas
    std::optional<CaptionStyle> style; // propio; si no, el de la pista
    std::vector<CaptionWord> words{};  // vacío si se escribió a mano o se cambió el texto

    std::int64_t end() const { return start + length; }

    friend bool operator==(const Caption&, const Caption&) = default;
};

struct CaptionTrack {
    std::vector<Caption> captions; // ordenados por `start` y sin solaparse
    CaptionStyle style{};
    bool hidden = false;
    bool locked = false;

    friend bool operator==(const CaptionTrack&, const CaptionTrack&) = default;
};

// La transformación del clip en el fotograma `frame` de la línea de tiempo, con sus
// propiedades animadas ya calculadas (y dentro de sus límites).
ClipTransform transformAt(const Clip& clip, std::int64_t frame);

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
    std::vector<Marker> markers{};             // ordenados por fotograma
    std::vector<CaptionTrack> captionTracks{}; // S1, S2… (se dibujan en ese orden)
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
        // Transición-clip (clip->isEffect(), sin medio): se aplica a las capas de debajo,
        // en este fotograma de su efecto.
        std::int64_t effectFrame = 0;
    };

    struct TransitionRef {
        std::size_t track = 0;
        std::size_t index = 0;
    };

    // Final de la línea de tiempo (fin del último clip o subtítulo de cualquier pista).
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
    // El de `frame` o, si cae en un hueco, el más cercano a no más de `maxDistance`
    // fotogramas (para clips que en pantalla miden menos que el ratón); nullptr si no hay.
    const Clip* nearestClip(std::size_t track, std::int64_t frame, std::int64_t maxDistance) const;

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

    // Duración del medio en fotogramas del proyecto (de una imagen fija, la que tiene al
    // ponerla en la línea de tiempo: sus clips no tienen límite).
    std::int64_t mediaFrames(const MediaItem& item) const;

    // Instante del medio (µs desde su inicio) que se ve en el fotograma `frame` de
    // la línea de tiempo, si `frame` cae dentro del clip.
    std::int64_t mediaTimeUs(const Clip& clip, std::int64_t frame) const;

    // Fotograma del medio (en sus propios fps) que se ve en `frame`. Fuera del clip
    // (en una transición) sigue el medio, limitado a su primer y último fotograma. De
    // una imagen fija, siempre 0.
    std::int64_t mediaFrameIndex(const Clip& clip, std::int64_t frame) const;

    // --- Subtítulos ---
    struct CaptionRef {
        std::size_t track = 0;
        std::size_t index = 0;
    };
    std::optional<CaptionRef> findCaption(ObjectId id) const;
    const Caption* caption(ObjectId id) const;
    // Subtítulo de la pista que se ve en `frame`, o nullptr.
    const Caption* captionAt(std::size_t track, std::int64_t frame) const;
    // Estilo con el que se dibuja (el propio o el de su pista).
    const CaptionStyle& styleOf(std::size_t track, const Caption& caption) const;

    // Lo que se ve en `frame`, de abajo (V1) arriba: un clip (o dos, en una
    // transición) por pista de video visible, sin los que apuntan a un medio que no
    // existe.
    std::vector<Layer> videoLayersAt(std::int64_t frame) const;

    friend bool operator==(const Project&, const Project&) = default;
};

} // namespace sharkcut
