#include "media/media_info.h"

#include "media/ffmpeg_util.h"
#include "media/media_info_internal.h"

#include <cmath>
#include <string>
#include <string_view>

namespace sharkcut {

namespace detail {

namespace {

bool isUsable(AVRational r) {
    constexpr int kLimit = 1 << 30;
    return r.num > 0 && r.den > 0 && r.num < kLimit && r.den < kLimit;
}

// true si las dos tasas difieren menos de un 2 %.
bool isClose(AVRational a, AVRational b) {
    const double x = av_q2d(a);
    const double y = av_q2d(b);
    return std::abs(x - y) <= y * 0.02;
}

FrameRate chooseFrameRate(const AVStream* stream) {
    // r_frame_rate es la base exacta que deduce FFmpeg (60/1); avg_frame_rate es la
    // media según la duración, que puede salir ligeramente desviada (p. ej. 7200/119)
    // si el último paquete no indica su duración. Si son parecidas, manda la base.
    const AVRational base = stream->r_frame_rate;
    const AVRational avg = stream->avg_frame_rate;
    AVRational r{60, 1};
    if (isUsable(base) && (!isUsable(avg) || isClose(avg, base))) {
        r = base;
    } else if (isUsable(avg)) {
        r = avg;
    } else if (isUsable(base)) {
        r = base;
    }
    int num = 0;
    int den = 0;
    av_reduce(&num, &den, r.num, r.den, 1 << 30);
    return {num, den};
}

bool isStillImage(const AVFormatContext* format) {
    // Las imágenes sueltas las abren los demuxers de imágenes: "image2" o uno por formato
    // ("png_pipe", "jpeg_pipe", "webp_pipe"…).
    const std::string name = format->iformat && format->iformat->name ? format->iformat->name : "";
    constexpr std::string_view kPipe = "_pipe";
    return name == "image2" || (name.size() > kPipe.size() &&
                                name.compare(name.size() - kPipe.size(), kPipe.size(), kPipe) == 0);
}

} // namespace

VideoStreamInfo describeVideoStream(const AVFormatContext* format, const AVStream* stream) {
    const AVCodecParameters* par = stream->codecpar;

    VideoStreamInfo info;
    info.streamIndex = stream->index;
    info.codec = avcodec_get_name(par->codec_id);
    const char* pixName = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format));
    info.pixelFormat = pixName ? pixName : "desconocido";
    info.width = par->width;
    info.height = par->height;
    info.frameRate = chooseFrameRate(stream);
    info.timeBase = ff::toRational(stream->time_base);

    if (isUsable(stream->avg_frame_rate) && isUsable(stream->r_frame_rate)) {
        // Media y base muy distintas: probablemente fotogramas variables (VFR).
        info.variableFrameRate = !isClose(stream->avg_frame_rate, stream->r_frame_rate);
    }

    // Número de fotogramas: el que declara el contenedor o, si no lo hay (MP4
    // fragmentado de OBS), el que resulta de la duración y los fps.
    if (isStillImage(format)) {
        info.still = true;
        info.frameCount = 1;
        info.variableFrameRate = false;
    } else if (stream->nb_frames > 0) {
        info.frameCount = stream->nb_frames;
    } else {
        std::int64_t durationUs = 0;
        if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
            durationUs = rescale(stream->duration, info.timeBase, kMicroseconds);
        } else if (format->duration != AV_NOPTS_VALUE && format->duration > 0) {
            durationUs = format->duration; // AV_TIME_BASE = microsegundos
        }
        info.frameCount = rescale(durationUs, kMicroseconds, info.frameRate.frameDuration());
    }
    return info;
}

} // namespace detail

void quietMediaLog() {
    av_log_set_level(AV_LOG_ERROR);
}

MediaInfo probeMedia(const std::string& path) {
    ff::FormatContextPtr format = ff::openInput(path);

    MediaInfo info;
    info.container = format->iformat && format->iformat->name ? format->iformat->name : "";
    if (format->duration != AV_NOPTS_VALUE && format->duration > 0) {
        info.durationUs = format->duration;
    }

    const int videoIndex =
        av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex >= 0) {
        info.video = detail::describeVideoStream(format.get(), format->streams[videoIndex]);
    }

    // La principal, como la elige AudioDecoder.
    const int mainAudio = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const AVStream* stream = format->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
            continue;
        }
        AudioStreamInfo audio;
        audio.streamIndex = stream->index;
        audio.codec = avcodec_get_name(stream->codecpar->codec_id);
        audio.sampleRate = stream->codecpar->sample_rate;
        audio.channels = stream->codecpar->ch_layout.nb_channels;
        if (const AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", nullptr, 0)) {
            audio.title = title->value;
        }
        audio.main = stream->index == mainAudio;
        info.audio.push_back(audio);
    }
    return info;
}

} // namespace sharkcut
