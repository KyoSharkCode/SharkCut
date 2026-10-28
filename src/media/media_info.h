#pragma once

#include "core/time.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sharkcut {

struct VideoStreamInfo {
    int streamIndex = -1;
    std::string codec;       // p. ej. "h264", "hevc"
    std::string pixelFormat; // p. ej. "yuv420p", "nv12"
    int width = 0;
    int height = 0;
    FrameRate frameRate; // fps del flujo (60/1, 30000/1001…)
    Rational timeBase;   // base de tiempo de los pts del flujo
    std::int64_t frameCount = 0;
    bool variableFrameRate = false; // indicio de VFR (fotogramas variables)
    // Imagen fija (PNG, JPG…): un solo fotograma, sin duración propia. VideoDecoder la
    // lee una vez y la entrega como fotograma 0 (no se puede saltar dentro de ella).
    bool still = false;
};

struct AudioStreamInfo {
    int streamIndex = -1;
    std::string codec;
    int sampleRate = 0;
    int channels = 0;
};

struct MediaInfo {
    std::string container; // p. ej. "mov,mp4,m4a,3gp,3g2,mj2"
    std::int64_t durationUs = 0;
    std::optional<VideoStreamInfo> video;
    std::vector<AudioStreamInfo> audio;
};

// Analiza un archivo (ruta en UTF-8) sin decodificarlo. Lanza MediaError si falla.
MediaInfo probeMedia(const std::string& path);

// FFmpeg solo escribe sus errores (en stderr, que va al registro de la sesión): sus avisos
// sobre metadatos que no entiende o referencias de H.264 que se arreglan solas llenaban el
// registro de cientos de líneas sin importancia.
void quietMediaLog();

} // namespace sharkcut
