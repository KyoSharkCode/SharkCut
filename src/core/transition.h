#pragma once

// Transiciones entre dos clips: catálogo, parámetros y estado en cada fotograma.
// Todo es independiente de la GPU; el compositor traduce el estado a un shader.

#include "core/easing.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace sharkcut {

enum class TransitionType {
    FadeToBlack,
    PanLeft,
    Rotate,
    ShakeDistort,
    ZoomIn,
    ZoomOut,
    ZoomRotateOut,
};

struct TransitionParams {
    TransitionType type = TransitionType::ZoomIn;
    std::int64_t durationFrames = 30; // a 60 fps, medio segundo
    double intensity = 0.6;           // 0–1: cuánto zoom, giro, desplazamiento o temblor
    double blur = 0.6;                // 0–1: desenfoque de movimiento
    Easing easing = Easing::EaseInOutExpo;
};

struct TransitionInfo {
    TransitionType type;
    std::string_view id;   // identificador estable (presets, archivos de proyecto)
    std::string_view name; // nombre para la interfaz
    TransitionParams defaults;
};

inline constexpr std::array<TransitionInfo, 7> kTransitions{{
    {TransitionType::FadeToBlack,
     "fade-to-black",
     "Fade to Black",
     {TransitionType::FadeToBlack, 30, 0.5, 0.0, Easing::EaseInOutSine}},
    {TransitionType::PanLeft,
     "pan-left",
     "Pan (Left)",
     {TransitionType::PanLeft, 24, 1.0, 0.7, Easing::EaseInOutExpo}},
    {TransitionType::Rotate,
     "rotate",
     "Rotate",
     {TransitionType::Rotate, 30, 0.6, 0.6, Easing::EaseInOutExpo}},
    {TransitionType::ShakeDistort,
     "shake-distort",
     "Shake & Distort",
     {TransitionType::ShakeDistort, 24, 0.6, 0.5, Easing::EaseInOutSine}},
    {TransitionType::ZoomIn,
     "zoom-in",
     "Zoom In",
     {TransitionType::ZoomIn, 30, 0.6, 0.6, Easing::EaseInOutExpo}},
    {TransitionType::ZoomOut,
     "zoom-out",
     "Zoom Out",
     {TransitionType::ZoomOut, 30, 0.6, 0.6, Easing::EaseInOutExpo}},
    {TransitionType::ZoomRotateOut,
     "zoom-rotate-out",
     "Zoom & Rotate Out",
     {TransitionType::ZoomRotateOut, 30, 0.6, 0.6, Easing::EaseInOutExpo}},
}};

const TransitionInfo& transitionInfo(TransitionType type);

// Estado de la transición en un fotograma concreto.
struct TransitionState {
    double progress = 0.0; // 0–1 lineal (centro del fotograma)
    double eased = 0.0;    // progreso tras aplicar la curva
    double velocity = 0.0; // velocidad de la curva en ese punto (para el desenfoque)
};

// Estado en el fotograma `frame` de la transición (0 = primero, durationFrames − 1 =
// último). Se evalúa en el centro de cada fotograma, así que nunca vale exactamente
// 0 ni 1: el primer fotograma ya se mueve un poco y el último aún no ha terminado.
TransitionState evaluateTransition(const TransitionParams& params, std::int64_t frame);

} // namespace sharkcut
