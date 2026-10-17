#include "test_clip.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <cstdlib>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>

namespace sharkcut::test {

namespace {

void check(int code, const char* what) {
    if (code < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(code, buffer, sizeof(buffer));
        throw std::runtime_error(std::string(what) + ": " + buffer);
    }
}

void writePackets(AVCodecContext* enc, AVFormatContext* out, AVStream* stream, AVFrame* frame,
                  bool durations) {
    check(avcodec_send_frame(enc, frame), "send_frame");
    AVPacket* packet = av_packet_alloc();
    for (;;) {
        const int r = avcodec_receive_packet(enc, packet);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
            break;
        }
        check(r, "receive_packet");
        if (durations && packet->duration == 0) {
            packet->duration = 1; // un fotograma, en la base de tiempo del codificador
        }
        av_packet_rescale_ts(packet, enc->time_base, stream->time_base);
        packet->stream_index = stream->index;
        check(av_interleaved_write_frame(out, packet), "write_frame");
    }
    av_packet_free(&packet);
}

} // namespace

float audioLevelFor(int index) {
    return static_cast<float>((index % 200 + 1) * 150) / 32768.0f;
}

int levelFor(int digit) {
    return 24 + digit * 13; // 24 … 219, dentro del rango limitado (16–235)
}

int decodeIndex(int leftLevel, int rightLevel) {
    auto digitOf = [](int level) {
        const int digit = (level - 24 + 6) / 13;
        if (digit < 0 || digit > 15 || std::abs(level - levelFor(digit)) > 4) {
            return -1;
        }
        return digit;
    };
    const int low = digitOf(leftLevel);
    const int high = digitOf(rightLevel);
    return (low < 0 || high < 0) ? -1 : high * 16 + low;
}

std::filesystem::path tempClipPath(const std::string& name) {
    std::random_device rd;
    return std::filesystem::temp_directory_path() /
           ("sharkcut-test-" + std::to_string(rd()) + "-" + name);
}

namespace {

// Codificador de audio y muestras pendientes de enviarle.
struct AudioTrack {
    AVCodecContext* enc = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    std::vector<std::int16_t> pending; // estéreo intercalado
    std::int64_t nextPts = 0;          // en muestras (base 1/48000)
    int chunk = 0;                     // muestras por trama enviada al codificador

    void push(const std::vector<std::int16_t>& samples) {
        pending.insert(pending.end(), samples.begin(), samples.end());
    }

    void encode(AVFormatContext* out, bool flush) {
        while (pending.size() >= static_cast<std::size_t>(chunk) * 2 ||
               (flush && !pending.empty())) {
            const int n = std::min<int>(chunk, static_cast<int>(pending.size() / 2));
            av_frame_unref(frame);
            frame->format = enc->sample_fmt;
            frame->nb_samples = n;
            frame->sample_rate = enc->sample_rate;
            av_channel_layout_copy(&frame->ch_layout, &enc->ch_layout);
            check(av_frame_get_buffer(frame, 0), "audio buffer");
            if (enc->sample_fmt == AV_SAMPLE_FMT_S16) {
                std::memcpy(frame->data[0], pending.data(),
                            static_cast<std::size_t>(n) * 2 * sizeof(std::int16_t));
            } else { // AV_SAMPLE_FMT_FLTP (AAC)
                auto* left = reinterpret_cast<float*>(frame->data[0]);
                auto* right = reinterpret_cast<float*>(frame->data[1]);
                for (int i = 0; i < n; ++i) {
                    left[i] = pending[static_cast<std::size_t>(i) * 2] / 32768.0f;
                    right[i] = pending[static_cast<std::size_t>(i) * 2 + 1] / 32768.0f;
                }
            }
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(n) * 2);
            frame->pts = nextPts;
            nextPts += n;
            writePackets(enc, out, stream, frame, true);
        }
        if (flush) {
            writePackets(enc, out, stream, nullptr, true);
        }
    }
};

} // namespace

std::string writeTestClip(const std::filesystem::path& path, const ClipOptions& o) {
    const std::string file = path.string();

    // H.264 si está disponible (como las grabaciones reales); si no, MPEG-4.
    const AVCodec* codec = nullptr;
    if (!o.videoEncoder.empty()) {
        codec = avcodec_find_encoder_by_name(o.videoEncoder.c_str());
        if (!codec) {
            return {};
        }
    } else {
        codec = avcodec_find_encoder_by_name("libx264");
        if (!codec) {
            codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        }
    }
    if (!codec) {
        throw std::runtime_error("no hay codificador de video disponible");
    }

    AVFormatContext* out = nullptr;
    check(avformat_alloc_output_context2(&out, nullptr, o.container.c_str(), file.c_str()),
          "alloc_output");

    AVStream* stream = avformat_new_stream(out, nullptr);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->width = o.width;
    enc->height = o.height;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->time_base = {static_cast<int>(o.rate.den), static_cast<int>(o.rate.num)};
    enc->framerate = {static_cast<int>(o.rate.num), static_cast<int>(o.rate.den)};
    enc->gop_size = o.gopSize;
    enc->max_b_frames = 2; // reordenación, como en H.264 real
    enc->colorspace = AVCOL_SPC_BT709;
    enc->color_range = AVCOL_RANGE_MPEG;
    if (out->oformat->flags & AVFMT_GLOBALHEADER) {
        enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if (std::strcmp(codec->name, "libx264") == 0) {
        av_opt_set(enc->priv_data, "preset", "ultrafast", 0);
        av_opt_set(enc->priv_data, "qp", "4", 0);
        av_opt_set(enc->priv_data, "x264-params", "scenecut=0", 0);
    } else if (std::strcmp(codec->name, "libsvtav1") == 0) {
        av_opt_set(enc->priv_data, "preset", "12", 0);
        av_opt_set(enc->priv_data, "crf", "8", 0);
    } else if (std::strcmp(codec->name, "libaom-av1") == 0) {
        av_opt_set(enc->priv_data, "cpu-used", "8", 0);
        av_opt_set(enc->priv_data, "crf", "8", 0);
    } else {
        enc->flags |= AV_CODEC_FLAG_QSCALE;
        enc->global_quality = FF_QP2LAMBDA * 2;
    }
    check(avcodec_open2(enc, codec, nullptr), "open encoder");
    check(avcodec_parameters_from_context(stream->codecpar, enc), "parameters");
    stream->time_base = enc->time_base;

    AudioTrack audio;
    if (o.audio != ClipOptions::Audio::None) {
        const AVCodec* audioCodec = avcodec_find_encoder(
            o.audio == ClipOptions::Audio::Pcm ? AV_CODEC_ID_PCM_S16LE : AV_CODEC_ID_AAC);
        if (!audioCodec) {
            throw std::runtime_error("no hay codificador de audio disponible");
        }
        audio.stream = avformat_new_stream(out, nullptr);
        audio.enc = avcodec_alloc_context3(audioCodec);
        audio.enc->sample_rate = 48'000;
        audio.enc->sample_fmt =
            o.audio == ClipOptions::Audio::Pcm ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLTP;
        av_channel_layout_default(&audio.enc->ch_layout, 2);
        audio.enc->time_base = {1, 48'000};
        audio.enc->bit_rate = 192'000;
        if (out->oformat->flags & AVFMT_GLOBALHEADER) {
            audio.enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        check(avcodec_open2(audio.enc, audioCodec, nullptr), "open audio encoder");
        check(avcodec_parameters_from_context(audio.stream->codecpar, audio.enc), "audio params");
        audio.stream->time_base = audio.enc->time_base;
        audio.frame = av_frame_alloc();
        audio.chunk = audio.enc->frame_size > 0 ? audio.enc->frame_size : 800;
        audio.nextPts =
            static_cast<std::int64_t>(o.audioDelayFrames) * 48'000 * o.rate.den / o.rate.num;
    }

    check(avio_open(&out->pb, file.c_str(), AVIO_FLAG_WRITE), "avio_open");
    AVDictionary* opts = nullptr;
    if (o.fragmented) {
        av_dict_set(&opts, "movflags", "+frag_keyframe+empty_moov+default_base_moof", 0);
    }
    check(avformat_write_header(out, &opts), "write_header");
    av_dict_free(&opts);

    AVFrame* frame = av_frame_alloc();
    frame->format = enc->pix_fmt;
    frame->width = o.width;
    frame->height = o.height;
    check(av_frame_get_buffer(frame, 0), "frame buffer");

    const std::int64_t samplesPerFrame = 48'000 * o.rate.den / o.rate.num;
    for (int i = 0; i < o.frames; ++i) {
        check(av_frame_make_writable(frame), "make_writable");
        const int left = levelFor(i % 16);
        const int right = levelFor((i / 16) % 16);
        for (int y = 0; y < o.height; ++y) {
            std::uint8_t* row =
                frame->data[0] + static_cast<std::ptrdiff_t>(y) * frame->linesize[0];
            std::memset(row, left, static_cast<std::size_t>(o.width / 2));
            std::memset(row + o.width / 2, right, static_cast<std::size_t>(o.width - o.width / 2));
        }
        for (int plane = 1; plane <= 2; ++plane) {
            for (int y = 0; y < (o.height + 1) / 2; ++y) {
                std::memset(frame->data[plane] +
                                static_cast<std::ptrdiff_t>(y) * frame->linesize[plane],
                            128, static_cast<std::size_t>((o.width + 1) / 2));
            }
        }
        frame->pts = i;
        writePackets(enc, out, stream, frame, o.packetDurations);

        if (audio.enc) {
            // Durante el fotograma i, todas las muestras valen audioLevelFor(i).
            const auto value = static_cast<std::int16_t>(audioLevelFor(i) * 32768.0f);
            audio.push(
                std::vector<std::int16_t>(static_cast<std::size_t>(samplesPerFrame) * 2, value));
            audio.encode(out, false);
        }
    }
    writePackets(enc, out, stream, nullptr, o.packetDurations);
    if (audio.enc) {
        audio.encode(out, true);
    }
    check(av_write_trailer(out), "write_trailer");

    const std::string used = codec->name;
    av_frame_free(&frame);
    avcodec_free_context(&enc);
    if (audio.enc) {
        av_frame_free(&audio.frame);
        avcodec_free_context(&audio.enc);
    }
    avio_closep(&out->pb);
    avformat_free_context(out);
    return used == "libx264" ? "h264" : used;
}

} // namespace sharkcut::test
