#include "media/color.h"

namespace sharkcut {

std::array<float, 16> yuvToRgbMatrix(ColorMatrix matrix, bool fullRange) {
    // Coeficientes de luminancia de cada estándar.
    double kr = 0.2126;
    double kb = 0.0722;
    switch (matrix) {
    case ColorMatrix::Bt601:
        kr = 0.299;
        kb = 0.114;
        break;
    case ColorMatrix::Bt709:
        break;
    case ColorMatrix::Bt2020:
        kr = 0.2627;
        kb = 0.0593;
        break;
    }
    const double kg = 1.0 - kr - kb;

    // Y' = ys·(Y − yo);  Cb = cs·(U − co);  Cr = cs·(V − co)
    const double ys = fullRange ? 1.0 : 255.0 / 219.0;
    const double yo = fullRange ? 0.0 : 16.0 / 255.0;
    const double cs = fullRange ? 1.0 : 255.0 / 224.0;
    const double co = 128.0 / 255.0;

    // R = Y' + rv·Cr;  G = Y' − gu·Cb − gv·Cr;  B = Y' + bu·Cb
    const double rv = 2.0 * (1.0 - kr);
    const double gu = 2.0 * kb * (1.0 - kb) / kg;
    const double gv = 2.0 * kr * (1.0 - kr) / kg;
    const double bu = 2.0 * (1.0 - kb);

    const double yTerm = -ys * yo;
    auto f = [](double v) { return static_cast<float>(v); };
    return {
        f(ys),  f(0.0),      f(rv * cs),  f(yTerm - rv * cs * co),                // R
        f(ys),  f(-gu * cs), f(-gv * cs), f(yTerm + gu * cs * co + gv * cs * co), // G
        f(ys),  f(bu * cs),  f(0.0),      f(yTerm - bu * cs * co),                // B
        f(0.0), f(0.0),      f(0.0),      f(1.0),
    };
}

} // namespace sharkcut
