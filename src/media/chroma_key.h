#pragma once

#include "media/video_frame.h"

#include <cstdint>
#include <optional>

namespace sharkcut {

// Color de fondo de un fotograma con pantalla verde (o azul, o de cualquier tono): el
// tono saturado que más se repite en los bordes, donde suele estar el fondo. Devuelve
// 0xRRGGBB, o std::nullopt si los bordes no tienen un color dominante (p. ej. grises).
std::optional<std::uint32_t> detectKeyColor(const VideoFrame& frame);

} // namespace sharkcut
