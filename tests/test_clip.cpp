#include "test_clip.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

#include <cstdlib>
#include <cstring>
#include <random>
#include <stdexcept>

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

std::string writeTestClip(const std::filesystem::path& path, const ClipOptions& o) {
    const std::string file = path.string();
    AVFormatContext* out = nullptr;
    check(avformat_alloc_output_context2(&out, nullptr, "mp4", file.c_str()), "alloc_output");

    // H.264 si está disponible (como las grabaciones reales); si no, MPEG-4.
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    }
    if (!codec) {
        throw std::runtime_error("no hay codificador de video disponible");
    }

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
    } else {
        enc->flags |= AV_CODEC_FLAG_QSCALE;
        enc->global_quality = FF_QP2LAMBDA * 2;
    }
    check(avcodec_open2(enc, codec, nullptr), "open encoder");
    check(avcodec_parameters_from_context(stream->codecpar, enc), "parameters");
    stream->time_base = enc->time_base;

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
    }
    writePackets(enc, out, stream, nullptr, o.packetDurations);
    check(av_write_trailer(out), "write_trailer");

    const std::string used = codec->name;
    av_frame_free(&frame);
    avcodec_free_context(&enc);
    avio_closep(&out->pb);
    avformat_free_context(out);
    return used == "libx264" ? "h264" : "mpeg4";
}

} // namespace sharkcut::test
