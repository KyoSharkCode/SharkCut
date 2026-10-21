#pragma once

// Curvas de suavizado (easing) para transiciones y, más adelante, animaciones.

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace sharkcut {

enum class Easing {
    Linear,
    EaseInOutSine,
    EaseInOutCubic,
    EaseInOutQuart,
    EaseInOutExpo,
    EaseOutBack, // con un pequeño rebote al final
    // Solo para keyframes (no se ofrecen en las transiciones):
    EaseInCubic,  // arranca despacio
    EaseOutCubic, // frena al llegar
    Hold,         // se queda en el valor hasta el siguiente keyframe (salto)
};

struct EasingInfo {
    Easing easing;
    std::string_view id;   // identificador estable (archivos de proyecto, presets)
    std::string_view name; // nombre para la interfaz
};

// En el orden del enum (kEasings[static_cast<size_t>(e)] es la de `e`).
inline constexpr std::array<EasingInfo, 9> kEasings{{
    {Easing::Linear, "linear", "Lineal"},
    {Easing::EaseInOutSine, "easeInOutSine", "Suave (seno)"},
    {Easing::EaseInOutCubic, "easeInOutCubic", "Suave (cúbica)"},
    {Easing::EaseInOutQuart, "easeInOutQuart", "Marcada (cuártica)"},
    {Easing::EaseInOutExpo, "easeInOutExpo", "Explosiva (exponencial)"},
    {Easing::EaseOutBack, "easeOutBack", "Con rebote"},
    {Easing::EaseInCubic, "easeInCubic", "Entrada suave"},
    {Easing::EaseOutCubic, "easeOutCubic", "Salida suave"},
    {Easing::Hold, "hold", "Mantener (salto)"},
}};
// Las primeras de kEasings son las de las transiciones.
inline constexpr std::size_t kTransitionEasings = 6;

// Curvas de los keyframes, en el orden de la interfaz.
inline constexpr std::array<Easing, 8> kKeyframeEasings{
    Easing::Linear,        Easing::EaseInOutSine, Easing::EaseInCubic,    Easing::EaseOutCubic,
    Easing::EaseInOutExpo, Easing::EaseOutBack,   Easing::EaseInOutCubic, Easing::Hold,
};

// La curva con ese identificador estable (std::nullopt si no existe).
std::optional<Easing> easingFromId(std::string_view id);

// Aplica la curva a t ∈ [0, 1] (fuera de ese rango se limita). Devuelve 0 en t = 0 y
// 1 en t = 1; EaseOutBack puede pasar ligeramente de 1 antes de llegar.
double applyEasing(Easing easing, double t);

// Velocidad de la curva (derivada) en t, aproximada numéricamente. Sirve para
// graduar el desenfoque de movimiento: más velocidad, más desenfoque.
double easingVelocity(Easing easing, double t);

} // namespace sharkcut
