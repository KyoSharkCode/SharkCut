#include "media/proxy.h"

#include "core/project_file.h"
#include "media/ffmpeg_util.h"
#include "media/media_error.h"
#include "media/video_decoder.h"
#include "media/waveform.h"

extern "C" {
#include <libavutil/opt.h>
}

#include <algorithm>
#include <filesystem>
#include <vector>

namespace sharkcut {

namespace {

// Por hardware primero (NVIDIA, Intel, AMD); x264 por software como respaldo.
const char* const kEncoders[] = {"h264_nvenc", "h264_qsv", "h264_amf", "libx264"};

void configure(AVCodecContext* enc, const std::string& name) {
    if (name == "h264_nvenc") {
        av_opt_set(enc->priv_data, "preset", "p2", 0); // rápido: es solo para editar
        av_opt_set(enc->priv_data, "rc", "vbr", 0);
        av_opt_set(enc->priv_data, "cq", "26", 0);
    } else if (name == "h264_qsv") {
        av_opt_set(enc->priv_data, "preset", "veryfast", 0);
    } else if (name == "h264_amf") {
        av_opt_set(enc->priv_data, "quality", "speed", 0);
    } else if (name == "libx264") {
        av_opt_set(enc->priv_data, "preset", "veryfast", 0);
        av_opt_set(enc->priv_data, "crf", "24", 0);
    }
}

struct ProxyOutput {
    ff::FormatContextPtr out; // de salida: se libera a mano (ver ~ProxyOutput)
    ff::CodecContextPtr enc;
    AVStream* stream = nullptr;
    ff::FramePtr frame{av_frame_alloc()};
    ff::PacketPtr packet{av_packet_alloc()};
    ff::SwsPtr sws;
    bool headerWritten = false;

    ~ProxyOutput() {
        if (AVFormatContext* ctx = out.release()) {
            if (ctx->pb && !(ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&ctx->pb);
            }
            avformat_free_context(ctx);
        }
    }

    void drain() {
        for (;;) {
            const int r = avcodec_receive_packet(enc.get(), packet.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                return;
            }
            ff::check(r, "Error al codificar el proxy");
            av_packet_rescale_ts(packet.get(), enc->time_base, stream->time_base);
            packet->stream_index = stream->index;
            ff::check(av_interleaved_write_frame(out.get(), packet.get()),
                      "Error al escribir el proxy");
        }
    }
};

} // namespace

std::string proxyFileName(const std::string& mediaPath) {
    std::string name = waveformCacheName(mediaPath); // mismo criterio: ruta, tamaño y fecha
    if (name.empty()) {
        return {};
    }
    return name.substr(0, name.find('.')) + ".mp4";
}

void writeProxy(const std::string& source, const std::string& target,
                const std::function<void(std::int64_t, std::int64_t)>& progress,
                const std::atomic<bool>* cancel) {
    VideoDecoder decoder(source, HwAccel::Auto);
    const VideoStreamInfo& info = decoder.info();
    // H.264 no guarda transparencia: un proxy perdería el alfa (y el original ya suele
    // ser ligero, p. ej. una intro animada).
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(av_get_pix_fmt(info.pixelFormat.c_str()));
    if (desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA)) {
        throw MediaError("Los videos con transparencia no usan proxy");
    }
    const int height = std::min(kProxyHeight, info.height) & ~1;
    const int width = std::max(2, static_cast<int>(static_cast<std::int64_t>(info.width) * height /
                                                   std::max(1, info.height)) &
                                      ~1);

    const std::filesystem::path finalPath = pathFromUtf8(target);
    std::filesystem::path tempPath = finalPath;
    tempPath += ".tmp";
    const std::string tempUtf8 = utf8FromPath(tempPath);
    std::filesystem::create_directories(finalPath.parent_path());
    auto removeTemp = [&tempPath] {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
    };

    bool cancelled = false;
    try {
        ProxyOutput o;
        AVFormatContext* raw = nullptr;
        ff::check(avformat_alloc_output_context2(&raw, nullptr, "mp4", tempUtf8.c_str()),
                  "No se pudo crear el proxy");
        o.out.reset(raw);

        for (const char* name : kEncoders) {
            const AVCodec* codec = avcodec_find_encoder_by_name(name);
            if (!codec) {
                continue;
            }
            ff::CodecContextPtr enc(avcodec_alloc_context3(codec));
            enc->width = width;
            enc->height = height;
            // Base de tiempo = un fotograma: el pts de cada fotograma es su índice.
            enc->time_base = {static_cast<int>(info.frameRate.den),
                              static_cast<int>(info.frameRate.num)};
            enc->framerate = {static_cast<int>(info.frameRate.num),
                              static_cast<int>(info.frameRate.den)};
            enc->pix_fmt = std::string(name) == "h264_qsv" ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
            enc->gop_size = kProxyGop;
            enc->max_b_frames = 0; // buscar y retroceder sin esperar fotogramas futuros
            enc->bit_rate = 3'000'000;
            enc->colorspace = AVCOL_SPC_BT709;
            enc->color_primaries = AVCOL_PRI_BT709;
            enc->color_trc = AVCOL_TRC_BT709;
            enc->color_range = AVCOL_RANGE_MPEG;
            if (o.out->oformat->flags & AVFMT_GLOBALHEADER) {
                enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }
            configure(enc.get(), name);
            if (avcodec_open2(enc.get(), codec, nullptr) >= 0) {
                o.enc = std::move(enc);
                break;
            }
        }
        if (!o.enc) {
            throw MediaError("No hay ningún codificador H.264 para crear el proxy");
        }
        o.stream = avformat_new_stream(o.out.get(), nullptr);
        ff::check(avcodec_parameters_from_context(o.stream->codecpar, o.enc.get()),
                  "No se pudo configurar el proxy");
        o.stream->time_base = o.enc->time_base;
        o.stream->avg_frame_rate = o.enc->framerate;
        ff::check(avio_open(&o.out->pb, tempUtf8.c_str(), AVIO_FLAG_WRITE),
                  "No se pudo crear el archivo del proxy");
        ff::check(avformat_write_header(o.out.get(), nullptr),
                  "No se pudo escribir la cabecera del proxy");
        o.headerWritten = true;

        o.frame->format = o.enc->pix_fmt;
        o.frame->width = width;
        o.frame->height = height;
        ff::check(av_frame_get_buffer(o.frame.get(), 0), "Sin memoria para el proxy");

        const std::int64_t total = std::max<std::int64_t>(1, info.frameCount);
        std::int64_t done = 0;
        std::int64_t lastIndex = -1;
        while (auto decoded = decoder.nextFrame()) {
            if (cancel && cancel->load()) {
                cancelled = true;
                break;
            }
            if (decoded->index <= lastIndex) {
                continue; // índices siempre crecientes (el MP4 no admite repetidos)
            }
            lastIndex = decoded->index;
            // NV12 (con su matriz y rango) → el formato del codificador, a menor tamaño.
            o.sws.reset(sws_getCachedContext(o.sws.release(), decoded->width, decoded->height,
                                             AV_PIX_FMT_NV12, width, height, o.enc->pix_fmt,
                                             SWS_BILINEAR, nullptr, nullptr, nullptr));
            if (!o.sws) {
                throw MediaError("No se pudo preparar la conversión del proxy");
            }
            const int* coefficients = sws_getCoefficients(
                decoded->matrix == ColorMatrix::Bt601 ? SWS_CS_ITU601 : SWS_CS_ITU709);
            sws_setColorspaceDetails(o.sws.get(), coefficients, decoded->fullRange ? 1 : 0,
                                     sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1 << 16, 1 << 16);
            ff::check(av_frame_make_writable(o.frame.get()), "Sin memoria para el proxy");
            const std::uint8_t* src[4] = {decoded->y.data(), decoded->uv.data(), nullptr, nullptr};
            const int srcStride[4] = {decoded->width, decoded->uvWidth() * 2, 0, 0};
            sws_scale(o.sws.get(), src, srcStride, 0, decoded->height, o.frame->data,
                      o.frame->linesize);
            o.frame->pts = decoded->index;
            ff::check(avcodec_send_frame(o.enc.get(), o.frame.get()),
                      "Error al codificar el proxy");
            o.drain();
            ++done;
            if (progress && (done % 30 == 0)) {
                progress(std::min(done, total), total);
            }
        }
        if (!cancelled) {
            avcodec_send_frame(o.enc.get(), nullptr);
            o.drain();
            ff::check(av_write_trailer(o.out.get()), "No se pudo cerrar el proxy");
            if (progress) {
                progress(total, total);
            }
        }
    } catch (...) {
        removeTemp(); // `o` ya cerró el archivo
        throw;
    }
    if (cancelled) {
        removeTemp();
        throw MediaError("Proxy cancelado");
    }
    std::error_code error;
    std::filesystem::rename(tempPath, finalPath, error);
    if (error) {
        std::filesystem::remove(tempPath, error);
        throw MediaError("No se pudo guardar el proxy");
    }
}

} // namespace sharkcut
