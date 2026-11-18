#include "core/transition.h"

#include <algorithm>

namespace sharkcut {

const TransitionInfo& transitionInfo(TransitionType type) {
    for (const TransitionInfo& info : kTransitions) {
        if (info.type == type) {
            return info;
        }
    }
    return kTransitions.front();
}

TransitionState evaluateTransition(const TransitionParams& params, std::int64_t frame) {
    const std::int64_t duration = std::max<std::int64_t>(params.durationFrames, 1);
    frame = std::clamp<std::int64_t>(frame, 0, duration - 1);

    TransitionState state;
    state.progress = (static_cast<double>(frame) + 0.5) / static_cast<double>(duration);
    state.eased = applyEasing(params.easing, state.progress);
    state.velocity = easingVelocity(params.easing, state.progress);
    if (params.variant != TransitionVariant::Cut && hasVariants(params.type)) {
        // Media transición: el mismo recorrido en la mitad que toca (y a media velocidad).
        const double offset = params.variant == TransitionVariant::In ? 0.5 : 0.0;
        state.progress = offset + 0.5 * state.progress;
        state.eased = offset + 0.5 * state.eased;
        state.velocity *= 0.5;
    }
    return state;
}

bool hasVariants(TransitionType type) {
    return type == TransitionType::FadeToBlack;
}

const TransitionVariantInfo& transitionVariantInfo(TransitionVariant variant) {
    for (const TransitionVariantInfo& info : kTransitionVariants) {
        if (info.variant == variant) {
            return info;
        }
    }
    return kTransitionVariants.back();
}

} // namespace sharkcut
