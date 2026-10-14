#pragma once

// Modelo de tiempo de SharkCut.
//
// Todos los tiempos se representan como enteros en una base de tiempo racional
// (p. ej. fotogramas a 60/1 fps, ticks de 1/90000 s de un MP4, microsegundos).
// Nunca se usan segundos en coma flotante para posiciones ni duraciones, para
// evitar desfases acumulados entre video y audio.

#include <cstdint>
#include <string>

namespace sharkcut {

// Fracción num/den. Como base de tiempo, una unidad equivale a num/den segundos.
struct Rational {
    std::int64_t num = 0;
    std::int64_t den = 1;

    friend constexpr bool operator==(const Rational&, const Rational&) = default;
};

// Microsegundos: base de tiempo interna de referencia.
inline constexpr Rational kMicroseconds{1, 1'000'000};

enum class Rounding {
    Nearest, // Al más cercano; los empates se alejan de cero.
    Down,    // Hacia menos infinito.
    Up,      // Hacia más infinito.
};

// Calcula a * b / c con un producto intermedio de 128 bits, sin desbordar.
// Requiere c > 0. Lanza std::overflow_error si el resultado no cabe en 64 bits.
std::int64_t mulDiv(std::int64_t a, std::int64_t b, std::int64_t c,
                    Rounding rounding = Rounding::Nearest);

// Convierte `value`, expresado en unidades de la base `from`, a la base `to`.
// Ambas bases deben tener numerador y denominador en (0, 2^31).
// Lanza std::invalid_argument si alguna base no es válida.
std::int64_t rescale(std::int64_t value, Rational from, Rational to,
                     Rounding rounding = Rounding::Nearest);

// Velocidad de fotogramas como fracción (60/1, 30000/1001…).
struct FrameRate {
    std::int64_t num = 60;
    std::int64_t den = 1;

    // Duración de un fotograma, usable como base de tiempo.
    constexpr Rational frameDuration() const { return {den, num}; }

    // Fotogramas por segundo redondeados al entero más cercano (60, 30, 24…).
    constexpr std::int64_t nominal() const { return (num + den / 2) / den; }

    friend constexpr bool operator==(const FrameRate&, const FrameRate&) = default;
};

// Instante de inicio del fotograma `frame`, en microsegundos.
std::int64_t frameToMicroseconds(std::int64_t frame, FrameRate rate);

// Fotograma que se ve en el instante `us`: el último que empieza en o antes de `us`.
std::int64_t microsecondsToFrame(std::int64_t us, FrameRate rate);

// Código de tiempo "HH:MM:SS:FF" (sin drop-frame), usando los fps nominales.
// Los valores negativos llevan el prefijo "-".
std::string formatTimecode(std::int64_t frame, FrameRate rate);

} // namespace sharkcut
