#include "media/video_decoder.h"

#include "media/ffmpeg_util.h"
#include "media/media_error.h"
#include "media/media_info_internal.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace sharkcut {

namespace {

// Tipos de aceleración por hardware a probar, en orden de preferencia.
std::vector<AVHWDeviceType> preferredHwDevices() {
#ifdef _WIN32
    // D3D11VA usa NVDEC en tarjetas NVIDIA (y el equivalente en AMD/Intel), y
    // entrega texturas Direct3D 11, la misma API que usa el visor.
    return {AV_HWDEVICE_TYPE_D3D11VA};
#else
    return {};
#endif
}

ColorMatrix colorMatrixOf(const AVFrame* frame) {
    switch (frame->colorspace) {
    case AVCOL_SPC_BT709:
        return ColorMatrix::Bt709;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_FCC:
    case AVCOL_SPC_SMPTE240M:
        return ColorMatrix::Bt601;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return ColorMatrix::Bt2020;
    default:
        // Sin especificar: convención habitual según la resolución.
        return frame->height >= 720 ? ColorMatrix::Bt709 : ColorMatrix::Bt601;
    }
}

bool isFullRange(const AVFrame* frame) {
    if (frame->color_range == AVCOL_RANGE_JPEG) {
        return true;
    }
    const auto format = static_cast<AVPixelFormat>(frame->format);
    return format == AV_PIX_FMT_YUVJ420P || format == AV_PIX_FMT_YUVJ422P ||
           format == AV_PIX_FMT_YUVJ444P;
}

void copyPlane(std::vector<std::uint8_t>& dst, const std::uint8_t* src, int srcStride, int rowBytes,
               int rows) {
    dst.resize(static_cast<std::size_t>(rowBytes) * static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        std::memcpy(dst.data() + static_cast<std::size_t>(row) * rowBytes,
                    src + static_cast<std::ptrdiff_t>(row) * srcStride,
                    static_cast<std::size_t>(rowBytes));
    }
}

} // namespace

struct VideoDecoder::Impl {
    ff::FormatContextPtr format;
    AVStream* stream = nullptr;
    ff::CodecContextPtr codec;
    ff::BufferRefPtr hwDevice;
    AVPixelFormat hwPixelFormat = AV_PIX_FMT_NONE;
    std::string hwName;
    bool lastFrameWasHw = false;

    VideoStreamInfo info;
    std::int64_t startPts = 0;

    ff::PacketPtr packet{av_packet_alloc()};
    ff::FramePtr frame{av_frame_alloc()};
    ff::FramePtr transferFrame{av_frame_alloc()};
    ff::FramePtr lastSkipped{av_frame_alloc()};
    ff::SwsPtr sws;

    bool demuxFinished = false; // ya se envió al decodificador el paquete de vaciado
    bool havePending = false;   // `frame` contiene un fotograma aún no entregado
    bool haveLastSkipped = false;
    std::int64_t skipUntil = 0; // se descartan los fotogramas anteriores a este índice
    std::int64_t keepFrom = -1; // …salvo desde este (se guardan en `kept`)
    std::vector<VideoFrame> kept;
    std::int64_t lastIndex = -1;

    // Imagen fija: se decodifica al abrirla y se entrega como fotograma 0 tras cada salto.
    std::optional<VideoFrame> stillFrame;
    bool stillPending = false;

    // Hasta cuántos fotogramas por delante se sigue decodificando en vez de volver al
    // fotograma clave (en un VOD de OBS hay uno cada 2 s, 120 fotogramas a 60 fps).
    static constexpr std::int64_t kForwardSkip = 90;

    static AVPixelFormat chooseFormat(AVCodecContext* ctx, const AVPixelFormat* formats) {
        const auto* self = static_cast<const Impl*>(ctx->opaque);
        for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
            if (*f == self->hwPixelFormat) {
                return *f;
            }
        }
        // El hardware no admite este video: primer formato por software.
        for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
            const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(*f);
            if (desc && !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) {
                return *f;
            }
        }
        return AV_PIX_FMT_NONE;
    }

    void setupHardware(const AVCodec* decoder) {
        for (const AVHWDeviceType type : preferredHwDevices()) {
            for (int i = 0;; ++i) {
                const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
                if (!config) {
                    break;
                }
                if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) ||
                    config->device_type != type) {
                    continue;
                }
                AVBufferRef* device = nullptr;
                if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0) {
                    break; // Este tipo no está disponible en el equipo: probar el siguiente.
                }
                hwDevice.reset(device);
                hwPixelFormat = config->pix_fmt;
                hwName = av_hwdevice_get_type_name(type);
                codec->hw_device_ctx = av_buffer_ref(device);
                return;
            }
        }
    }

    // Abre el archivo y elige su flujo de video. Devuelve el decodificador que propone
    // FFmpeg para él.
    const AVCodec* openStream(const std::string& path) {
        format = ff::openInput(path);

        const AVCodec* decoder = nullptr;
        const int index =
            av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        if (index < 0 || !decoder) {
            throw MediaError("El archivo no tiene un flujo de video compatible");
        }
        stream = format->streams[index];
        info = detail::describeVideoStream(format.get(), stream);
        startPts = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;

        // Solo se leen los paquetes del flujo de video.
        for (unsigned i = 0; i < format->nb_streams; ++i) {
            if (static_cast<int>(i) != index) {
                format->streams[i]->discard = AVDISCARD_ALL;
            }
        }
        return decoder;
    }

    void openCodec(const AVCodec* decoder, bool hardware) {
        codec.reset(avcodec_alloc_context3(decoder));
        if (!codec) {
            throw MediaError("No se pudo crear el decodificador");
        }
        ff::check(avcodec_parameters_to_context(codec.get(), stream->codecpar),
                  "No se pudo configurar el decodificador");
        codec->pkt_timebase = stream->time_base;
        codec->opaque = this;
        codec->get_format = &Impl::chooseFormat;
        codec->thread_count = 0; // automático

        if (hardware) {
            setupHardware(decoder);
        }
        ff::check(avcodec_open2(codec.get(), decoder, nullptr),
                  "No se pudo abrir el decodificador");
    }

    // Vuelve al estado de recién creado (tras un intento fallido de abrir).
    void resetDecoder() {
        codec.reset();
        hwDevice.reset();
        hwPixelFormat = AV_PIX_FMT_NONE;
        hwName.clear();
        format.reset();
        stream = nullptr;
        av_packet_unref(packet.get());
        av_frame_unref(frame.get());
        demuxFinished = false;
        havePending = false;
    }

    // AV1 por la GPU: el decodificador propio de FFmpeg solo funciona con aceleración
    // por hardware (NVDEC en las NVIDIA RTX 30 y posteriores). Que la tarjeta lo admita
    // solo se sabe al decodificar, así que se prueba con el primer fotograma (que queda
    // pendiente para nextFrame()). Devuelve false si no se puede.
    bool openAv1Hardware() {
        const AVCodec* native = avcodec_find_decoder_by_name("av1");
        if (!native) {
            return false;
        }
        try {
            openCodec(native, true);
        } catch (const MediaError&) {
            return false;
        }
        if (!hwDevice) {
            return false;
        }
        // Como en receiveRaw(), pero un error corta la prueba (sin seguir leyendo) y
        // con un límite de paquetes.
        for (int packets = 0; packets < 120;) {
            const int received = avcodec_receive_frame(codec.get(), frame.get());
            if (received == 0) {
                havePending = true;
                return true;
            }
            if (received != AVERROR(EAGAIN) || av_read_frame(format.get(), packet.get()) < 0) {
                return false;
            }
            if (packet->stream_index == stream->index) {
                ++packets;
                const int sent = avcodec_send_packet(codec.get(), packet.get());
                if (sent < 0) {
                    av_packet_unref(packet.get());
                    return false;
                }
            }
            av_packet_unref(packet.get());
        }
        return false;
    }

    // AV1 por software: dav1d (rápido) o, si no está, libaom.
    static const AVCodec* av1SoftwareDecoder() {
        for (const char* name : {"libdav1d", "libaom-av1"}) {
            if (const AVCodec* decoder = avcodec_find_decoder_by_name(name)) {
                return decoder;
            }
        }
        throw MediaError("Esta compilación de FFmpeg no puede decodificar AV1 por software "
                         "(falta dav1d)");
    }

    void open(const std::string& path, HwAccel hwAccel) {
        const AVCodec* decoder = openStream(path);
        if (stream->codecpar->codec_id == AV_CODEC_ID_AV1) {
            if (hwAccel == HwAccel::Auto) {
                if (openAv1Hardware()) {
                    return;
                }
                resetDecoder();
                openStream(path); // de vuelta al principio del archivo
            }
            openCodec(av1SoftwareDecoder(), false);
            return;
        }
        if (info.still) {
            openStill(decoder);
            return;
        }
        openCodec(decoder, hwAccel == HwAccel::Auto);
    }

    // Una imagen se lee entera al abrirla: sus demuxers no permiten saltar (ni volver al
    // principio), así que después no se toca el archivo.
    void openStill(const AVCodec* decoder) {
        openCodec(decoder, false);
        if (!receiveRaw()) {
            throw MediaError("No se pudo leer la imagen");
        }
        stillFrame = convert(frame.get());
        stillFrame->index = 0;
        av_frame_unref(frame.get());
        stillPending = true;
        // Ya no hace falta nada más del archivo.
        codec.reset();
        format.reset();
        stream = nullptr;
    }

    // Deja en `frame` el siguiente fotograma decodificado (en orden de presentación).
    // Devuelve false al final del flujo.
    bool receiveRaw() {
        if (havePending) {
            havePending = false;
            return true;
        }
        for (;;) {
            const int received = avcodec_receive_frame(codec.get(), frame.get());
            if (received == 0) {
                return true;
            }
            if (received == AVERROR_EOF) {
                return false;
            }
            if (received != AVERROR(EAGAIN)) {
                ff::check(received, "Error al decodificar el video");
            }
            if (demuxFinished) {
                return false;
            }

            const int read = av_read_frame(format.get(), packet.get());
            if (read < 0) {
                // Fin del archivo (o archivo truncado, p. ej. una grabación cortada):
                // se vacía el decodificador para obtener los fotogramas pendientes.
                demuxFinished = true;
                avcodec_send_packet(codec.get(), nullptr);
                continue;
            }
            if (packet->stream_index == stream->index) {
                // Un paquete dañado no detiene la reproducción: se ignora.
                avcodec_send_packet(codec.get(), packet.get());
            }
            av_packet_unref(packet.get());
        }
    }

    std::int64_t indexOf(const AVFrame* f) const {
        std::int64_t pts = f->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE) {
            pts = f->pts;
        }
        if (pts == AV_NOPTS_VALUE) {
            return lastIndex + 1;
        }
        return rescale(pts - startPts, info.timeBase, info.frameRate.frameDuration());
    }

    VideoFrame convert(const AVFrame* source) {
        const AVFrame* src = source;
        lastFrameWasHw = source->format == hwPixelFormat && hwPixelFormat != AV_PIX_FMT_NONE;
        if (lastFrameWasHw) {
            // Copia de la GPU a memoria. En una etapa posterior el visor usará la
            // textura de la GPU directamente, sin esta copia.
            av_frame_unref(transferFrame.get());
            ff::check(av_hwframe_transfer_data(transferFrame.get(), source, 0),
                      "No se pudo copiar el fotograma desde la GPU");
            transferFrame->colorspace = source->colorspace;
            transferFrame->color_range = source->color_range;
            src = transferFrame.get();
        }

        VideoFrame out;
        out.width = src->width;
        out.height = src->height;
        out.matrix = colorMatrixOf(src);
        out.fullRange = isFullRange(src);
        const int uvRowBytes = out.uvWidth() * 2;
        const auto format = static_cast<AVPixelFormat>(src->format);
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
        // RGB, con paleta o en escala de grises (sin crominancia): Animación, PNG…
        const bool rgb = desc && ((desc->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PAL)) ||
                                  desc->nb_components <= 2);
        if (rgb) {
            // Se pasa a YUV BT.709 de rango limitado.
            out.matrix = ColorMatrix::Bt709;
            out.fullRange = false;
        }

        if (desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA)) {
            return convertWithAlpha(src, std::move(out), rgb);
        }

        if (src->format == AV_PIX_FMT_NV12) {
            copyPlane(out.y, src->data[0], src->linesize[0], out.width, out.height);
            copyPlane(out.uv, src->data[1], src->linesize[1], uvRowBytes, out.uvHeight());
            return out;
        }

        // Cualquier otro formato (yuv420p, 10 bits…) se convierte a NV12.
        prepareSws(src, AV_PIX_FMT_NV12, rgb);
        out.y.resize(static_cast<std::size_t>(out.width) * out.height);
        out.uv.resize(static_cast<std::size_t>(uvRowBytes) * out.uvHeight());
        std::uint8_t* dst[4] = {out.y.data(), out.uv.data(), nullptr, nullptr};
        const int dstStride[4] = {out.width, uvRowBytes, 0, 0};
        sws_scale(sws.get(), src->data, src->linesize, 0, src->height, dst, dstStride);
        return out;
    }

    // Conversión de `src` a `target` (8 bits). De RGB a YUV, con BT.709 de rango limitado.
    void prepareSws(const AVFrame* src, AVPixelFormat target, bool rgb) {
        sws.reset(sws_getCachedContext(
            sws.release(), src->width, src->height, static_cast<AVPixelFormat>(src->format),
            src->width, src->height, target, SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!sws) {
            throw MediaError(
                "Formato de píxel no compatible: " +
                std::string(av_get_pix_fmt_name(static_cast<AVPixelFormat>(src->format))));
        }
        if (rgb) {
            const int* bt709 = sws_getCoefficients(SWS_CS_ITU709);
            sws_setColorspaceDetails(sws.get(), bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);
        }
    }

    // Video con transparencia: YUVA 4:2:0 de 8 bits; el UV se entrelaza (NV12) y el
    // alfa se guarda aparte.
    VideoFrame convertWithAlpha(const AVFrame* src, VideoFrame out, bool rgb) {
        prepareSws(src, AV_PIX_FMT_YUVA420P, rgb);
        const int uvw = out.uvWidth();
        const int uvh = out.uvHeight();
        out.y.resize(static_cast<std::size_t>(out.width) * out.height);
        out.a.resize(out.y.size());
        std::vector<std::uint8_t> u(static_cast<std::size_t>(uvw) * uvh);
        std::vector<std::uint8_t> v(u.size());
        std::uint8_t* dst[4] = {out.y.data(), u.data(), v.data(), out.a.data()};
        const int dstStride[4] = {out.width, uvw, uvw, out.width};
        sws_scale(sws.get(), src->data, src->linesize, 0, src->height, dst, dstStride);
        out.uv.resize(static_cast<std::size_t>(uvw) * 2 * uvh);
        for (std::size_t i = 0; i < u.size(); ++i) {
            out.uv[i * 2] = u[i];
            out.uv[i * 2 + 1] = v[i];
        }
        return out;
    }

    std::optional<VideoFrame> next() {
        if (info.still) {
            if (!stillPending) {
                return std::nullopt; // como un video de un fotograma: ya se entregó
            }
            stillPending = false;
            lastIndex = 0;
            return *stillFrame;
        }
        while (receiveRaw()) {
            const std::int64_t index = indexOf(frame.get());
            lastIndex = index;
            if (index < skipUntil) {
                if (keepFrom >= 0 && index >= keepFrom) {
                    VideoFrame keep = convert(frame.get());
                    keep.index = index;
                    kept.push_back(std::move(keep));
                }
                // Fotograma anterior al destino de una búsqueda: se descarta, pero se
                // guarda por si el clip termina antes del destino.
                av_frame_unref(lastSkipped.get());
                haveLastSkipped = av_frame_ref(lastSkipped.get(), frame.get()) == 0;
                av_frame_unref(frame.get());
                continue;
            }
            skipUntil = 0;
            keepFrom = -1;
            haveLastSkipped = false;
            VideoFrame out = convert(frame.get());
            out.index = index;
            av_frame_unref(frame.get());
            return out;
        }
        keepFrom = -1;

        if (skipUntil > 0 && haveLastSkipped) {
            // El destino estaba más allá del final real: se entrega el último fotograma.
            skipUntil = 0;
            haveLastSkipped = false;
            VideoFrame out = convert(lastSkipped.get());
            out.index = lastIndex;
            av_frame_unref(lastSkipped.get());
            return out;
        }
        return std::nullopt;
    }

    void seek(std::int64_t index, std::int64_t keepBefore) {
        if (info.still) {
            kept.clear();
            stillPending = true; // cualquier destino es el único fotograma
            return;
        }
        const std::int64_t last = std::max<std::int64_t>(info.frameCount - 1, 0);
        index = std::clamp<std::int64_t>(index, 0, last);
        const Rational frameDuration = info.frameRate.frameDuration();
        kept.clear();

        // Poco por delante: seguir decodificando es más rápido que volver al clave.
        const bool pendingFits = !havePending || indexOf(frame.get()) <= index;
        if (keepBefore == 0 && lastIndex >= 0 && index > lastIndex &&
            index - lastIndex <= kForwardSkip && !demuxFinished && pendingFits) {
            skipUntil = index;
            keepFrom = -1;
            return;
        }
        keepFrom = keepBefore > 0 ? std::max<std::int64_t>(0, index - keepBefore) : -1;

        // Se salta al fotograma clave anterior al destino. Si el contenedor no tiene un
        // índice exacto (p. ej. MP4 fragmentado), FFmpeg puede quedarse por detrás o
        // pasarse; si se pasa, se reintenta desde más atrás.
        std::int64_t backoff = 0;
        for (int attempt = 0; attempt < 6; ++attempt) {
            const std::int64_t target = std::max<std::int64_t>(index - backoff, 0);
            const std::int64_t targetPts =
                startPts + rescale(target, frameDuration, info.timeBase, Rounding::Down);

            if (av_seek_frame(format.get(), stream->index, targetPts, AVSEEK_FLAG_BACKWARD) < 0) {
                av_seek_frame(format.get(), stream->index, startPts,
                              AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
            }
            avcodec_flush_buffers(codec.get());
            av_frame_unref(frame.get());
            av_frame_unref(lastSkipped.get());
            demuxFinished = false;
            havePending = false;
            haveLastSkipped = false;
            lastIndex = -1;
            skipUntil = index;
            kept.clear();

            if (!receiveRaw()) {
                return; // Sin fotogramas tras el salto: nextFrame() devolverá nullopt.
            }
            const std::int64_t first = indexOf(frame.get());
            if (first <= index || target == 0) {
                havePending = true; // nextFrame() continuará desde aquí
                return;
            }
            av_frame_unref(frame.get());
            backoff = backoff == 0 ? std::int64_t{60} : backoff * 4;
        }
        havePending = false;
    }
};

VideoDecoder::VideoDecoder(const std::string& path, HwAccel hwAccel) : d(std::make_unique<Impl>()) {
    d->open(path, hwAccel);
}

VideoDecoder::~VideoDecoder() = default;

const VideoStreamInfo& VideoDecoder::info() const {
    return d->info;
}

std::string VideoDecoder::hwAccelName() const {
    return d->lastFrameWasHw ? d->hwName : std::string();
}

std::optional<VideoFrame> VideoDecoder::nextFrame() {
    return d->next();
}

void VideoDecoder::seekToFrame(std::int64_t index, std::int64_t keepBefore) {
    d->seek(index, std::max<std::int64_t>(0, keepBefore));
}

void VideoDecoder::seekNear(std::int64_t index) {
    d->lastIndex = -1; // siempre al fotograma clave (nada de seguir decodificando)
    d->seek(index, 0);
    d->skipUntil = 0; // el primero que salga, sin descartar hasta el destino
}

std::vector<VideoFrame> VideoDecoder::takeKeptFrames() {
    return std::exchange(d->kept, {});
}

std::string probeHwDecodeDevice() {
    for (const AVHWDeviceType type : preferredHwDevices()) {
        AVBufferRef* device = nullptr;
        if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) >= 0) {
            av_buffer_unref(&device);
            return av_hwdevice_get_type_name(type);
        }
    }
    return {};
}

} // namespace sharkcut
