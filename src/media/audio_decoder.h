#pragma once

#include "media/audio_source.h"
#include "media/media_info.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace sharkcut {

// Formato de audio interno de SharkCut: float de 32 bits, estéreo intercalado, 48 kHz.
inline constexpr int kAudioSampleRate = 48'000;
inline constexpr int kAudioChannels = 2;
inline constexpr Rational kAudioSampleDuration{1, kAudioSampleRate};

// Decodifica el flujo de audio principal de un archivo y lo entrega en el formato
// interno. La muestra 0 coincide con el primer fotograma del video del mismo archivo:
// si el audio empieza más tarde se rellena con silencio y si empieza antes se recorta.
//
// No es seguro usar una misma instancia desde varios hilos a la vez.
class AudioDecoder : public AudioSource {
public:
    // Abre `path` (UTF-8). Lanza MediaError si no se puede abrir o no tiene audio.
    explicit AudioDecoder(const std::string& path);
    ~AudioDecoder() override;

    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    const AudioStreamInfo& info() const override;

    // Escribe hasta `frames` muestras por canal (frames × 2 floats) en `out`.
    // Devuelve cuántas escribió; 0 significa fin del audio.
    std::size_t read(float* out, std::size_t frames) override;

    // Coloca el decodificador para que read() empiece exactamente en `sample`.
    void seekToSample(std::int64_t sample) override;

    // Muestra que devolverá la próxima llamada a read().
    std::int64_t position() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace sharkcut
