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
    return state;
}

} // namespace sharkcut
