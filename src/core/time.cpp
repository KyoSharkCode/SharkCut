#include "core/time.h"

#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

namespace sharkcut {

namespace {

// Entero sin signo de 128 bits mínimo y portable (MSVC no tiene __int128).
struct U128 {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
};

U128 mul64(std::uint64_t a, std::uint64_t b) {
    const std::uint64_t aLo = a & 0xFFFFFFFFu;
    const std::uint64_t aHi = a >> 32;
    const std::uint64_t bLo = b & 0xFFFFFFFFu;
    const std::uint64_t bHi = b >> 32;

    const std::uint64_t ll = aLo * bLo;
    const std::uint64_t lh = aLo * bHi;
    const std::uint64_t hl = aHi * bLo;
    const std::uint64_t hh = aHi * bHi;

    const std::uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu);
    U128 r;
    r.lo = (mid << 32) | (ll & 0xFFFFFFFFu);
    r.hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    return r;
}

// Divide n entre d. Requiere n.hi < d, lo que garantiza que el cociente cabe en 64 bits.
std::uint64_t div128(U128 n, std::uint64_t d, std::uint64_t& remainder) {
    std::uint64_t rem = n.hi;
    std::uint64_t quot = 0;
    for (int bit = 63; bit >= 0; --bit) {
        const bool carry = (rem >> 63) != 0;
        rem = (rem << 1) | ((n.lo >> bit) & 1u);
        quot <<= 1;
        if (carry || rem >= d) {
            rem -= d;
            quot |= 1u;
        }
    }
    remainder = rem;
    return quot;
}

std::uint64_t magnitude(std::int64_t v) {
    return v < 0 ? 0u - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
}

void validateTimeBase(Rational r, const char* name) {
    constexpr std::int64_t kLimit = std::int64_t{1} << 31;
    if (r.num <= 0 || r.den <= 0 || r.num >= kLimit || r.den >= kLimit) {
        throw std::invalid_argument(std::string("base de tiempo no válida: ") + name);
    }
}

} // namespace

std::int64_t mulDiv(std::int64_t a, std::int64_t b, std::int64_t c, Rounding rounding) {
    if (c <= 0) {
        throw std::invalid_argument("mulDiv: el divisor debe ser positivo");
    }
    const bool negative = (a < 0) != (b < 0) && a != 0 && b != 0;
    const std::uint64_t divisor = static_cast<std::uint64_t>(c);

    const U128 product = mul64(magnitude(a), magnitude(b));
    if (product.hi >= divisor) {
        throw std::overflow_error("mulDiv: el resultado no cabe en 64 bits");
    }

    std::uint64_t rem = 0;
    std::uint64_t q = div128(product, divisor, rem);

    // Redondeo aplicado a la magnitud del resultado.
    bool roundUp = false;
    switch (rounding) {
    case Rounding::Nearest:
        roundUp = rem >= divisor - rem; // rem * 2 >= divisor, sin desbordar
        break;
    case Rounding::Down:
        roundUp = negative && rem != 0;
        break;
    case Rounding::Up:
        roundUp = !negative && rem != 0;
        break;
    }
    if (roundUp) {
        ++q;
    }

    constexpr auto kMax = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (negative) {
        if (q > kMax + 1) {
            throw std::overflow_error("mulDiv: el resultado no cabe en 64 bits");
        }
        return q == kMax + 1 ? std::numeric_limits<std::int64_t>::min()
                             : -static_cast<std::int64_t>(q);
    }
    if (q > kMax) {
        throw std::overflow_error("mulDiv: el resultado no cabe en 64 bits");
    }
    return static_cast<std::int64_t>(q);
}

std::int64_t rescale(std::int64_t value, Rational from, Rational to, Rounding rounding) {
    validateTimeBase(from, "origen");
    validateTimeBase(to, "destino");
    // value * from.num / from.den = x * to.num / to.den
    // Con num y den < 2^31, ambos productos caben en 62 bits.
    return mulDiv(value, from.num * to.den, from.den * to.num, rounding);
}

std::int64_t frameToMicroseconds(std::int64_t frame, FrameRate rate) {
    return rescale(frame, rate.frameDuration(), kMicroseconds, Rounding::Nearest);
}

std::int64_t microsecondsToFrame(std::int64_t us, FrameRate rate) {
    // El inicio de cada fotograma está redondeado al microsegundo más cercano, así que
    // se compara contra esos mismos valores para que la conversión sea reversible.
    std::int64_t frame = rescale(us, kMicroseconds, rate.frameDuration(), Rounding::Down);
    while (frameToMicroseconds(frame + 1, rate) <= us) {
        ++frame;
    }
    while (frameToMicroseconds(frame, rate) > us) {
        --frame;
    }
    return frame;
}

std::string formatTimecode(std::int64_t frame, FrameRate rate) {
    const std::int64_t fps = rate.nominal();
    if (fps <= 0) {
        throw std::invalid_argument("formatTimecode: fps no válidos");
    }
    const bool negative = frame < 0;
    const std::uint64_t total = magnitude(frame);
    const auto ufps = static_cast<std::uint64_t>(fps);

    const std::uint64_t ff = total % ufps;
    const std::uint64_t totalSeconds = total / ufps;
    const std::uint64_t ss = totalSeconds % 60;
    const std::uint64_t mm = (totalSeconds / 60) % 60;
    const std::uint64_t hh = totalSeconds / 3600;

    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%s%02llu:%02llu:%02llu:%02llu", negative ? "-" : "",
                  static_cast<unsigned long long>(hh), static_cast<unsigned long long>(mm),
                  static_cast<unsigned long long>(ss), static_cast<unsigned long long>(ff));
    return buffer;
}

std::optional<std::int64_t> parseTimecode(std::string_view text, FrameRate rate) {
    const std::int64_t fps = rate.nominal();
    if (fps <= 0) {
        return std::nullopt;
    }
    // Campos de derecha a izquierda: fotogramas, segundos, minutos, horas.
    std::vector<std::int64_t> fields;
    std::int64_t value = 0;
    int digits = 0;
    auto close = [&]() -> bool {
        if (digits == 0) {
            return false;
        }
        fields.push_back(value);
        value = 0;
        digits = 0;
        return true;
    };
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && text[begin] == ' ') {
        ++begin;
    }
    while (end > begin && text[end - 1] == ' ') {
        --end;
    }
    for (std::size_t i = begin; i < end; ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            if (++digits > 9) {
                return std::nullopt; // demasiado largo para ser un tiempo razonable
            }
            value = value * 10 + (c - '0');
        } else if (c == ':' || c == '.') {
            if (!close()) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    if (!close() || fields.size() > 4) {
        return std::nullopt;
    }
    static constexpr std::int64_t kSeconds[] = {0, 1, 60, 3600};
    std::int64_t frame = fields.back(); // el último campo escrito son los fotogramas
    for (std::size_t i = 1; i < fields.size(); ++i) {
        frame += fields[fields.size() - 1 - i] * kSeconds[i] * fps;
    }
    return frame;
}

} // namespace sharkcut
