#include "core/export_settings.h"
#include "core/time.h"

#include <catch2/catch_test_macros.hpp>

using namespace sharkcut;

TEST_CASE("parseTimecode lee codigos completos y cortos", "[time]") {
    const FrameRate r60{60, 1};
    CHECK(parseTimecode("00:00:00:00", r60) == 0);
    CHECK(parseTimecode("00:01:02:03", r60) == (62 * 60 + 3));
    CHECK(parseTimecode("01:00:00:00", r60) == 3600 * 60);
    CHECK(parseTimecode("1:02:03", r60) == (62 * 60 + 3)); // MM:SS:FF
    CHECK(parseTimecode("5:10", r60) == 5 * 60 + 10);      // SS:FF
    CHECK(parseTimecode("90", r60) == 90);                 // solo fotogramas
    CHECK(parseTimecode(" 00.00.01.00 ", r60) == 60);
    CHECK(parseTimecode("00:00:00:90", r60) == 90); // campos que se pasan
    // Ida y vuelta con formatTimecode, también a 30000/1001 (fps nominales).
    const FrameRate ntsc{30000, 1001};
    CHECK(parseTimecode(formatTimecode(123456, ntsc), ntsc) == 123456);
    CHECK(parseTimecode(formatTimecode(987654, r60), r60) == 987654);
}

TEST_CASE("parseTimecode rechaza textos que no son tiempos", "[time]") {
    const FrameRate r60{60, 1};
    CHECK_FALSE(parseTimecode("", r60));
    CHECK_FALSE(parseTimecode("abc", r60));
    CHECK_FALSE(parseTimecode("00::01", r60));
    CHECK_FALSE(parseTimecode("1:2:3:4:5", r60));
    CHECK_FALSE(parseTimecode("-00:00:01:00", r60));
    CHECK_FALSE(parseTimecode("00:00:01:", r60));
}

TEST_CASE("ExportTiming con los mismos fps copia el tramo", "[export]") {
    ExportTiming t{{60, 1}, {60, 1}, 120, 300};
    CHECK(t.frameCount() == 180);
    CHECK(t.sequenceFrame(0) == 120);
    CHECK(t.sequenceFrame(179) == 299);
    CHECK(t.audioStart() == 2 * 48'000);
    CHECK(t.audioSamplesBefore(60) == 48'000);
}

TEST_CASE("ExportTiming a menos fps salta fotogramas y a mas repite", "[export]") {
    ExportTiming half{{60, 1}, {30, 1}, 0, 61};
    CHECK(half.frameCount() == 31); // cubre también el último fotograma suelto
    CHECK(half.sequenceFrame(0) == 0);
    CHECK(half.sequenceFrame(1) == 2);
    CHECK(half.sequenceFrame(30) == 60);

    ExportTiming twice{{30, 1}, {60, 1}, 10, 20};
    CHECK(twice.frameCount() == 20);
    CHECK(twice.sequenceFrame(0) == 10);
    CHECK(twice.sequenceFrame(1) == 10);
    CHECK(twice.sequenceFrame(19) == 19);

    // 60 → 29,97: nunca se sale del tramo.
    ExportTiming ntsc{{60, 1}, {30000, 1001}, 0, 600};
    for (std::int64_t f = 0; f < ntsc.frameCount(); ++f) {
        CHECK(ntsc.sequenceFrame(f) < 600);
    }
    CHECK(ntsc.frameCount() == 300);

    ExportTiming empty{{60, 1}, {60, 1}, 50, 50};
    CHECK(empty.frameCount() == 0);
}

TEST_CASE("exportSizes ofrece escalas con la misma forma", "[export]") {
    const auto horizontal = exportSizes(1920, 1080);
    REQUIRE(horizontal.size() == 5);
    CHECK(horizontal[0].width == 3840);
    CHECK(horizontal[0].height == 2160);
    CHECK(horizontal[1].width == 2560);
    CHECK(horizontal[1].height == 1440);
    CHECK(horizontal[2].width == 1920);
    CHECK(horizontal[2].scale == 1.0);
    CHECK(horizontal[3].width == 1280);
    CHECK(horizontal[3].height == 720);
    CHECK(horizontal[4].width == 960);
    CHECK(horizontal[4].height == 540);

    const auto vertical = exportSizes(1080, 1920);
    CHECK(vertical[0].width == 2160);
    CHECK(vertical[0].height == 3840);

    // Más de 4096: sin 200 %; lados siempre pares.
    const auto big = exportSizes(2560, 1440);
    CHECK(big[0].width == 3414);
    CHECK(big[0].height == 1920);
    for (const ExportSize& s : exportSizes(1366, 769)) {
        CHECK(s.width % 2 == 0);
        CHECK(s.height % 2 == 0);
    }
    CHECK(exportSizes(0, 1080).empty());
}

TEST_CASE("recommendedBitrate escala con pixeles fps y codec", "[export]") {
    const FrameRate r60{60, 1};
    CHECK(recommendedBitrate(1920, 1080, r60, 0, VideoCodec::H264) == 8'000'000);
    CHECK(recommendedBitrate(1920, 1080, r60, 1, VideoCodec::H264) == 16'000'000);
    CHECK(recommendedBitrate(1920, 1080, r60, 2, VideoCodec::H264) == 40'000'000);
    CHECK(recommendedBitrate(1080, 1920, r60, 1, VideoCodec::H264) == 16'000'000);
    CHECK(recommendedBitrate(3840, 2160, r60, 1, VideoCodec::H264) == 64'000'000);
    CHECK(recommendedBitrate(1920, 1080, {30, 1}, 1, VideoCodec::H264) == 11'000'000);
    CHECK(recommendedBitrate(1920, 1080, r60, 1, VideoCodec::H265) == 10'500'000);
    CHECK(recommendedBitrate(16, 16, r60, 0, VideoCodec::H264) == 1'000'000);
}
