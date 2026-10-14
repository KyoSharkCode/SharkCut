#pragma once

#include "audio/ring_buffer.h"

#include <cstdint>
#include <memory>
#include <string>

namespace sharkcut {

// Salida de audio (miniaudio; WASAPI en Windows) en el formato interno: float estéreo
// a 48 kHz. El dispositivo lee del búfer en anillo desde su hilo de tiempo real.
//
// Además es el reloj maestro de la reproducción: cuenta las muestras que ya han
// salido por los altavoces, descontando la latencia del dispositivo.
class AudioOutput {
public:
    struct Clock {
        std::int64_t sample = 0;     // muestra del clip que suena ahora
        std::uint64_t requestId = 0; // reproducción a la que corresponde (ver reset)
    };

    enum class Device {
        Default, // salida predeterminada del sistema
        Null,    // sin sonido, pero consume el audio a velocidad real (tests)
    };

    // Abre el dispositivo de salida. Lanza std::runtime_error si falla.
    explicit AudioOutput(Device device = Device::Default);
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    std::string backendName() const; // p. ej. "WASAPI"
    std::string deviceName() const;
    int latencyFrames() const;

    // Búfer del que lee el dispositivo. Solo debe escribir en él un hilo (el productor).
    AudioRingBuffer& buffer();

    // Prepara una nueva posición: deja de sonar, vacía el búfer y coloca el reloj en
    // `baseSample`. Solo desde el hilo productor.
    void reset(std::int64_t baseSample, std::uint64_t requestId);

    // Con false el dispositivo emite silencio y el reloj se detiene.
    void setPlaying(bool playing);
    bool playing() const;

    void setVolume(float volume); // 0 = silencio, 1 = original

    Clock clock() const;

    // Picos (valor absoluto, 0–1 o más si satura) de cada canal desde la última llamada,
    // medidos antes del volumen: el medidor de la mezcla se ve aunque esté silenciado.
    struct Levels {
        float left = 0.0f;
        float right = 0.0f;
    };
    Levels takePeakLevels();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace sharkcut
