#include "media/color.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace sharkcut;

namespace {

struct Rgb {
    double r, g, b;
};

// Aplica la matriz a valores YUV de 8 bits, como hace el shader.
Rgb apply(const std::array<float, 16>& m, int y, int u, int v) {
    const double in[4] = {y / 255.0, u / 255.0, v / 255.0, 1.0};
    double out[3] = {};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            out[row] += m[row * 4 + col] * in[col];
        }
    }
    return {out[0], out[1], out[2]};
}

void checkRgb(const Rgb& c, double r, double g, double b) {
    constexpr double kTolerance = 0.006; // menos de 2/255
    CHECK(c.r == Catch::Approx(r).margin(kTolerance));
    CHECK(c.g == Catch::Approx(g).margin(kTolerance));
    CHECK(c.b == Catch::Approx(b).margin(kTolerance));
}

} // namespace

TEST_CASE("Rango limitado: 16 es negro y 235 es blanco", "[color]") {
    for (const ColorMatrix matrix : {ColorMatrix::Bt601, ColorMatrix::Bt709, ColorMatrix::Bt2020}) {
        const auto m = yuvToRgbMatrix(matrix, false);
        checkRgb(apply(m, 16, 128, 128), 0, 0, 0);
        checkRgb(apply(m, 235, 128, 128), 1, 1, 1);
        checkRgb(apply(m, 126, 128, 128), 0.5023, 0.5023, 0.5023); // gris medio
    }
}

TEST_CASE("Rango completo: 0 es negro y 255 es blanco", "[color]") {
    const auto m = yuvToRgbMatrix(ColorMatrix::Bt709, true);
    checkRgb(apply(m, 0, 128, 128), 0, 0, 0);
    checkRgb(apply(m, 255, 128, 128), 1, 1, 1);
}

TEST_CASE("BT.709 limitado reproduce los colores primarios", "[color]") {
    // Valores YCbCr de referencia de las barras de color al 100 % (BT.709, 8 bits).
    const auto m = yuvToRgbMatrix(ColorMatrix::Bt709, false);
    checkRgb(apply(m, 63, 102, 240), 1, 0, 0); // rojo
    checkRgb(apply(m, 173, 42, 26), 0, 1, 0);  // verde
    checkRgb(apply(m, 32, 240, 118), 0, 0, 1); // azul
}

TEST_CASE("BT.601 limitado reproduce los colores primarios", "[color]") {
    const auto m = yuvToRgbMatrix(ColorMatrix::Bt601, false);
    checkRgb(apply(m, 81, 90, 240), 1, 0, 0);  // rojo
    checkRgb(apply(m, 145, 54, 34), 0, 1, 0);  // verde
    checkRgb(apply(m, 41, 240, 110), 0, 0, 1); // azul
}
