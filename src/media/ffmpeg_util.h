#pragma once

// Utilidades internas para trabajar con FFmpeg desde C++: inclusión de cabeceras,
// liberación automática de recursos (RAII) y mensajes de error.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include "core/time.h"

#include <memory>
#include <string>

namespace sharkcut::ff {

struct FormatContextDeleter {
    void operator()(AVFormatContext* p) const { avformat_close_input(&p); }
};
struct CodecContextDeleter {
    void operator()(AVCodecContext* p) const { avcodec_free_context(&p); }
};
struct FrameDeleter {
    void operator()(AVFrame* p) const { av_frame_free(&p); }
};
struct PacketDeleter {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct BufferRefDeleter {
    void operator()(AVBufferRef* p) const { av_buffer_unref(&p); }
};
struct SwsDeleter {
    void operator()(struct SwsContext* p) const { sws_freeContext(p); }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using BufferRefPtr = std::unique_ptr<AVBufferRef, BufferRefDeleter>;
using SwsPtr = std::unique_ptr<struct SwsContext, SwsDeleter>;

// Texto legible de un código de error de FFmpeg.
std::string errorString(int code);

// Lanza MediaError con `what` y el texto del error si `code` es negativo.
void check(int code, const char* what);

// Abre un archivo y lee la información de sus flujos. Lanza MediaError si falla.
FormatContextPtr openInput(const std::string& path);

inline Rational toRational(AVRational r) {
    return {r.num, r.den};
}

} // namespace sharkcut::ff
