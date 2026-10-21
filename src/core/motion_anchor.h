#pragma once

// Punto de anclaje: un clip (texto, imagen, video pequeño) sigue algo que se mueve en
// otro clip de video. El seguimiento (media/motion_tracker.h) da dónde está la zona en
// cada fotograma del video seguido; aquí se pasa al lienzo (con la colocación de ese
// clip en cada momento) y se convierte en keyframes de posición del clip anclado.

#include "core/project.h"

#include <cstdint>
#include <vector>

namespace sharkcut {

// Punto (x, y en píxeles del lienzo, desde la esquina superior izquierda).
struct CanvasPoint {
    double x = 0.0;
    double y = 0.0;
};

// Dónde cae en el lienzo el punto (u, v) del video (fracciones de su ancho y alto, 0–1)
// de un clip con esa colocación: ajuste al lienzo sin deformar, escala, giro y posición,
// como lo dibuja el compositor.
CanvasPoint mediaPointOnCanvas(const ProjectSettings& settings, int mediaWidth, int mediaHeight,
                               const ClipTransform& transform, double u, double v);

// Al revés: qué punto del video (fracciones; fuera de 0–1 si cae fuera) se ve en el
// punto (x, y) del lienzo.
CanvasPoint canvasPointOnMedia(const ProjectSettings& settings, int mediaWidth, int mediaHeight,
                               const ClipTransform& transform, double x, double y);

// La zona seguida en un fotograma de la línea de tiempo: su centro en el video del clip
// seguido (fracciones, 0–1).
struct AnchorSample {
    std::int64_t frame = 0;
    double u = 0.0;
    double v = 0.0;
};

// Diferencia (píxeles del lienzo) que se deja al quitar keyframes que sobran del recorrido.
inline constexpr double kAnchorTolerance = 1.0;

// Ancla `clip` a `target` (clips de video distintos; `clip` en una pista sin bloquear):
// a partir del primer fotograma de `samples`, la posición del clip se mueve lo mismo que
// la zona seguida en el lienzo, con keyframes lineales (uno por fotograma, quitando los que
// sobran; sustituyen a los de posición que hubiera en ese tramo, los de fuera se quedan). Los
// fotogramas fuera del clip anclado se ignoran. Devuelve si cambió algo.
bool anchorClip(Project& project, ObjectId clip, ObjectId target,
                const std::vector<AnchorSample>& samples);

} // namespace sharkcut
