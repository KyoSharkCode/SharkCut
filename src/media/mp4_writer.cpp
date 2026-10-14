#include "media/mp4_writer.h"

#include "media/audio_decoder.h"
#include "media/ffmpeg_util.h"
#include "media/media_error.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cstring>

namespace sharkcut {

namespace {

std::vector<std::string> defaultVideoEncoders() {
    // Por hardware primero (NVIDIA, AMD, Intel); x264 por software como respaldo.
    return {"h264_nvenc", "h264_amf", "h264_qsv", "libx264"};
}

void configureEncoder(AVCodecContext* enc, const std::string& name, std::int64_t bitrate) {
    enc->bit_rate = bitrate;
    enc->rc_max_rate = bitrate * 3 / 2;
    enc->rc_buffer_size = static_cast<int>(std::min<std::int64_t>(bitrate * 2, INT32_MAX));
    if (name == "h264_nvenc") {
        av_opt_set(enc->priv_data, "preset", "p5", 0); // calidad alta, aún muy rápido
        av_opt_set(enc->priv_data, "rc", "vbr", 0);
        av_opt_set(enc->priv_data, "cq", "19", 0);
        av_opt_set(enc->priv_data, "spatial-aq", "1", 0);
    } else if (name == "h264_amf") {
        av_opt_set(enc->priv_data, "quality", "quality", 0);
    } else if (name == "h264_qsv") {
        av_opt_set(enc->priv_data, "preset", "slow", 0);
    } else if (name == "libx264") {
        av_opt_set(enc->priv_data, "preset", "medium", 0);
        av_opt_set(enc->priv_data, "crf", "18", 0);
    }
}

} // namespace

struct Mp4Writer::Impl {
    ff::FormatContextPtr out; // se libera con avformat_free_context (ver destructor)
    AVStream* videoStream = nullptr;
    ff::CodecContextPtr video;
    std::string videoName;
    AVStream* audioStream = nullptr;
    ff::CodecContextPtr audio;
    ff::SwsPtr sws;
    ff::FramePtr videoFrame{av_frame_alloc()};
    ff::FramePtr audioFrame{av_frame_alloc()};
    ff::PacketPtr packet{av_packet_alloc()};
    std::int64_t nextVideoPts = 0;
    std::int64_t nextAudioPts = 0;
    std::vector<float> audioPending; // estéreo intercalado, a la espera de un bloque completo
    bool headerWritten = false;
    bool finished = false;
    EncoderSettings settings;

    ~Impl() {
        // avformat_close_input no sirve para contextos de salida.
        if (AVFormatContext* ctx = out.release()) {
            if (ctx->pb && !(ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&ctx->pb);
            }
            avformat_free_context(ctx);
        }
    }

    void drain(AVCodecContext* enc, AVStream* stream) {
        for (;;) {
            const int r = avcodec_receive_packet(enc, packet.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                return;
            }
            ff::check(r, "Error al codificar");
            av_packet_rescale_ts(packet.get(), enc->time_base, stream->time_base);
            packet->stream_index = stream->index;
            ff::check(av_interleaved_write_frame(out.get(), packet.get()),
                      "Error al escribir el archivo");
        }
    }

    bool openVideo(const std::string& name) {
        const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec) {
            return false;
        }
        ff::CodecContextPtr enc(avcodec_alloc_context3(codec));
        enc->width = settings.width;
        enc->height = settings.height;
        enc->time_base = {static_cast<int>(settings.rate.den), static_cast<int>(settings.rate.num)};
        enc->framerate = {static_cast<int>(settings.rate.num), static_cast<int>(settings.rate.den)};
        enc->pix_fmt = name == "h264_qsv" ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
        enc->gop_size = static_cast<int>(settings.rate.nominal() * 2); // clave cada 2 s
        enc->max_b_frames = 2;
        enc->colorspace = AVCOL_SPC_BT709;
        enc->color_primaries = AVCOL_PRI_BT709;
        enc->color_trc = AVCOL_TRC_BT709;
        enc->color_range = AVCOL_RANGE_MPEG;
        if (out->oformat->flags & AVFMT_GLOBALHEADER) {
            enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        configureEncoder(enc.get(), name, settings.videoBitrate);
        if (avcodec_open2(enc.get(), codec, nullptr) < 0) {
            return false; // p. ej. NVENC sin tarjeta NVIDIA: se prueba el siguiente
        }
        video = std::move(enc);
        videoName = name;
        return true;
    }

    void openAudio() {
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!codec) {
            throw MediaError("No hay codificador AAC disponible");
        }
        audio.reset(avcodec_alloc_context3(codec));
        audio->sample_rate = kAudioSampleRate;
        audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_default(&audio->ch_layout, kAudioChannels);
        audio->time_base = {1, kAudioSampleRate};
        audio->bit_rate = 192'000;
        if (out->oformat->flags & AVFMT_GLOBALHEADER) {
            audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        ff::check(avcodec_open2(audio.get(), codec, nullptr),
                  "No se pudo abrir el codificador AAC");
        audioStream = avformat_new_stream(out.get(), nullptr);
        ff::check(avcodec_parameters_from_context(audioStream->codecpar, audio.get()),
                  "No se pudo configurar el audio");
        audioStream->time_base = audio->time_base;
    }

    void open(const std::string& path) {
        AVFormatContext* raw = nullptr;
        ff::check(avformat_alloc_output_context2(&raw, nullptr, "mp4", path.c_str()),
                  "No se pudo crear el MP4");
        out.reset(raw);

        const std::vector<std::string> candidates =
            settings.videoEncoders.empty() ? defaultVideoEncoders() : settings.videoEncoders;
        for (const std::string& name : candidates) {
            if (openVideo(name)) {
                break;
            }
        }
        if (!video) {
            throw MediaError("No hay ningún codificador de video H.264 disponible");
        }
        videoStream = avformat_new_stream(out.get(), nullptr);
        ff::check(avcodec_parameters_from_context(videoStream->codecpar, video.get()),
                  "No se pudo configurar el video");
        videoStream->time_base = video->time_base;
        videoStream->avg_frame_rate = video->framerate;

        if (settings.audio) {
            openAudio();
        }

        ff::check(avio_open(&out->pb, path.c_str(), AVIO_FLAG_WRITE),
                  "No se pudo crear el archivo de salida");
        AVDictionary* options = nullptr;
        av_dict_set(&options, "movflags", "+faststart", 0); // reproducible antes de descargarse
        const int written = avformat_write_header(out.get(), &options);
        av_dict_free(&options);
        ff::check(written, "No se pudo escribir la cabecera del MP4");
        headerWritten = true;

        videoFrame->format = video->pix_fmt;
        videoFrame->width = settings.width;
        videoFrame->height = settings.height;
        ff::check(av_frame_get_buffer(videoFrame.get(), 0), "Sin memoria para el video");

        sws.reset(sws_getContext(settings.width, settings.height, AV_PIX_FMT_RGBA, settings.width,
                                 settings.height, video->pix_fmt, SWS_BICUBIC, nullptr, nullptr,
                                 nullptr));
        if (!sws) {
            throw MediaError("No se pudo preparar la conversión de color");
        }
        // RGB (rango completo) → YUV BT.709 de rango limitado.
        const int* coefficients = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(sws.get(), coefficients, 1, coefficients, 0, 0, 1 << 16, 1 << 16);
    }

    void writeVideo(const std::uint8_t* rgba, int stride) {
        ff::check(av_frame_make_writable(videoFrame.get()), "Sin memoria para el video");
        const std::uint8_t* src[4] = {rgba, nullptr, nullptr, nullptr};
        const int srcStride[4] = {stride, 0, 0, 0};
        sws_scale(sws.get(), src, srcStride, 0, settings.height, videoFrame->data,
                  videoFrame->linesize);
        videoFrame->pts = nextVideoPts++;
        ff::check(avcodec_send_frame(video.get(), videoFrame.get()), "Error al codificar el video");
        drain(video.get(), videoStream);
    }

    void encodeAudioBlock(std::size_t frames) {
        av_frame_unref(audioFrame.get());
        audioFrame->format = AV_SAMPLE_FMT_FLTP;
        audioFrame->nb_samples = static_cast<int>(frames);
        audioFrame->sample_rate = kAudioSampleRate;
        av_channel_layout_default(&audioFrame->ch_layout, kAudioChannels);
        ff::check(av_frame_get_buffer(audioFrame.get(), 0), "Sin memoria para el audio");
        auto* left = reinterpret_cast<float*>(audioFrame->data[0]);
        auto* right = reinterpret_cast<float*>(audioFrame->data[1]);
        for (std::size_t i = 0; i < frames; ++i) {
            left[i] = audioPending[i * 2];
            right[i] = audioPending[i * 2 + 1];
        }
        audioPending.erase(audioPending.begin(),
                           audioPending.begin() + static_cast<std::ptrdiff_t>(frames * 2));
        audioFrame->pts = nextAudioPts;
        nextAudioPts += static_cast<std::int64_t>(frames);
        ff::check(avcodec_send_frame(audio.get(), audioFrame.get()), "Error al codificar el audio");
        drain(audio.get(), audioStream);
    }

    void writeAudio(const float* samples, std::size_t frames) {
        if (!audio) {
            return;
        }
        audioPending.insert(audioPending.end(), samples, samples + frames * kAudioChannels);
        const auto block =
            static_cast<std::size_t>(audio->frame_size > 0 ? audio->frame_size : 1024);
        while (audioPending.size() >= block * kAudioChannels) {
            encodeAudioBlock(block);
        }
    }

    void finish() {
        if (finished || !headerWritten) {
            return;
        }
        finished = true;
        if (audio) {
            if (!audioPending.empty()) {
                encodeAudioBlock(audioPending.size() / kAudioChannels);
            }
            avcodec_send_frame(audio.get(), nullptr);
            drain(audio.get(), audioStream);
        }
        avcodec_send_frame(video.get(), nullptr);
        drain(video.get(), videoStream);
        ff::check(av_write_trailer(out.get()), "No se pudo cerrar el MP4");
    }
};

Mp4Writer::Mp4Writer(const std::string& path, const EncoderSettings& settings)
    : d(std::make_unique<Impl>()) {
    d->settings = settings;
    d->open(path);
}

Mp4Writer::~Mp4Writer() = default;

std::string Mp4Writer::videoEncoderName() const {
    return d->videoName;
}

void Mp4Writer::writeVideo(const std::uint8_t* rgba, int stride) {
    d->writeVideo(rgba, stride);
}

void Mp4Writer::writeAudio(const float* samples, std::size_t frames) {
    d->writeAudio(samples, frames);
}

void Mp4Writer::finish() {
    d->finish();
}

} // namespace sharkcut
