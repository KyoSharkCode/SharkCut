#pragma once

// Ajustes guardados de clips (presets): el aspecto de un clip con
// nombre (colocación, opacidad, recorte, efectos, pantalla verde y su animación) para
// aplicarlo a otros arrastrándolo. La animación se guarda en tiempo relativo al inicio o
// al final del clip, en microsegundos: vale para clips de cualquier duración y fps.

#include "core/project.h"
#include "core/time.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sharkcut {

// Qué partes del clip lleva el ajuste.
struct PresetParts {
    bool placement = true; // posición, escala y rotación
    bool opacity = true;
    bool crop = true;
    bool effects = true; // blanco y negro, desenfoque, esquinas, sombra
    bool chroma = true;  // pantalla verde
    bool animation = true;

    friend bool operator==(const PresetParts&, const PresetParts&) = default;
};

enum class PresetAnchor { Start, End }; // la animación cuenta desde el inicio o el final

struct PresetKeyframe {
    std::int64_t offsetUs = 0; // desde el ancla (negativo con el ancla al final)
    double value = 0.0;
    Easing easing = Easing::Linear;

    friend bool operator==(const PresetKeyframe&, const PresetKeyframe&) = default;
};

struct ClipPreset {
    std::string name;
    PresetParts parts;
    PresetAnchor anchor = PresetAnchor::Start;
    ClipTransform transform; // valores fijos (de las partes que lleva)
    std::array<std::vector<PresetKeyframe>, kAnimPropertyCount> keys{};

    bool animated() const;

    friend bool operator==(const ClipPreset&, const ClipPreset&) = default;
};

// ¿La propiedad pertenece a alguna de las partes?
bool presetIncludes(const PresetParts& parts, AnimProperty property);

// El ajuste a partir de un clip. Sin animación (o para las propiedades que no están
// animadas) se guarda el valor que tiene en `frame` (lo que se ve en el visor).
ClipPreset presetFromClip(const Clip& clip, std::int64_t frame, FrameRate rate,
                          const PresetParts& parts, PresetAnchor anchor, std::string name);

// Aplica el ajuste al clip de video (pista sin bloquear): sus partes sustituyen a las
// del clip, con su animación (o sin ella, si el ajuste no la lleva) colocada desde el
// inicio o el final del clip. Lo demás del clip no cambia.
bool applyPreset(Project& project, ObjectId clip, const ClipPreset& preset);

// Lista de ajustes ↔ JSON (para guardarlos en la configuración de la app).
std::string presetsToJson(const std::vector<ClipPreset>& presets);
// Los que no se entienden se ignoran.
std::vector<ClipPreset> presetsFromJson(std::string_view json);

} // namespace sharkcut
