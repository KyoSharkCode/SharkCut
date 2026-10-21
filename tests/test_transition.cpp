#include "core/easing.h"
#include "core/sequence.h"
#include "core/transition.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sharkcut;

TEST_CASE("Las curvas de suavizado empiezan en 0 y terminan en 1", "[easing]") {
    for (const EasingInfo& info : kEasings) {
        CAPTURE(info.id);
        CHECK(applyEasing(info.easing, 0.0) == Catch::Approx(0.0).margin(1e-9));
        CHECK(applyEasing(info.easing, 1.0) == Catch::Approx(1.0).margin(1e-9));
        // Fuera de rango se limita.
        CHECK(applyEasing(info.easing, -1.0) == Catch::Approx(0.0).margin(1e-9));
        CHECK(applyEasing(info.easing, 2.0) == Catch::Approx(1.0).margin(1e-9));
    }
}

TEST_CASE("Las curvas simetricas pasan por el centro y nunca retroceden", "[easing]") {
    // Las de las transiciones (las demás, solo de keyframes, no son simétricas).
    for (std::size_t e = 0; e < kTransitionEasings; ++e) {
        const EasingInfo& info = kEasings[e];
        CAPTURE(info.id);
        if (info.easing != Easing::EaseOutBack) {
            CHECK(applyEasing(info.easing, 0.5) == Catch::Approx(0.5).margin(1e-9));
        }
        double previous = -1.0;
        for (int i = 0; i <= 100; ++i) {
            const double v = applyEasing(info.easing, i / 100.0);
            if (info.easing != Easing::EaseOutBack) {
                CHECK(v >= previous - 1e-12);
            }
            previous = v;
        }
    }
    // El rebote pasa de 1 antes de asentarse.
    CHECK(applyEasing(Easing::EaseOutBack, 0.7) > 1.0);
}

TEST_CASE("Keyframe easings go from 0 to 1 without going back", "[easing]") {
    for (const Easing e : kKeyframeEasings) {
        CAPTURE(kEasings[static_cast<std::size_t>(e)].id);
        CHECK(kEasings[static_cast<std::size_t>(e)].easing == e); // catálogo en el orden del enum
        CHECK(applyEasing(e, 0.0) == Catch::Approx(0.0).margin(1e-9));
        CHECK(applyEasing(e, 1.0) == Catch::Approx(1.0).margin(1e-9));
        CHECK(easingFromId(kEasings[static_cast<std::size_t>(e)].id) == e);
    }
    CHECK(applyEasing(Easing::EaseInCubic, 0.5) < 0.5);  // arranca despacio
    CHECK(applyEasing(Easing::EaseOutCubic, 0.5) > 0.5); // frena al llegar
    CHECK(applyEasing(Easing::Hold, 0.99) == 0.0);       // salta al final
    CHECK_FALSE(easingFromId("no-existe").has_value());
}

TEST_CASE("La velocidad de la curva es maxima en el centro de un ease-in-out", "[easing]") {
    const double center = easingVelocity(Easing::EaseInOutExpo, 0.5);
    CHECK(center > easingVelocity(Easing::EaseInOutExpo, 0.1));
    CHECK(center > easingVelocity(Easing::EaseInOutExpo, 0.9));
    CHECK(easingVelocity(Easing::Linear, 0.3) == Catch::Approx(1.0).margin(1e-6));
}

TEST_CASE("Catalogo de transiciones: las siete prioritarias", "[transition]") {
    CHECK(kTransitions.size() == 7);
    CHECK(transitionInfo(TransitionType::ZoomRotateOut).name == "Zoom & Rotate Out");
    CHECK(transitionInfo(TransitionType::PanLeft).id == "pan-left");
    for (const TransitionInfo& info : kTransitions) {
        CHECK(info.defaults.type == info.type);
        CHECK(info.defaults.durationFrames > 0);
    }
}

TEST_CASE("El progreso de la transicion se evalua en el centro de cada fotograma", "[transition]") {
    TransitionParams params;
    params.durationFrames = 4;
    params.easing = Easing::Linear;
    CHECK(evaluateTransition(params, 0).progress == Catch::Approx(0.125));
    CHECK(evaluateTransition(params, 3).progress == Catch::Approx(0.875));
    CHECK(evaluateTransition(params, 99).progress == Catch::Approx(0.875)); // se limita
    CHECK(evaluateTransition(params, 1).eased == Catch::Approx(0.375));
}

TEST_CASE("La secuencia A, transicion, B coloca cada fotograma", "[sequence]") {
    // A: fotogramas 100–159 (60), B: 500–589 (90), transición de 20.
    const SequenceLayout layout = makeLayout({100, 160}, {500, 590}, 20);
    CHECK(layout.length() == 60 + 90 - 20);
    CHECK(layout.transitionStart() == 40);
    CHECK(layout.transitionEnd() == 60);

    auto at = layout.at(0);
    CHECK(at.aFrame == 100);
    CHECK_FALSE(at.bFrame.has_value());
    CHECK_FALSE(at.transitionFrame.has_value());

    at = layout.at(39); // justo antes de la transición
    CHECK(at.aFrame == 139);
    CHECK_FALSE(at.bFrame.has_value());

    at = layout.at(40); // primer fotograma de la transición
    CHECK(at.aFrame == 140);
    CHECK(at.bFrame == 500);
    CHECK(at.transitionFrame == 0);

    at = layout.at(59); // último fotograma de la transición
    CHECK(at.aFrame == 159);
    CHECK(at.bFrame == 519);
    CHECK(at.transitionFrame == 19);

    at = layout.at(60); // ya solo B
    CHECK_FALSE(at.aFrame.has_value());
    CHECK(at.bFrame == 520);

    at = layout.at(1'000); // se limita al último fotograma
    CHECK(at.bFrame == 589);
}

TEST_CASE("La transicion no puede durar mas que el clip mas corto", "[sequence]") {
    CHECK(makeLayout({0, 10}, {0, 100}, 30).transitionFrames == 10);
    CHECK(makeLayout({0, 100}, {0, 100}, 0).transitionFrames == 1);
    CHECK(makeLayout({0, 0}, {0, 100}, 30).transitionFrames == 0);
    CHECK(makeLayout({5, 3}, {0, 100}, 30).length() == 100); // tramo inválido = vacío
}
