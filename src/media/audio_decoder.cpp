#include "media/audio_decoder.h"

#include "media/ffmpeg_util.h"
#include "media/media_error.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstring>
#include <vector>

namespace sharkcut {

namespace {

struct SwrDeleter {
    void operator()(SwrContext* p) const { swr_free(&p); }
};

// Como mucho se rellenan 10 s de silencio de una vez (protege ante pts erróneos).
constexpr std::int64_t kMaxPadding = std::int64_t{kAudioSampleRate} * 10;

} // namespace

struct AudioDecoder::Impl {
    ff::FormatContextPtr format;
    AVStream* stream = nullptr;
    ff::CodecContextPtr codec;
    std::unique_ptr<SwrContext, SwrDeleter> swr;
    AudioStreamInfo info;

    // Origen del clip (inicio del video) en microsegundos de la línea de tiempo del archivo.
    std::int64_t originUs = 0;

    ff::PacketPtr packet{av_packet_alloc()};
    ff::FramePtr frame{av_frame_alloc()};
    bool demuxFinished = false;
    bool decoderFinished = false;

    // Formato de entrada con el que se configuró swr (se reconfigura si cambia).
    AVChannelLayout swrLayout{};
    int swrRate = 0;
    int swrFormat = -1;

    std::vector<float> pending;    // muestras convertidas aún no entregadas
    std::size_t pendingOffset = 0; // en floats

    bool aligning = true; // la próxima trama se alinea con `alignTarget`
    std::int64_t alignTarget = 0;
    std::int64_t position = 0;

    ~Impl() { av_channel_layout_uninit(&swrLayout); }

    void open(const std::string& path) {
        format = ff::openInput(path);

        const AVCodec* decoder = nullptr;
        const int index =
            av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
        if (index < 0 || !decoder) {
            throw MediaError("El archivo no tiene audio compatible");
        }
        stream = format->streams[index];

        // El origen es el inicio del video; sin video, el inicio del propio audio.
        const int videoIndex =
            av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, index, nullptr, 0);
        const AVStream* reference = videoIndex >= 0 ? format->streams[videoIndex] : stream;
        if (reference->start_time != AV_NOPTS_VALUE) {
            originUs =
                rescale(reference->start_time, ff::toRational(reference->time_base), kMicroseconds);
        }

        for (unsigned i = 0; i < format->nb_streams; ++i) {
            if (static_cast<int>(i) != index) {
                format->streams[i]->discard = AVDISCARD_ALL;
            }
        }

        codec.reset(avcodec_alloc_context3(decoder));
        if (!codec) {
            throw MediaError("No se pudo crear el decodificador de audio");
        }
        ff::check(avcodec_parameters_to_context(codec.get(), stream->codecpar),
                  "No se pudo configurar el decodificador de audio");
        codec->pkt_timebase = stream->time_base;
        ff::check(avcodec_open2(codec.get(), decoder, nullptr),
                  "No se pudo abrir el decodificador de audio");

        info.streamIndex = index;
        info.codec = avcodec_get_name(stream->codecpar->codec_id);
        info.sampleRate = stream->codecpar->sample_rate;
        info.channels = stream->codecpar->ch_layout.nb_channels;
    }

    void configureResampler(const AVFrame* f) {
        if (swr && swrRate == f->sample_rate && swrFormat == f->format &&
            av_channel_layout_compare(&swrLayout, &f->ch_layout) == 0) {
            return;
        }
        AVChannelLayout input{};
        if (f->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || f->ch_layout.nb_channels == 0) {
            av_channel_layout_default(&input, std::max(f->ch_layout.nb_channels, 1));
        } else {
            av_channel_layout_copy(&input, &f->ch_layout);
        }
        AVChannelLayout output{};
        av_channel_layout_default(&output, kAudioChannels);

        SwrContext* raw = nullptr;
        const int allocated =
            swr_alloc_set_opts2(&raw, &output, AV_SAMPLE_FMT_FLT, kAudioSampleRate, &input,
                                static_cast<AVSampleFormat>(f->format), f->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&input);
        ff::check(allocated, "No se pudo preparar la conversión de audio");
        swr.reset(raw);
        ff::check(swr_init(swr.get()), "No se pudo preparar la conversión de audio");

        av_channel_layout_uninit(&swrLayout);
        av_channel_layout_copy(&swrLayout, &f->ch_layout);
        swrRate = f->sample_rate;
        swrFormat = f->format;
    }

    // Decodifica la siguiente trama en `frame`. Devuelve false al final del audio.
    bool receiveRaw() {
        for (;;) {
            const int received = avcodec_receive_frame(codec.get(), frame.get());
            if (received == 0) {
                return true;
            }
            if (received == AVERROR_EOF) {
                return false;
            }
            if (received != AVERROR(EAGAIN)) {
                ff::check(received, "Error al decodificar el audio");
            }
            if (demuxFinished) {
                return false;
            }
            if (av_read_frame(format.get(), packet.get()) < 0) {
                demuxFinished = true;
                avcodec_send_packet(codec.get(), nullptr);
                continue;
            }
            if (packet->stream_index == stream->index) {
                avcodec_send_packet(codec.get(), packet.get()); // un paquete dañado se ignora
            }
            av_packet_unref(packet.get());
        }
    }

    // Convierte la trama actual y la añade a `pending`, aplicando la alineación.
    void appendFrame() {
        configureResampler(frame.get());

        const int capacity = swr_get_out_samples(swr.get(), frame->nb_samples);
        std::vector<float> converted(static_cast<std::size_t>(std::max(capacity, 0)) *
                                     kAudioChannels);
        auto* outPtr = reinterpret_cast<std::uint8_t*>(converted.data());
        const int produced =
            swr_convert(swr.get(), &outPtr, capacity,
                        const_cast<const std::uint8_t**>(frame->extended_data), frame->nb_samples);
        ff::check(produced, "Error al convertir el audio");
        converted.resize(static_cast<std::size_t>(produced) * kAudioChannels);

        if (aligning) {
            std::int64_t pts = frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) {
                pts = frame->pts;
            }
            aligning = false;
            if (pts != AV_NOPTS_VALUE) {
                const std::int64_t startUs =
                    rescale(pts, ff::toRational(stream->time_base), kMicroseconds) - originUs;
                const std::int64_t start = rescale(startUs, kMicroseconds, kAudioSampleDuration);
                const std::int64_t end = start + produced;
                if (end <= alignTarget) {
                    aligning = true; // la trama entera es anterior al destino
                    return;
                }
                if (start < alignTarget) {
                    const auto skip =
                        static_cast<std::size_t>(alignTarget - start) * kAudioChannels;
                    converted.erase(converted.begin(),
                                    converted.begin() + static_cast<std::ptrdiff_t>(skip));
                } else if (start > alignTarget) {
                    const std::int64_t pad = std::min(start - alignTarget, kMaxPadding);
                    pending.insert(pending.end(), static_cast<std::size_t>(pad) * kAudioChannels,
                                   0.0f);
                }
            }
        }
        pending.insert(pending.end(), converted.begin(), converted.end());
    }

    std::size_t read(float* out, std::size_t frames) {
        std::size_t written = 0;
        while (written < frames) {
            const std::size_t available = (pending.size() - pendingOffset) / kAudioChannels;
            if (available > 0) {
                const std::size_t n = std::min(available, frames - written);
                std::memcpy(out + written * kAudioChannels, pending.data() + pendingOffset,
                            n * kAudioChannels * sizeof(float));
                pendingOffset += n * kAudioChannels;
                written += n;
                continue;
            }
            pending.clear();
            pendingOffset = 0;
            if (decoderFinished || !receiveRaw()) {
                decoderFinished = true;
                break;
            }
            appendFrame();
            av_frame_unref(frame.get());
        }
        position += static_cast<std::int64_t>(written);
        return written;
    }

    void seek(std::int64_t sample) {
        sample = std::max<std::int64_t>(sample, 0);
        const std::int64_t targetUs =
            originUs + rescale(sample, kAudioSampleDuration, kMicroseconds);
        const std::int64_t targetPts =
            rescale(targetUs, kMicroseconds, ff::toRational(stream->time_base), Rounding::Down);
        if (av_seek_frame(format.get(), stream->index, targetPts, AVSEEK_FLAG_BACKWARD) < 0) {
            av_seek_frame(format.get(), stream->index, 0, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
        }
        avcodec_flush_buffers(codec.get());
        av_frame_unref(frame.get());
        swr.reset(); // se recrea con la próxima trama (vacía su memoria interna)
        pending.clear();
        pendingOffset = 0;
        demuxFinished = false;
        decoderFinished = false;
        aligning = true;
        alignTarget = sample;
        position = sample;
    }
};

AudioDecoder::AudioDecoder(const std::string& path) : d(std::make_unique<Impl>()) {
    d->open(path);
}

AudioDecoder::~AudioDecoder() = default;

const AudioStreamInfo& AudioDecoder::info() const {
    return d->info;
}

std::size_t AudioDecoder::read(float* out, std::size_t frames) {
    return d->read(out, frames);
}

void AudioDecoder::seekToSample(std::int64_t sample) {
    d->seek(sample);
}

std::int64_t AudioDecoder::position() const {
    return d->position;
}

} // namespace sharkcut
