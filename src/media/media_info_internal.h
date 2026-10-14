#pragma once

// Funciones compartidas entre el analizador y el decodificador.

#include "media/ffmpeg_util.h"
#include "media/media_info.h"

namespace sharkcut::detail {

// Describe el flujo de video `stream` de un archivo ya abierto.
VideoStreamInfo describeVideoStream(const AVFormatContext* format, const AVStream* stream);

} // namespace sharkcut::detail
