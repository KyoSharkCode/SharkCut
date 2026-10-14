#include "media/ffmpeg_util.h"

#include "media/media_error.h"

namespace sharkcut::ff {

std::string errorString(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(code, buffer, sizeof(buffer)) < 0) {
        return "error desconocido (" + std::to_string(code) + ")";
    }
    return buffer;
}

void check(int code, const char* what) {
    if (code < 0) {
        throw MediaError(std::string(what) + ": " + errorString(code));
    }
}

FormatContextPtr openInput(const std::string& path) {
    // FFmpeg espera rutas en UTF-8 (en Windows las convierte internamente).
    AVFormatContext* raw = nullptr;
    const int opened = avformat_open_input(&raw, path.c_str(), nullptr, nullptr);
    if (opened < 0) {
        throw MediaError("No se pudo abrir el archivo: " + errorString(opened));
    }
    FormatContextPtr ctx(raw);
    check(avformat_find_stream_info(ctx.get(), nullptr),
          "No se pudo leer la información del archivo");
    return ctx;
}

} // namespace sharkcut::ff
