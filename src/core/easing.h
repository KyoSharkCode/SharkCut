#pragma once

// Curvas de suavizado (easing) para transiciones y, más adelante, animaciones.

#include <array>
#include <string_view>

namespace sharkcut {

enum class Easing {
    Linear,
    EaseInOutSine,
    EaseInOutCubic,
    EaseInOutQuart,
    EaseInOutExpo,
    EaseOutBack, // con un pequeño rebote al final
};

struct EasingInfo {
    Easing easing;
    std::string_view id;   // identificador estable (archivos de proyecto, presets)
    std::string_view name; // nombre para la interfaz
};

inline constexpr std::array<EasingInfo, 6> kEasings{{
    {Easing::Linear, "linear", "Lineal"},
    {Easing::EaseInOutSine, "easeInOutSine", "Suave (seno)"},
    {Easing::EaseInOutCubic, "easeInOutCubic", "Suave (cúbica)"},
    {Easing::EaseInOutQuart, "easeInOutQuart", "Marcada (cuártica)"},
    {Easing::EaseInOutExpo, "easeInOutExpo", "Explosiva (exponencial)"},
    {Easing::EaseOutBack, "easeOutBack", "Con rebote"},
}};

// Aplica la curva a t ∈ [0, 1] (fuera de ese rango se limita). Devuelve 0 en t = 0 y
// 1 en t = 1; EaseOutBack puede pasar ligeramente de 1 antes de llegar.
double applyEasing(Easing easing, double t);

// Velocidad de la curva (derivada) en t, aproximada numéricamente. Sirve para
// graduar el desenfoque de movimiento: más velocidad, más desenfoque.
double easingVelocity(Easing easing, double t);

} // namespace sharkcut
