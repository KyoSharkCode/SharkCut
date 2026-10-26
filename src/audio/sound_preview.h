#pragma once

#include "audio/audio_output.h"

#include <memory>
#include <string>

namespace sharkcut {

// Escucha de un efecto de sonido de la biblioteca (pestaña Sonidos), aparte de la
// reproducción de la secuencia: tiene su propia salida de audio.
//
// Seguro entre hilos. play() lee y decodifica el archivo entero (los efectos son
// cortos): fuera del hilo de la interfaz.
class SoundPreview {
public:
    // Abre la salida (la predeterminada o la muda). Lanza std::runtime_error si falla.
    explicit SoundPreview(AudioOutput::Device device = AudioOutput::Device::Default);
    ~SoundPreview();

    SoundPreview(const SoundPreview&) = delete;
    SoundPreview& operator=(const SoundPreview&) = delete;

    // Suena `path` (UTF-8) desde el principio; corta lo que sonara antes. Lanza
    // std::runtime_error si no se puede abrir.
    void play(const std::string& path);
    void stop();
    // true mientras suena (false al acabar o tras stop()).
    bool playing() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace sharkcut
