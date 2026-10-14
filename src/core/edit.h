#pragma once

// Operaciones de edición de la línea de tiempo.
//
// Cada operación modifica el proyecto recibido y devuelve si cambió algo; si no
// hay nada que hacer, lo deja intacto. No registran deshacer: eso lo hace History,
// que aplica la operación sobre una copia (ver history.h).
//
// Reglas comunes:
// - Los clips enlazados (video + audio del mismo tramo) se editan siempre juntos.
// - Colocar o mover un clip sobrescribe lo que hubiera debajo (como el modo
//   "sobrescribir" de Premiere): los clips tapados se recortan, se parten o se quitan.
//   Si un clip enlazado queda tapado solo en una de sus pistas, deja de estar enlazado.
// - Quitar o mover un clip deja un hueco; no se desplaza el resto (eso es la
//   eliminación con rizo, prevista para la fase 4).
// - Las pistas bloqueadas no cambian: sus clips no se mueven, recortan, dividen ni
//   quitan, y no se puede colocar nada en ellas.

#include "core/project.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sharkcut {

// Añade un medio a la biblioteca y devuelve su nuevo identificador.
ObjectId addMedia(Project& project, MediaItem item);

// Quita un medio de la biblioteca y todos sus clips de la línea de tiempo.
bool removeMedia(Project& project, ObjectId media);

// --- Pistas ---

// Añade una pista encima de las de video (V3 sobre V2…) o debajo de las de audio.
// Devuelve su índice en project.tracks.
std::size_t addTrack(Project& project, TrackKind kind);

// Quita la pista y sus clips. Siempre queda al menos una pista de cada tipo.
bool removeTrack(Project& project, std::size_t track);

bool setTrackHidden(Project& project, std::size_t track, bool hidden); // solo video
bool setTrackMuted(Project& project, std::size_t track, bool muted);   // solo audio
bool setTrackLocked(Project& project, std::size_t track, bool locked);

// --- Clips ---

// Pistas donde se coloca un medio: números dentro de cada tipo (0 = V1 o A1). Las que
// falten se crean.
struct PlaceTarget {
    std::size_t video = 0;
    std::size_t audio = 0;
};

// Coloca el tramo [in, in + length) del medio a partir del fotograma `start`.
// El tramo se limita a la duración del medio. Crea un clip de video y otro de audio
// (enlazados) según lo que tenga el medio. Devuelve el clip de video (o el de audio
// si el medio no tiene video o su pista está bloqueada); 0 si no se colocó nada.
ObjectId placeClip(Project& project, ObjectId media, std::int64_t start, std::int64_t in,
                   std::int64_t length, PlaceTarget target = {});

// Coloca el medio entero a partir de `start`.
ObjectId placeClip(Project& project, ObjectId media, std::int64_t start, PlaceTarget target = {});

// Parte en dos los clips que contienen el fotograma `frame` (sin contar su primer
// fotograma). Con `clips` vacío parte todas las pistas; si no, solo esos clips (y
// sus enlazados).
bool splitAt(Project& project, std::int64_t frame, const std::vector<ObjectId>& clips = {});

// Quita los clips (y sus enlazados) dejando un hueco.
bool removeClips(Project& project, const std::vector<ObjectId>& clips);

// Recorta el inicio del clip para que empiece en `start`, conservando el resto
// del tramo. limitTrimStart() da el valor que se aplicaría: sin pasar del inicio
// del medio, sin pisar el clip anterior y dejando al menos un fotograma.
std::int64_t limitTrimStart(const Project& project, ObjectId clip, std::int64_t start);
bool trimStart(Project& project, ObjectId clip, std::int64_t start);

// Recorta el final del clip para que termine en `end` (primer fotograma excluido).
// limitTrimEnd(): sin pasar del final del medio ni pisar el clip siguiente.
std::int64_t limitTrimEnd(const Project& project, ObjectId clip, std::int64_t end);
bool trimEnd(Project& project, ObjectId clip, std::int64_t end);

// Desplaza los clips (y sus enlazados) `delta` fotogramas y, los de tipo `shiftKind`,
// `trackShift` pistas (+1 = una pista más arriba en video, más abajo en audio),
// sobrescribiendo lo que haya en el destino. Los enlazados del otro tipo se quedan en
// su pista. limitMove() da el desplazamiento aplicable (nada antes del 0) y
// limitTrackShift() el cambio de pista posible (a pistas que existen y no están
// bloqueadas).
std::int64_t limitMove(const Project& project, const std::vector<ObjectId>& clips,
                       std::int64_t delta);
int limitTrackShift(const Project& project, const std::vector<ObjectId>& clips, TrackKind shiftKind,
                    int trackShift);
bool moveClips(Project& project, const std::vector<ObjectId>& clips, std::int64_t delta,
               int trackShift = 0, TrackKind shiftKind = TrackKind::Video);

// Posición, escala, opacidad y recorte de un clip de video (valores fuera de rango
// se ajustan con clampTransform).
ClipTransform clampTransform(ClipTransform transform);
bool setClipTransform(Project& project, ObjectId clip, const ClipTransform& transform);

// Volumen de un clip de audio, en dB (entre kMinVolumeDb y kMaxVolumeDb).
bool setClipVolume(Project& project, ObjectId clip, double volumeDb);

// Separa los clips de sus enlazados: a partir de ahí se editan por separado.
bool unlinkClips(Project& project, const std::vector<ObjectId>& clips);

// --- Transiciones ---
//
// Una transición vive en el corte entre dos clips seguidos de una pista de video. Si
// una edición separa los clips (mover, recortar, quitar…), la transición desaparece; si
// parte el clip saliente, pasa a su parte derecha. Su efecto de sonido es un clip de
// audio normal (se puede mover, cambiar de volumen o quitar).

// Lo que puede durar la transición a cada lado del corte: sin salirse de los clips ni
// pisar la transición del otro extremo de cada uno.
struct TransitionLimits {
    std::int64_t maxBefore = 0;
    std::int64_t maxAfter = 0;
};
TransitionLimits transitionLimits(const Project& project, ObjectId transition);

// Pone una transición en el corte de la pista `track` que está en el fotograma `cut`
// (donde termina un clip y empieza el siguiente), centrada y con la duración
// `params.durationFrames` limitada a lo que cabe. Si ya había una en ese corte, cambia
// sus parámetros y conserva su sonido. Devuelve su id, o 0 si ahí no hay corte (o la
// pista está bloqueada).
ObjectId addTransition(Project& project, std::size_t track, std::int64_t cut,
                       const TransitionParams& params);

// Quita la transición y, con `withSound`, también su efecto de sonido.
bool removeTransition(Project& project, ObjectId transition, bool withSound = true);

// Tipo, intensidad, desenfoque y curva (la duración se cambia con setTransitionLength).
bool setTransitionParams(Project& project, ObjectId transition, const TransitionParams& params);

// Fotogramas antes y después del corte (limitados por transitionLimits; en total, al
// menos 2).
bool setTransitionLength(Project& project, ObjectId transition, std::int64_t before,
                         std::int64_t after);

// Coloca el sonido `soundMedia` con su golpe (`hitFrames` desde su inicio) en el corte
// de la transición, en la primera pista de audio libre en ese tramo (o en una nueva), y
// lo empareja con la transición (quitando el anterior). Devuelve el clip, o 0.
ObjectId placeTransitionSound(Project& project, ObjectId transition, ObjectId soundMedia,
                              std::int64_t hitFrames, double volumeDb = 0.0);

// Los clips indicados más sus enlazados, sin repetir y solo los que existen.
std::vector<ObjectId> withLinked(const Project& project, const std::vector<ObjectId>& clips);

// Comprueba los enlaces: se conservan los recíprocos entre un clip de video y uno de
// audio con el mismo medio, posición y tramo; el resto se deshacen.
void repairLinks(Project& project);

// Formato antiguo (versión 1 del .sharkp, sin enlaces fiables): enlaza el clip de V1
// con el de A1 que comparte medio, posición y tramo.
void linkMatchingClips(Project& project);

} // namespace sharkcut
