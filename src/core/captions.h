#pragma once

// Subtítulos: división de una transcripción en subtítulos (como «Crear subtítulos» de
// Premiere: caracteres por línea, una o dos líneas o una sola palabra, duración mínima y
// separación) y archivos .srt.

#include "core/project.h"
#include "core/time.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sharkcut {

// Palabra transcrita con su tramo en microsegundos de la línea de tiempo.
struct TimedWord {
    std::int64_t startUs = 0;
    std::int64_t endUs = 0;
    std::string text;

    friend bool operator==(const TimedWord&, const TimedWord&) = default;
};

struct CaptionLayout {
    enum class Lines { Single, Double, Word };
    Lines lines = Lines::Double;
    int maxChars = 42; // por línea
    std::int64_t minDurationUs = 3'000'000;
    std::int64_t gapFrames = 0; // separación entre subtítulos seguidos
    bool removePunctuation = false;

    friend bool operator==(const CaptionLayout&, const CaptionLayout&) = default;
};

// Silencio a partir del cual un subtítulo termina aunque quepan más palabras.
inline constexpr std::int64_t kCaptionPauseUs = 1'000'000;

// Divide las palabras (en orden) en subtítulos, sin solaparse y con sus palabras. Los
// subtítulos terminan también al final de cada frase. Los ids quedan a 0.
std::vector<Caption> layoutCaptions(const std::vector<TimedWord>& words,
                                    const CaptionLayout& layout, FrameRate rate);

// Las palabras de una pista, en la línea de tiempo. Los subtítulos sin palabras (escritos
// a mano o retocados) reparten su tramo entre sus palabras según su longitud.
std::vector<TimedWord> captionWords(const CaptionTrack& track, FrameRate rate);

// Número de caracteres (no de bytes) de un texto UTF-8.
std::size_t utf8Length(std::string_view text);

// Texto sin signos de puntuación (.,;:!?¡¿…"«» y comillas).
std::string withoutPunctuation(std::string_view text);

// --- Archivos .srt ---

struct SrtEntry {
    std::int64_t startUs = 0;
    std::int64_t endUs = 0;
    std::string text;

    friend bool operator==(const SrtEntry&, const SrtEntry&) = default;
};

std::string captionsToSrt(const CaptionTrack& track, FrameRate rate);
// Lee un .srt (con o sin BOM, saltos \n o \r\n). Las entradas que no se entienden se
// ignoran.
std::vector<SrtEntry> parseSrt(std::string_view text);
// Subtítulos a partir de entradas .srt, ordenados y sin solaparse. Ids a 0.
std::vector<Caption> captionsFromSrt(const std::vector<SrtEntry>& entries, FrameRate rate);

} // namespace sharkcut
