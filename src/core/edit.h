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
// - Quitar o mover un clip deja un hueco; no se desplaza el resto (eso es la
//   eliminación con rizo, prevista para la fase 4).

#include "core/project.h"

#include <cstdint>
#include <vector>

namespace sharkcut {

// Añade un medio a la biblioteca y devuelve su nuevo identificador.
ObjectId addMedia(Project& project, MediaItem item);

// Quita un medio de la biblioteca y todos sus clips de la línea de tiempo.
bool removeMedia(Project& project, ObjectId media);

// Coloca el tramo [in, in + length) del medio a partir del fotograma `start`.
// El tramo se limita a la duración del medio. Crea un clip de video y otro de audio
// (enlazados) según lo que tenga el medio. Devuelve el clip de video (o el de audio
// si el medio no tiene video); 0 si no se colocó nada.
ObjectId placeClip(Project& project, ObjectId media, std::int64_t start, std::int64_t in,
                   std::int64_t length);

// Coloca el medio entero a partir de `start`.
ObjectId placeClip(Project& project, ObjectId media, std::int64_t start);

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

// Desplaza los clips (y sus enlazados) `delta` fotogramas, sobrescribiendo lo que
// haya en el destino. limitMove() da el desplazamiento aplicable (nada antes del 0).
std::int64_t limitMove(const Project& project, const std::vector<ObjectId>& clips,
                       std::int64_t delta);
bool moveClips(Project& project, const std::vector<ObjectId>& clips, std::int64_t delta);

// Los clips indicados más sus enlazados, sin repetir y solo los que existen.
std::vector<ObjectId> withLinked(const Project& project, const std::vector<ObjectId>& clips);

// Recalcula los enlaces: un clip de video y uno de audio quedan enlazados si
// comparten medio, posición y tramo. Lo usan las operaciones y la carga de archivos.
void relinkClips(Project& project);

} // namespace sharkcut
