#include "core/easing.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sharkcut {

double applyEasing(Easing easing, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::EaseInOutSine:
        return -(std::cos(std::numbers::pi * t) - 1.0) / 2.0;
    case Easing::EaseInOutCubic:
        return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    case Easing::EaseInOutQuart:
        return t < 0.5 ? 8.0 * std::pow(t, 4.0) : 1.0 - std::pow(-2.0 * t + 2.0, 4.0) / 2.0;
    case Easing::EaseInOutExpo:
        if (t <= 0.0) {
            return 0.0;
        }
        if (t >= 1.0) {
            return 1.0;
        }
        return t < 0.5 ? std::pow(2.0, 20.0 * t - 10.0) / 2.0
                       : (2.0 - std::pow(2.0, -20.0 * t + 10.0)) / 2.0;
    case Easing::EaseInCubic:
        return t * t * t;
    case Easing::EaseOutCubic:
        return 1.0 - std::pow(1.0 - t, 3.0);
    case Easing::Hold:
        return t >= 1.0 ? 1.0 : 0.0;
    case Easing::EaseOutBack: {
        constexpr double c1 = 1.70158;
        constexpr double c3 = c1 + 1.0;
        const double u = t - 1.0;
        return 1.0 + c3 * u * u * u + c1 * u * u;
    }
    }
    return t;
}

std::optional<Easing> easingFromId(std::string_view id) {
    for (const EasingInfo& e : kEasings) {
        if (e.id == id) {
            return e.easing;
        }
    }
    return std::nullopt;
}

double easingVelocity(Easing easing, double t) {
    constexpr double h = 1e-3;
    const double a = std::clamp(t - h, 0.0, 1.0);
    const double b = std::clamp(t + h, 0.0, 1.0);
    return (applyEasing(easing, b) - applyEasing(easing, a)) / (b - a);
}

} // namespace sharkcut
