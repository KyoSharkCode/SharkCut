#pragma once

#include "core/export_settings.h"
#include "core/time.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sharkcut {

struct EncoderSettings {
    int width = 1920;
    int height = 1080;
    FrameRate rate{60, 1};
    bool audio = true;          // pista AAC estéreo a 48 kHz
    int audioBitrate = 192'000; // bits por segundo
    VideoCodec codec = VideoCodec::H264;
    std::int64_t videoBitrate = 16'000'000; // bits por segundo (objetivo)
    // Codificadores de video a probar, en orden; vacío = los predeterminados del códec
    // (por hardware NVIDIA, AMD, Intel y, de respaldo, x264 o x265 por software).
    std::vector<std::string> videoEncoders;
};

// Codificador de video que usaría la exportación con ese códec: el primero de los
// predeterminados (por hardware NVIDIA, AMD, Intel; si no, x264 o x265) que se puede abrir
// en este equipo. Vacío si no hay ninguno. Abre codificadores: tarda (fuera del hilo de la
// interfaz).
std::string probeVideoEncoder(VideoCodec codec);
// El respaldo por software (x264 o x265), sin GPU.
bool isSoftwareEncoder(const std::string& name);

// Escribe un MP4 (H.264 o H.265 + AAC) a partir de fotogramas RGBA y audio float estéreo.
// Prueba los codificadores por hardware y usa el primero que funcione.
//
// Color: el RGB se convierte a YUV BT.709 de rango limitado, lo estándar en video.
class Mp4Writer {
public:
    // Lanza MediaError si no se puede crear el archivo o ningún codificador funciona.
    Mp4Writer(const std::string& path, const EncoderSettings& settings);
    ~Mp4Writer();

    Mp4Writer(const Mp4Writer&) = delete;
    Mp4Writer& operator=(const Mp4Writer&) = delete;

    std::string videoEncoderName() const;

    // Añade el siguiente fotograma (RGBA de 8 bits, width × height, filas de `stride` bytes).
    void writeVideo(const std::uint8_t* rgba, int stride);

    // Añade audio (float estéreo intercalado a 48 kHz).
    void writeAudio(const float* samples, std::size_t frames);

    // Vacía los codificadores y cierra el archivo. Obligatorio para un MP4 válido.
    void finish();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace sharkcut
