#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "animations/bubble/BubbleOptics.hpp"

namespace liquid_glass {
namespace {
double gaussian(double wavelength, double mean, double left, double right) {
    const double x = (wavelength - mean) * (wavelength < mean ? left : right);
    return std::exp(-0.5 * x * x);
}

std::array<double, 3> matching(double w) {
    return {1.056 * gaussian(w, 599.8, 0.0264, 0.0323) + 0.362 * gaussian(w, 442.0, 0.0624, 0.0374) -
                0.065 * gaussian(w, 501.1, 0.0490, 0.0382),
            0.821 * gaussian(w, 568.8, 0.0213, 0.0247) + 0.286 * gaussian(w, 530.9, 0.0613, 0.0322),
            1.217 * gaussian(w, 437.0, 0.0845, 0.0278) + 0.681 * gaussian(w, 459.0, 0.0385, 0.0725)};
}

std::array<double, 3> rgb(const std::array<double, 3> &xyz) {
    return {3.2406 * xyz[0] - 1.5372 * xyz[1] - 0.4986 * xyz[2], -0.9689 * xyz[0] + 1.8758 * xyz[1] + 0.0415 * xyz[2],
            0.0557 * xyz[0] - 0.2040 * xyz[1] + 1.0570 * xyz[2]};
}
} // namespace

BubbleThinFilmTable bubbleThinFilmTable(int thicknessSamples, int cosineSamples, double maxThickness) {
    if (thicknessSamples < 2 || cosineSamples < 2 || !std::isfinite(maxThickness) || maxThickness <= 0)
        return {};
    BubbleThinFilmTable table{thicknessSamples, cosineSamples, maxThickness,
                              std::vector<float>(std::size_t(thicknessSamples) * cosineSamples * 3)};
    // Colour matching is evaluated once per wavelength, not once per table cell.
    constexpr int                                  wavelengths = (780 - 380) / 5 + 1;
    std::array<std::array<double, 3>, wavelengths> cmf{};
    std::array<double, 3>                          white{};
    for (int i = 0; i < wavelengths; ++i) {
        cmf[i] = matching(380 + 5 * i);
        for (int k = 0; k < 3; ++k)
            white[k] += cmf[i][k];
    }
    const auto       mirror = rgb(white);
    constexpr double n = 1.33;
    for (int y = 0; y < cosineSamples; ++y) {
        const double c = double(y) / (cosineSamples - 1);
        const double ct = std::sqrt(1.0 - (1.0 - c * c) / (n * n));
        const double rs = (c - n * ct) / (c + n * ct);
        const double rp = (n * c - ct) / (n * c + ct);
        for (int x = 1; x < thicknessSamples; ++x) {
            const double          d = maxThickness * x / (thicknessSamples - 1);
            std::array<double, 3> xyz{};
            for (int i = 0; i < wavelengths; ++i) {
                const double phase = 4.0 * std::numbers::pi * n * d * ct / (380 + 5 * i);
                const double sine = std::sin(phase * 0.5);
                const double s = 4 * rs * rs * sine * sine;
                const double p = 4 * rp * rp * sine * sine;
                const double reflectance =
                    0.5 * (s / ((1 - rs * rs) * (1 - rs * rs) + s) + p / ((1 - rp * rp) * (1 - rp * rp) + p));
                for (int k = 0; k < 3; ++k)
                    xyz[k] += cmf[i][k] * reflectance;
            }
            const auto colour = rgb(xyz);
            for (int k = 0; k < 3; ++k)
                table.rgb[3 * (std::size_t(y) * thicknessSamples + x) + k] =
                    float(std::clamp(colour[k] / mirror[k], 0.0, 1.0));
        }
    }
    return table;
}
} // namespace liquid_glass
