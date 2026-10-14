#pragma once

#include <cstdint>
#include <vector>

namespace sharkcut {

// Matriz de color con la que se codificó el video (YUV → RGB).
enum class ColorMatrix {
    Bt601,
    Bt709,
    Bt2020,
};

// Fotograma decodificado en memoria, siempre en formato NV12 de 8 bits:
// un plano Y (luminancia) y un plano UV entrelazado a media resolución.
struct VideoFrame {
    std::int64_t index = 0; // número de fotograma dentro del clip (0 = primero)
    int width = 0;
    int height = 0;

    std::vector<std::uint8_t> y;  // width × height bytes
    std::vector<std::uint8_t> uv; // uvWidth() × 2 × uvHeight() bytes

    ColorMatrix matrix = ColorMatrix::Bt709;
    bool fullRange = false; // false = rango limitado (16–235), lo habitual en video

    int uvWidth() const { return (width + 1) / 2; }
    int uvHeight() const { return (height + 1) / 2; }
};

} // namespace sharkcut
