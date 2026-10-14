#include "core/sfx.h"

namespace sharkcut {

const SoundEffect* soundEffect(std::string_view id) {
    for (const SoundEffect& effect : kSoundEffects) {
        if (effect.id == id) {
            return &effect;
        }
    }
    return nullptr;
}

std::string_view defaultSoundFor(TransitionType type) {
    switch (type) {
    case TransitionType::FadeToBlack:
        return {}; // un fundido suele ir sin sonido
    case TransitionType::PanLeft:
        return "swoosh";
    case TransitionType::Rotate:
        return "giro";
    case TransitionType::ShakeDistort:
        return "impacto";
    case TransitionType::ZoomIn:
        return "whoosh-subida";
    case TransitionType::ZoomOut:
        return "whoosh-bajada";
    case TransitionType::ZoomRotateOut:
        return "whoosh";
    }
    return {};
}

} // namespace sharkcut
