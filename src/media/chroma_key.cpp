#include "media/chroma_key.h"

#include "media/color.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace sharkcut {

namespace {

constexpr int kHueBins = 36;
constexpr double kMinSaturation = 0.08; // tono (Cb, Cr) mínimo para no ser un gris

struct Sample {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    int bin = 0;
};

} // namespace

std::optional<std::uint32_t> detectKeyColor(const VideoFrame& frame) {
    if (frame.width < 4 || frame.height < 4 || frame.y.empty() || frame.uv.empty()) {
        return std::nullopt;
    }
    const std::array<float, 16> m = yuvToRgbMatrix(frame.matrix, frame.fullRange);
    const int bandX = std::max(2, frame.width / 12);
    const int bandY = std::max(2, frame.height / 12);
    const int step = std::max(2, std::min(frame.width, frame.height) / 120 * 2); // par

    std::vector<Sample> samples;
    int total = 0;
    std::array<int, kHueBins> counts{};
    for (int y = 0; y < frame.height; y += step) {
        const bool edgeRow = y < bandY || y >= frame.height - bandY;
        for (int x = 0; x < frame.width; x += step) {
            if (!edgeRow && x >= bandX && x < frame.width - bandX) {
                continue;
            }
            const std::size_t uvIndex =
                (static_cast<std::size_t>(y / 2) * frame.uvWidth() + x / 2) * 2;
            const double yy = frame.y[static_cast<std::size_t>(y) * frame.width + x] / 255.0;
            const double u = frame.uv[uvIndex] / 255.0;
            const double v = frame.uv[uvIndex + 1] / 255.0;
            const auto channel = [&](int row) {
                return std::clamp(m[row * 4] * yy + m[row * 4 + 1] * u + m[row * 4 + 2] * v +
                                      m[row * 4 + 3],
                                  0.0, 1.0);
            };
            const double r = channel(0);
            const double g = channel(1);
            const double b = channel(2);
            ++total;
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            const double cb = (b - luma) / 1.8556;
            const double cr = (r - luma) / 1.5748;
            if (std::hypot(cb, cr) < kMinSaturation) {
                continue;
            }
            const double turn = (std::atan2(cr, cb) + std::numbers::pi) / (2.0 * std::numbers::pi);
            const int bin = std::min(kHueBins - 1, static_cast<int>(turn * kHueBins));
            ++counts[static_cast<std::size_t>(bin)];
            samples.push_back({r, g, b, bin});
        }
    }
    if (samples.empty()) {
        return std::nullopt;
    }

    // El tono más repetido, sumando sus vecinos (un fondo con sombras ocupa varios).
    const auto around = [&counts](int bin) {
        int sum = 0;
        for (int d = -1; d <= 1; ++d) {
            sum += counts[static_cast<std::size_t>((bin + d + kHueBins) % kHueBins)];
        }
        return sum;
    };
    int best = 0;
    for (int bin = 1; bin < kHueBins; ++bin) {
        if (around(bin) > around(best)) {
            best = bin;
        }
    }
    // Tiene que ocupar al menos un tercio del borde para considerarlo el fondo.
    if (around(best) * 3 < total) {
        return std::nullopt;
    }

    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    int n = 0;
    for (const Sample& s : samples) {
        const int distance = std::abs(s.bin - best);
        if (std::min(distance, kHueBins - distance) <= 1) {
            r += s.r;
            g += s.g;
            b += s.b;
            ++n;
        }
    }
    const auto byte = [n](double sum) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(sum / n, 0.0, 1.0) * 255.0));
    };
    return (byte(r) << 16) | (byte(g) << 8) | byte(b);
}

} // namespace sharkcut
