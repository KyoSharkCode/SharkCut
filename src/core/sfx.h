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
    std::string_view category = {}; // grupo en la pestaña Sonidos
};

inline constexpr std::array<SoundEffect, 14> kSoundEffects{{
    {"whoosh", "whoosh.wav", "Whoosh", 750'000, 400'000, "Whoosh"},
    {"whoosh-subida", "whoosh-subida.wav", "Whoosh que sube", 650'000, 550'000, "Whoosh"},
    {"whoosh-bajada", "whoosh-bajada.wav", "Whoosh que baja", 750'000, 300'000, "Whoosh"},
    {"swoosh", "swoosh.wav", "Swoosh rápido", 450'000, 220'000, "Swoosh"},
    {"giro", "giro.wav", "Giro", 850'000, 400'000, "Whoosh"},
    {"impacto", "impacto.wav", "Impacto", 1'000'000, 0, "Impactos"},
    {"glitch", "glitch.wav", "Glitch", 500'000, 180'000, "Impactos"},
    {"riser", "riser.wav", "Subida (riser)", 1'200'000, 1'150'000, "Impactos"},
    {"clic-raton", "clic-raton.wav", "Clic de ratón", 160'000, 0, "Clic"},
    {"clic-suave", "clic-suave.wav", "Clic suave", 80'000, 0, "Clic"},
    {"pop-hueco", "pop-hueco.wav", "Pop hueco", 220'000, 0, "Pop"},
    {"pop-burbuja", "pop-burbuja.wav", "Pop de burbuja", 160'000, 0, "Pop"},
    {"swoosh-corto", "swoosh-corto.wav", "Swoosh corto", 300'000, 140'000, "Swoosh"},
    {"swoosh-grave", "swoosh-grave.wav", "Swoosh grave", 400'000, 180'000, "Swoosh"},
}};

// nullptr si no existe.
const SoundEffect* soundEffect(std::string_view id);

// Sonido que se pone por defecto con cada transición ("" = ninguno).
std::string_view defaultSoundFor(TransitionType type);

} // namespace sharkcut
