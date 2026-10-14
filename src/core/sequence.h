#pragma once

// Secuencia de prueba de la fase 1: clip A → transición → clip B.
//
// Cada clip es un tramo [in, out) de un archivo, en fotogramas del propio archivo.
// La transición solapa los últimos fotogramas de A con los primeros de B:
//
//   A: |■■■■■■■■■■■■■■■■■■■■|
//   B:                 |■■■■■■■■■■■■■■■■■■|
//                      └ transición ┘
//
// La línea de tiempo dura len(A) + len(B) − duración de la transición.

#include <cstdint>
#include <optional>

namespace sharkcut {

struct ClipRange {
    std::int64_t in = 0;  // primer fotograma incluido
    std::int64_t out = 0; // primer fotograma excluido
    std::int64_t length() const { return out > in ? out - in : 0; }
};

struct SequenceLayout {
    ClipRange a;
    ClipRange b;
    std::int64_t transitionFrames = 0; // ya limitada a lo que cabe (ver makeLayout)

    std::int64_t length() const { return a.length() + b.length() - transitionFrames; }
    std::int64_t transitionStart() const { return a.length() - transitionFrames; }
    std::int64_t transitionEnd() const { return a.length(); }
    // Primer fotograma de la línea de tiempo en el que aparece B.
    std::int64_t bStart() const { return transitionStart(); }

    struct At {
        std::optional<std::int64_t> aFrame;          // fotograma del archivo A que se ve
        std::optional<std::int64_t> bFrame;          // fotograma del archivo B que se ve
        std::optional<std::int64_t> transitionFrame; // 0 … transitionFrames − 1
    };
    // Qué se ve en el fotograma `frame` de la línea de tiempo (limitado al rango).
    At at(std::int64_t frame) const;
};

// Crea la disposición limitando la transición: como mucho la longitud del clip más
// corto, y al menos 1 fotograma si los dos clips tienen contenido.
SequenceLayout makeLayout(ClipRange a, ClipRange b, std::int64_t transitionFrames);

} // namespace sharkcut
