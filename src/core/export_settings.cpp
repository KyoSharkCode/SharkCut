#include "core/export_settings.h"

#include <algorithm>
#include <cmath>

namespace sharkcut {

namespace {
// Una muestra del audio interno, 48 kHz (kAudioSampleDuration en media/audio_decoder.h;
// el núcleo no depende de media).
constexpr Rational kSample{1, 48'000};
} // namespace

std::int64_t ExportTiming::frameCount() const {
    if (end <= start) {
        return 0;
    }
    return rescale(end - start, sequenceRate.frameDuration(), outputRate.frameDuration(),
                   Rounding::Up);
}

std::int64_t ExportTiming::sequenceFrame(std::int64_t frame) const {
    const std::int64_t offset =
        rescale(frame, outputRate.frameDuration(), sequenceRate.frameDuration(), Rounding::Down);
    return std::min(start + offset, end - 1);
}

std::int64_t ExportTiming::audioStart() const {
    return rescale(start, sequenceRate.frameDuration(), kSample);
}

std::int64_t ExportTiming::audioSamplesBefore(std::int64_t frame) const {
    return rescale(frame, outputRate.frameDuration(), kSample);
}

ExportSize exportSize(int canvasWidth, int canvasHeight, double scale) {
    auto even = [scale](int side) {
        const auto v = static_cast<int>(std::lround(side * scale / 2.0)) * 2;
        return std::max(2, v);
    };
    return {even(canvasWidth), even(canvasHeight), scale};
}

std::vector<ExportSize> exportSizes(int canvasWidth, int canvasHeight) {
    std::vector<ExportSize> sizes;
    if (canvasWidth <= 0 || canvasHeight <= 0) {
        return sizes;
    }
    for (const double scale : {2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5}) {
        const ExportSize size = exportSize(canvasWidth, canvasHeight, scale);
        const int longSide = std::max(size.width, size.height);
        const int shortSide = std::min(size.width, size.height);
        if (scale != 1.0 && (longSide > 4096 || shortSide < 240)) {
            continue;
        }
        sizes.push_back(size);
    }
    return sizes;
}

ExportPresetChoice exportPreset(ExportPreset preset, int canvasWidth, int canvasHeight,
                                FrameRate rate) {
    ExportPresetChoice choice;
    const std::vector<ExportSize> sizes = exportSizes(canvasWidth, canvasHeight);
    if (sizes.empty()) {
        return choice;
    }
    if (preset == ExportPreset::YouTube) {
        if (std::min(canvasWidth, canvasHeight) >= 1440) {
            return choice; // ya es 1440p o más
        }
        // La más pequeña que llega a 1440; si ninguna llega, la mayor.
        const ExportSize* best = nullptr;
        for (const ExportSize& size : sizes) {
            const bool reaches = std::min(size.width, size.height) >= 1440;
            if (reaches && (!best || size.scale < best->scale)) {
                best = &size;
            }
        }
        if (!best) {
            best = &*std::max_element(
                sizes.begin(), sizes.end(),
                [](const ExportSize& a, const ExportSize& b) { return a.scale < b.scale; });
        }
        choice.scale = best->scale;
        return choice;
    }
    // TikTok: 1080 de ancho en vertical. Si la secuencia no es vertical, a su tamaño (se
    // avisa; no se recorta ni se cambia la forma).
    choice.notVertical = canvasHeight <= canvasWidth;
    const ExportSize* best = &sizes.front();
    for (const ExportSize& size : sizes) {
        if (choice.notVertical) {
            if (size.scale == 1.0) {
                best = &size;
            }
            continue;
        }
        const int side = std::min(size.width, size.height);
        const int bestSide = std::min(best->width, best->height);
        if (std::abs(side - 1080) < std::abs(bestSide - 1080)) {
            best = &size;
        }
    }
    choice.scale = best->scale;
    const double fps = static_cast<double>(rate.num) / static_cast<double>(rate.den);
    const bool usual = std::abs(fps - 30.0) < 0.1 || std::abs(fps - 60.0) < 0.1 ||
                       std::abs(fps - 29.97) < 0.01 || std::abs(fps - 59.94) < 0.01;
    choice.fps = usual ? 0 : (fps < 45.0 ? 30 : 60);
    return choice;
}

std::int64_t recommendedBitrate(int width, int height, FrameRate rate, int quality,
                                VideoCodec codec) {
    static constexpr double kBase[kExportQualityCount] = {8e6, 16e6, 40e6};
    const double base = kBase[std::clamp(quality, 0, kExportQualityCount - 1)];
    const double pixels = static_cast<double>(width) * height / (1920.0 * 1080.0);
    const double fps = static_cast<double>(rate.num) / static_cast<double>(rate.den);
    const double motion = 0.4 + 0.6 * std::clamp(fps / 60.0, 0.25, 2.0);
    double bits = base * pixels * motion;
    if (codec == VideoCodec::H265) {
        bits *= 0.65;
    }
    // Múltiplos de 0,5 Mbps, como mínimo 1 Mbps.
    const double rounded = std::max(1.0, std::round(bits / 5e5) * 0.5);
    return static_cast<std::int64_t>(rounded * 1e6);
}

} // namespace sharkcut
