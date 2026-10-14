#include "core/time.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>

using namespace sharkcut;

namespace {
constexpr FrameRate k60{60, 1};
constexpr FrameRate kNtsc{30000, 1001};
constexpr Rational kMpegTicks{1, 90'000};
constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
} // namespace

TEST_CASE("mulDiv calcula productos exactos", "[time]") {
    CHECK(mulDiv(10, 3, 2) == 15);
    CHECK(mulDiv(0, 12345, 7) == 0);
    CHECK(mulDiv(-10, 3, 2) == -15);
    CHECK(mulDiv(10, -3, 2) == -15);
    CHECK(mulDiv(-10, -3, 2) == 15);
}

TEST_CASE("mulDiv redondea segun el modo", "[time]") {
    // 7 / 2 = 3,5
    CHECK(mulDiv(7, 1, 2, Rounding::Nearest) == 4);
    CHECK(mulDiv(7, 1, 2, Rounding::Down) == 3);
    CHECK(mulDiv(7, 1, 2, Rounding::Up) == 4);
    // -7 / 2 = -3,5
    CHECK(mulDiv(-7, 1, 2, Rounding::Nearest) == -4);
    CHECK(mulDiv(-7, 1, 2, Rounding::Down) == -4);
    CHECK(mulDiv(-7, 1, 2, Rounding::Up) == -3);
    // 10 / 3 = 3,33…
    CHECK(mulDiv(10, 1, 3, Rounding::Nearest) == 3);
    CHECK(mulDiv(10, 1, 3, Rounding::Up) == 4);
    CHECK(mulDiv(-10, 1, 3, Rounding::Down) == -4);
}

TEST_CASE("mulDiv usa 128 bits intermedios sin desbordar", "[time]") {
    // El producto (≈ 8,5e37) no cabe en 64 bits, pero el resultado sí.
    CHECK(mulDiv(kMax, kMax, kMax) == kMax);
    CHECK(mulDiv(kMax, 1'000'000, 1'000'000) == kMax);
    CHECK(mulDiv(4'000'000'000'000LL, 3'000'000'000LL, 6'000'000'000LL) == 2'000'000'000'000LL);
    CHECK(mulDiv(kMin, 1, 1) == kMin);
    CHECK(mulDiv(kMin, 3, 3) == kMin);
}

TEST_CASE("mulDiv detecta resultados que no caben y divisores invalidos", "[time]") {
    CHECK_THROWS_AS(mulDiv(kMax, 2, 1), std::overflow_error);
    CHECK_THROWS_AS(mulDiv(kMin, -1, 1), std::overflow_error);
    CHECK_THROWS_AS(mulDiv(1, 1, 0), std::invalid_argument);
    CHECK_THROWS_AS(mulDiv(1, 1, -5), std::invalid_argument);
}

#if defined(__SIZEOF_INT128__)
// Referencia exacta disponible en GCC y Clang (no en MSVC).
__extension__ typedef __int128 Int128;

TEST_CASE("mulDiv coincide con __int128 en valores variados", "[time]") {
    std::uint64_t seed = 0x9E3779B97F4A7C15u;
    auto next = [&seed] {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    };
    for (int i = 0; i < 20'000; ++i) {
        const auto a = static_cast<std::int64_t>(next()) >> (next() % 40);
        const auto b = static_cast<std::int64_t>(next() >> (24 + next() % 32));
        const auto c = static_cast<std::int64_t>((next() >> (1 + next() % 40)) | 1u);
        const Int128 exact = static_cast<Int128>(a) * b;
        Int128 q = exact / c;
        const Int128 r = exact % c;
        // Redondeo al más cercano, empates lejos de cero.
        if (r != 0 && (r < 0 ? -r : r) * 2 >= c) {
            q += exact < 0 ? -1 : 1;
        }
        if (q > kMax || q < kMin) {
            CHECK_THROWS_AS(mulDiv(a, b, c), std::overflow_error);
        } else {
            REQUIRE(mulDiv(a, b, c) == static_cast<std::int64_t>(q));
        }
    }
}
#endif

TEST_CASE("rescale convierte entre bases de tiempo", "[time]") {
    // 1 segundo en ticks de MPEG (90 kHz) a microsegundos.
    CHECK(rescale(90'000, kMpegTicks, kMicroseconds) == 1'000'000);
    // Fotograma 60 a 60 fps = 1 s = 90 000 ticks.
    CHECK(rescale(60, k60.frameDuration(), kMpegTicks) == 90'000);
    // Un fotograma a 60 fps dura 16 666,6… µs.
    CHECK(rescale(1, k60.frameDuration(), kMicroseconds) == 16'667);
    CHECK(rescale(1, k60.frameDuration(), kMicroseconds, Rounding::Down) == 16'666);
}

TEST_CASE("rescale rechaza bases de tiempo invalidas", "[time]") {
    CHECK_THROWS_AS(rescale(1, {0, 1}, kMicroseconds), std::invalid_argument);
    CHECK_THROWS_AS(rescale(1, {1, 0}, kMicroseconds), std::invalid_argument);
    CHECK_THROWS_AS(rescale(1, {-1, 60}, kMicroseconds), std::invalid_argument);
    CHECK_THROWS_AS(rescale(1, kMicroseconds, {1, std::int64_t{1} << 31}), std::invalid_argument);
}

TEST_CASE("Fotogramas y microsegundos son reversibles", "[time]") {
    for (const FrameRate rate : {k60, kNtsc, FrameRate{24, 1}, FrameRate{25, 1}}) {
        for (std::int64_t frame = -200; frame <= 2'000; ++frame) {
            const std::int64_t us = frameToMicroseconds(frame, rate);
            REQUIRE(microsecondsToFrame(us, rate) == frame);
            // Justo antes del inicio del fotograma todavía se ve el anterior.
            REQUIRE(microsecondsToFrame(us - 1, rate) == frame - 1);
        }
    }
}

TEST_CASE("Un VOD de 10 horas a 60 fps no acumula desfase", "[time]") {
    const std::int64_t frames = 10LL * 3600 * 60;
    CHECK(frameToMicroseconds(frames, k60) == 36'000'000'000LL);
    CHECK(microsecondsToFrame(36'000'000'000LL, k60) == frames);
    // En NTSC (29,97 fps), 30 000 fotogramas duran exactamente 1001 s.
    CHECK(frameToMicroseconds(30'000, kNtsc) == 1'001'000'000LL);
}

TEST_CASE("formatTimecode genera HH:MM:SS:FF", "[time]") {
    CHECK(formatTimecode(0, k60) == "00:00:00:00");
    CHECK(formatTimecode(59, k60) == "00:00:00:59");
    CHECK(formatTimecode(60, k60) == "00:00:01:00");
    CHECK(formatTimecode(40 * 60 * 60, k60) == "00:40:00:00");
    CHECK(formatTimecode(3 * 3600 * 60 + 61, k60) == "03:00:01:01");
    CHECK(formatTimecode(-61, k60) == "-00:00:01:01");
    // 29,97 fps usa 30 como fps nominales (sin drop-frame).
    CHECK(kNtsc.nominal() == 30);
    CHECK(formatTimecode(30, kNtsc) == "00:00:01:00");
}
