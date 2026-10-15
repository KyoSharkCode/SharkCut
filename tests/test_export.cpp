#include "media/audio_decoder.h"
#include "media/media_error.h"
#include "media/media_info.h"
#include "media/mp4_writer.h"
#include "media/video_decoder.h"
#include "test_clip.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace sharkcut;

namespace {

struct TempFile {
    std::filesystem::path path = test::tempClipPath("export.mp4");
    ~TempFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

// Gris del fotograma i (RGB), y la luminancia BT.709 limitada que le corresponde.
int grayFor(int i) {
    return 20 + (i * 3) % 200;
}
int lumaFor(int gray) {
    return 16 + static_cast<int>(std::lround(gray * 219.0 / 255.0));
}

} // namespace

TEST_CASE("Mp4Writer escribe video y audio legibles", "[export]") {
    TempFile file;
    EncoderSettings settings;
    settings.width = 320;
    settings.height = 180;
    settings.rate = {60, 1};
    settings.videoBitrate = 4'000'000;
    settings.videoEncoders = {"libx264", "mpeg4"}; // por software: igual en cualquier equipo

    {
        Mp4Writer writer(file.path.string(), settings);
        CHECK_FALSE(writer.videoEncoderName().empty());
        std::vector<std::uint8_t> rgba(320 * 180 * 4);
        std::vector<float> audio(800 * 2);
        for (int i = 0; i < 60; ++i) {
            const auto g = static_cast<std::uint8_t>(grayFor(i));
            for (std::size_t p = 0; p < rgba.size(); p += 4) {
                rgba[p] = rgba[p + 1] = rgba[p + 2] = g;
                rgba[p + 3] = 255;
            }
            writer.writeVideo(rgba.data(), 320 * 4);
            for (std::size_t s = 0; s < 800; ++s) {
                const float v = 0.25f * static_cast<float>(std::sin(2.0 * 3.14159265 * 440.0 *
                                                                    (i * 800 + s) / 48000.0));
                audio[s * 2] = audio[s * 2 + 1] = v;
            }
            writer.writeAudio(audio.data(), 800);
        }
        writer.finish();
    }

    const MediaInfo info = probeMedia(file.path.string());
    REQUIRE(info.video.has_value());
    CHECK(info.video->width == 320);
    CHECK(info.video->height == 180);
    CHECK(info.video->frameRate == FrameRate{60, 1});
    CHECK(info.video->frameCount == 60);
    REQUIRE(info.audio.size() == 1);
    CHECK(info.audio[0].codec == "aac");

    VideoDecoder decoder(file.path.string(), HwAccel::None);
    for (const int target : {0, 17, 59}) {
        CAPTURE(target);
        decoder.seekToFrame(target);
        const auto frame = decoder.nextFrame();
        REQUIRE(frame.has_value());
        CHECK(frame->index == target);
        CHECK(frame->matrix == ColorMatrix::Bt709);
        CHECK_FALSE(frame->fullRange);
        const int y = frame->y[static_cast<std::size_t>(90 * 320 + 160)];
        CHECK(std::abs(y - lumaFor(grayFor(target))) <= 3);
    }

    AudioDecoder audio(file.path.string());
    std::vector<float> chunk(4096 * 2);
    std::size_t total = 0;
    while (const std::size_t n = audio.read(chunk.data(), 4096)) {
        total += n;
    }
    CHECK(std::abs(static_cast<long>(total) - 48'000) <= 2048);
}

TEST_CASE("Mp4Writer avisa si no hay codificador", "[export]") {
    TempFile file;
    EncoderSettings settings;
    settings.videoEncoders = {"codificador-que-no-existe"};
    CHECK_THROWS_AS(Mp4Writer(file.path.string(), settings), MediaError);
}

TEST_CASE("Mp4Writer escribe H.265 sin audio", "[export]") {
    if (!avcodec_find_encoder_by_name("libx265")) {
        SKIP("FFmpeg sin libx265");
    }
    TempFile file;
    EncoderSettings settings;
    settings.width = 320;
    settings.height = 180;
    settings.rate = {30, 1};
    settings.audio = false;
    settings.codec = VideoCodec::H265;
    settings.videoBitrate = 2'000'000;
    settings.videoEncoders = {"libx265"};
    {
        Mp4Writer writer(file.path.string(), settings);
        CHECK(writer.videoEncoderName() == "libx265");
        std::vector<std::uint8_t> rgba(320 * 180 * 4, 128);
        for (int i = 0; i < 30; ++i) {
            writer.writeVideo(rgba.data(), 320 * 4);
        }
        writer.finish();
    }
    const MediaInfo info = probeMedia(file.path.string());
    REQUIRE(info.video.has_value());
    CHECK(info.video->codec == "hevc");
    CHECK(info.video->frameCount == 30);
    CHECK(info.audio.empty());
}
