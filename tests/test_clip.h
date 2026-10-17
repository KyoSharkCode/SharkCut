#pragma once

// Generador de clips de prueba para los tests del motor de medios.
//
// Cada fotograma es plano y lleva su número codificado en la luminancia:
// la mitad izquierda vale levelFor(index % 16) y la derecha levelFor(index / 16).
// Así se puede comprobar qué fotograma devolvió el decodificador.

#include "core/time.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace sharkcut::test {

struct ClipOptions {
    int frames = 120;
    int width = 320;
    int height = 180;
    FrameRate rate{60, 1};
    int gopSize = 30;            // distancia entre fotogramas clave
    bool fragmented = false;     // MP4 fragmentado, como graba OBS
    bool packetDurations = true; // false: paquetes sin duración (desvía los fps medios)

    // Pista de audio a 48 kHz estéreo. Pcm exige contenedor "matroska".
    enum class Audio { None, Pcm, Aac };
    Audio audio = Audio::None;
    std::string container = "mp4";
    int audioDelayFrames = 0; // el audio empieza este número de fotogramas más tarde
    // Codificador de video concreto (p. ej. "libsvtav1"); vacío = H.264 o MPEG-4.
    std::string videoEncoder{};
};

// Escribe el clip en `path`. Devuelve el nombre del codificador usado ("libx264",
// "mpeg4"…), o una cadena vacía si `videoEncoder` no está disponible (sin escribir nada).
std::string writeTestClip(const std::filesystem::path& path, const ClipOptions& options);

// Valor (float, ambos canales) de las muestras de audio PCM durante el fotograma `index`.
float audioLevelFor(int index);

// Nivel de luminancia (rango limitado) que representa el dígito `digit` (0–15).
int levelFor(int digit);

// Índice de fotograma reconstruido a partir de los niveles medidos, o -1 si no cuadra.
int decodeIndex(int leftLevel, int rightLevel);

// Ruta única en la carpeta temporal del sistema.
std::filesystem::path tempClipPath(const std::string& name);

} // namespace sharkcut::test
