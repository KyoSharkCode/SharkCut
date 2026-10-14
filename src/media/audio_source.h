#pragma once

#include "media/media_info.h"

#include <cstddef>
#include <cstdint>

namespace sharkcut {

// Fuente de audio en el formato interno (float estéreo intercalado a 48 kHz):
// un archivo (AudioDecoder) o una mezcla de varios (SequenceAudioSource).
class AudioSource {
public:
    virtual ~AudioSource() = default;

    virtual const AudioStreamInfo& info() const = 0;

    // Escribe hasta `frames` muestras por canal en `out`; 0 significa fin.
    virtual std::size_t read(float* out, std::size_t frames) = 0;

    // La próxima lectura empieza exactamente en `sample`.
    virtual void seekToSample(std::int64_t sample) = 0;
};

} // namespace sharkcut
