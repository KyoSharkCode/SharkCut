#pragma once

// Biblioteca de efectos de sonido (presets/sfx/) y su emparejamiento con las
// transiciones. Los archivos los genera tools/sfx/generar_sfx.py; cada uno está
// registrado, con su licencia, en presets/sfx/LICENCIAS.md.

#include "core/transition.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace sharkcut {

struct SoundEffect {
    std::string_view id;   // identificador estable
    std::string_view file; // nombre del archivo en presets/sfx/
    std::string_view name; // nombre para la interfaz
    std::int64_t durationUs = 0;
    // Instante del sonido que cae en el corte de la transición (lo más fuerte del
    // whoosh, el golpe del impacto…).
    std::int64_t hitUs = 0;
};

inline constexpr std::array<SoundEffect, 8> kSoundEffects{{
    {"whoosh", "whoosh.wav", "Whoosh", 750'000, 400'000},
    {"whoosh-subida", "whoosh-subida.wav", "Whoosh que sube", 650'000, 550'000},
    {"whoosh-bajada", "whoosh-bajada.wav", "Whoosh que baja", 750'000, 300'000},
    {"swoosh", "swoosh.wav", "Swoosh rápido", 450'000, 220'000},
    {"giro", "giro.wav", "Giro", 850'000, 400'000},
    {"impacto", "impacto.wav", "Impacto", 1'000'000, 0},
    {"glitch", "glitch.wav", "Glitch", 500'000, 180'000},
    {"riser", "riser.wav", "Subida (riser)", 1'200'000, 1'150'000},
}};

// nullptr si no existe.
const SoundEffect* soundEffect(std::string_view id);

// Sonido que se pone por defecto con cada transición ("" = ninguno).
std::string_view defaultSoundFor(TransitionType type);

} // namespace sharkcut
