#pragma once

#include "media/video_frame.h"

#include <array>

namespace sharkcut {

// Matriz 4×4 (por filas) que convierte las muestras YUV normalizadas a 0–1, tal
// como las lee el shader, a RGB:  (R, G, B, 1) = M · (Y, U, V, 1).
// Incluye la expansión de rango limitado (16–235 / 16–240) a completo.
std::array<float, 16> yuvToRgbMatrix(ColorMatrix matrix, bool fullRange);

} // namespace sharkcut
