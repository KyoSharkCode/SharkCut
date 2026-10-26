#pragma once

// Forma de onda de un medio para dibujarla en la línea de tiempo.
//
// Se guarda el pico de cada bloque de 5 ms (240 muestras a 48 kHz, los dos canales
// juntos) en una escala logarítmica de 0 a 255: 0 = −60 dB o menos, 255 = 0 dB. Así se
// ven también las partes flojas (voz), y un VOD de 40 minutos ocupa menos de 0,5 MB.
// El bloque 0 empieza en la muestra 0 del AudioDecoder (el primer fotograma del video).

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sharkcut {

struct Waveform {
    static constexpr int kBlockSamples = 240;    // muestras por bloque
    static constexpr int kBlocksPerSecond = 200; // 48 000 / 240
    static constexpr double kFloorDb = -60.0;

    std::vector<std::uint8_t> peaks;

    // Valor guardado para un pico lineal (0–1 o más).
    static std::uint8_t encode(float peak);
    // Altura para dibujar (0–1) con `gainDb` aplicada: raíz de la amplitud, un punto medio
    // entre la escala lineal (la voz casi no se ve) y la de dB (todo parece lleno).
    // 0 = −60 dB o menos, 1 = 0 dB; −12 dB llega a la mitad.
    static double level(std::uint8_t value, double gainDb = 0.0);

    friend bool operator==(const Waveform&, const Waveform&) = default;
};

// Decodifica todo el audio del archivo (UTF-8). Lanza MediaError si no tiene audio o
// no se puede leer. Con `cancel` a true se abandona (y también lanza MediaError).
// `parts`: en cuántos tramos se reparte, cada uno en su hilo; 0 = según la duración (un
// tramo por minuto, como mucho 4 y no más que núcleos), 1 = todo seguido.
Waveform computeWaveform(const std::string& path, const std::atomic<bool>* cancel = nullptr,
                         int parts = 0);

// Caché en disco. Se escribe con un temporal que se renombra al terminar.
bool saveWaveform(const Waveform& waveform, const std::filesystem::path& file);
std::optional<Waveform> loadWaveform(const std::filesystem::path& file);

// Nombre del archivo de caché de un medio: cambia si el archivo cambia (ruta, tamaño
// o fecha de modificación). Vacío si el archivo no existe.
std::string waveformCacheName(const std::string& mediaPath);

} // namespace sharkcut
