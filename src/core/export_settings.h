#pragma once

#include "core/time.h"

#include <cstdint>
#include <vector>

namespace sharkcut {

// Cálculos de la exportación que no dependen de la GPU ni de los codificadores.

enum class VideoCodec {
    H264, // compatible con todo
    H265, // HEVC: el mismo aspecto con menos bits (YouTube, móviles recientes)
};

// Calidad: Ligera, Alta, Máxima.
inline constexpr int kExportQualityCount = 3;

// Qué fotograma de la secuencia corresponde a cada fotograma exportado cuando se
// exporta un tramo [start, end) de la secuencia, quizá a otros fps (a menos fps se
// salta fotogramas; a más, repite).
struct ExportTiming {
    FrameRate sequenceRate{60, 1};
    FrameRate outputRate{60, 1};
    std::int64_t start = 0; // fotogramas de la secuencia
    std::int64_t end = 0;

    // Fotogramas que tendrá el archivo (cubren todo el tramo).
    std::int64_t frameCount() const;
    // Fotograma de la secuencia que se ve en el fotograma exportado `frame`.
    std::int64_t sequenceFrame(std::int64_t frame) const;
    // Muestra de audio de la secuencia donde empieza el tramo.
    std::int64_t audioStart() const;
    // Muestras de audio (desde el principio del archivo) hasta el inicio del fotograma
    // exportado `frame`.
    std::int64_t audioSamplesBefore(std::int64_t frame) const;
};

struct ExportSize {
    int width = 0;
    int height = 0;
    double scale = 1.0; // respecto al lienzo de la secuencia
};

// Resoluciones que se ofrecen para un lienzo: la suya y escalas con la misma forma
// (200 %, 133 %, 100 %, 67 %, 50 %), con lados pares y sin pasar de 4096 ni bajar de 240.
std::vector<ExportSize> exportSizes(int canvasWidth, int canvasHeight);

// Tamaño exportado con una escala del lienzo (lados pares).
ExportSize exportSize(int canvasWidth, int canvasHeight, double scale);

// Tasa de bits recomendada (bits por segundo) para una calidad (0 Ligera, 1 Alta,
// 2 Máxima): 8, 16 y 40 Mbps en H.264 a 1080p60, y proporcional a los píxeles y (más
// suave) a los fps. H.265 necesita un tercio menos para verse igual.
std::int64_t recommendedBitrate(int width, int height, FrameRate rate, int quality,
                                VideoCodec codec);

} // namespace sharkcut
