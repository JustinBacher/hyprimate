#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include "animations/bubble/BubbleOptics.hpp"

namespace hyprimate {
    namespace {
        constexpr int COLOUR_CHANNELS = 3;
        using Colour = std::array<double, COLOUR_CHANNELS>;

        constexpr int FIRST_WAVELENGTH_NM = 380;
        constexpr int LAST_WAVELENGTH_NM = 780;
        constexpr int WAVELENGTH_STEP_NM = 5;
        constexpr int WAVELENGTH_SAMPLES =
            ((LAST_WAVELENGTH_NM - FIRST_WAVELENGTH_NM) / WAVELENGTH_STEP_NM) + 1;
        constexpr double FILM_REFRACTIVE_INDEX =
            1.33;  // Water-like soap film; surrounding air is 1.
        constexpr double UNPOLARIZED_WEIGHT =
            0.5;  // Equal contributions from s and p polarizations.
        constexpr double ROUND_TRIP_PHASE_FACTOR =
            4.0 * std::numbers::pi;  // 2pi times two film traversals.

        // Asymmetric Gaussian fits to the CIE 1931 XYZ colour matching functions.
        // Wyman, Sloan and Shirley (2013), Table 1: https://jcgt.org/published/0002/02/01/paper.pdf
        // These are fitted coefficients, not animation tuning parameters.
        struct MatchingLobe {
            double amplitude;
            double centre_nm;
            double left_inverse_width_per_nm;
            double right_inverse_width_per_nm;
        };
        constexpr MatchingLobe X_LONG_WAVELENGTH_LOBE{
            .amplitude = 1.056,
            .centre_nm = 599.8,
            .left_inverse_width_per_nm = 0.0264,
            .right_inverse_width_per_nm = 0.0323,
        };
        constexpr MatchingLobe X_SHORT_WAVELENGTH_LOBE{
            .amplitude = 0.362,
            .centre_nm = 442.0,
            .left_inverse_width_per_nm = 0.0624,
            .right_inverse_width_per_nm = 0.0374,
        };
        constexpr MatchingLobe X_SUBTRACTIVE_LOBE{
            .amplitude = 0.065,
            .centre_nm = 501.1,
            .left_inverse_width_per_nm = 0.0490,
            .right_inverse_width_per_nm = 0.0382,
        };
        constexpr MatchingLobe Y_LONG_WAVELENGTH_LOBE{
            .amplitude = 0.821,
            .centre_nm = 568.8,
            .left_inverse_width_per_nm = 0.0213,
            .right_inverse_width_per_nm = 0.0247,
        };
        constexpr MatchingLobe Y_SHORT_WAVELENGTH_LOBE{
            .amplitude = 0.286,
            .centre_nm = 530.9,
            .left_inverse_width_per_nm = 0.0613,
            .right_inverse_width_per_nm = 0.0322,
        };
        constexpr MatchingLobe Z_SHORT_WAVELENGTH_LOBE{
            .amplitude = 1.217,
            .centre_nm = 437.0,
            .left_inverse_width_per_nm = 0.0845,
            .right_inverse_width_per_nm = 0.0278,
        };
        constexpr MatchingLobe Z_LONG_WAVELENGTH_LOBE{
            .amplitude = 0.681,
            .centre_nm = 459.0,
            .left_inverse_width_per_nm = 0.0385,
            .right_inverse_width_per_nm = 0.0725,
        };

        double matching_lobe(double wavelength_nm, const MatchingLobe &lobe) {
            const double INVERSE_WIDTH = wavelength_nm < lobe.centre_nm
                                             ? lobe.left_inverse_width_per_nm
                                             : lobe.right_inverse_width_per_nm;
            const double NORMALIZED_OFFSET = (wavelength_nm - lobe.centre_nm) * INVERSE_WIDTH;
            // Unit Gaussian exp(-x^2 / 2), scaled by the fitted amplitude.
            return lobe.amplitude * std::exp(-0.5 * NORMALIZED_OFFSET * NORMALIZED_OFFSET);
        }

        Colour colour_matching(double wavelength_nm) {
            return {
                matching_lobe(wavelength_nm, X_LONG_WAVELENGTH_LOBE) +
                    matching_lobe(wavelength_nm, X_SHORT_WAVELENGTH_LOBE) -
                    matching_lobe(wavelength_nm, X_SUBTRACTIVE_LOBE),
                matching_lobe(wavelength_nm, Y_LONG_WAVELENGTH_LOBE) +
                    matching_lobe(wavelength_nm, Y_SHORT_WAVELENGTH_LOBE),
                matching_lobe(wavelength_nm, Z_SHORT_WAVELENGTH_LOBE) +
                    matching_lobe(wavelength_nm, Z_LONG_WAVELENGTH_LOBE),
            };
        }

        // CIE XYZ to linear sRGB (D65) conversion: rows are red, green and blue;
        // columns are X, Y and Z. No display gamma is applied to this reflectance table.
        constexpr std::array<Colour, COLOUR_CHANNELS> XYZ_TO_LINEAR_SRGB{
            {
                {3.2406, -1.5372, -0.4986},
                {-0.9689, 1.8758, 0.0415},
                {0.0557, -0.2040, 1.0570},
            },
        };

        Colour linear_srgb(const Colour &xyz) {
            Colour result{};
            std::ranges::transform(XYZ_TO_LINEAR_SRGB, result.begin(), [&xyz](const Colour &row) {
                return (std::get<0>(row) * std::get<0>(xyz)) +
                       (std::get<1>(row) * std::get<1>(xyz)) +
                       (std::get<2>(row) * std::get<2>(xyz));
            });
            return result;
        }

        constexpr double wavelength_nm(int index) {
            return FIRST_WAVELENGTH_NM + (WAVELENGTH_STEP_NM * index);
        }
    }  // namespace

    BubbleThinFilmTable bubble_thin_film_table(
        int thickness_samples, int cosine_samples, double max_thickness) {
        constexpr int K_MINIMUM_GRID_SAMPLES = 2;  // Both endpoints must be represented.
        if (thickness_samples < K_MINIMUM_GRID_SAMPLES || cosine_samples < K_MINIMUM_GRID_SAMPLES ||
            !std::isfinite(max_thickness) || max_thickness <= 0) {
            return {};
        }
        const auto WIDTH = static_cast<std::size_t>(thickness_samples);
        const auto HEIGHT = static_cast<std::size_t>(cosine_samples);
        if (WIDTH > std::vector<float>{}.max_size() / COLOUR_CHANNELS / HEIGHT) {
            return {};
        }
        BubbleThinFilmTable table{
            .thickness_samples = thickness_samples,
            .cosine_samples = cosine_samples,
            .max_thickness = max_thickness,
            .rgb = std::vector<float>(WIDTH * HEIGHT * COLOUR_CHANNELS),
        };
        // Colour matching is evaluated once per wavelength, not once per table cell.
        std::array<Colour, WAVELENGTH_SAMPLES> matching_by_wavelength{};
        Colour equal_energy_white_xyz{};
        int wavelength_index = 0;
        for (auto &matching : matching_by_wavelength) {
            matching = colour_matching(wavelength_nm(wavelength_index++));
            auto *white = equal_energy_white_xyz.begin();
            for (const auto MATCH_VALUE : matching) {
                *white += MATCH_VALUE;
                ++white;
            }
        }
        // Normalize each channel against a perfect reflector under an equal-energy spectrum.
        const auto PERFECT_MIRROR_RGB = linear_srgb(equal_energy_white_xyz);
        auto output = table.rgb.begin();
        for (int row = 0; row < cosine_samples; ++row) {
            ++output;
            ++output;
            ++output;
            const double INCIDENCE_COSINE = static_cast<double>(row) / (cosine_samples - 1);
            // Snell's law, with air's refractive index treated as one.
            const double TRANSMISSION_COSINE =
                std::sqrt(1.0 - ((1.0 - (INCIDENCE_COSINE * INCIDENCE_COSINE)) /
                                    (FILM_REFRACTIVE_INDEX * FILM_REFRACTIVE_INDEX)));
            const double S_REFLECTION_AMPLITUDE =
                (INCIDENCE_COSINE - (FILM_REFRACTIVE_INDEX * TRANSMISSION_COSINE)) /
                (INCIDENCE_COSINE + (FILM_REFRACTIVE_INDEX * TRANSMISSION_COSINE));
            const double P_REFLECTION_AMPLITUDE =
                ((FILM_REFRACTIVE_INDEX * INCIDENCE_COSINE) - TRANSMISSION_COSINE) /
                ((FILM_REFRACTIVE_INDEX * INCIDENCE_COSINE) + TRANSMISSION_COSINE);
            // The zero-thickness column remains zero: the two interfaces cancel exactly.
            for (int col = 1; col < thickness_samples; ++col) {
                const double THICKNESS_NM = max_thickness * col / (thickness_samples - 1);
                Colour reflected_xyz{};
                int spectral_index = 0;
                for (const auto &matching : matching_by_wavelength) {
                    const double ROUND_TRIP_PHASE =
                        ROUND_TRIP_PHASE_FACTOR * FILM_REFRACTIVE_INDEX * THICKNESS_NM *
                        TRANSMISSION_COSINE / wavelength_nm(spectral_index++);
                    const double HALF_PHASE_SINE = std::sin(ROUND_TRIP_PHASE * 0.5);
                    // Airy interference terms for each polarization of an air/film/air layer.
                    const double S_INTERFERENCE = 4 * S_REFLECTION_AMPLITUDE *
                                                  S_REFLECTION_AMPLITUDE * HALF_PHASE_SINE *
                                                  HALF_PHASE_SINE;
                    const double P_INTERFERENCE = 4 * P_REFLECTION_AMPLITUDE *
                                                  P_REFLECTION_AMPLITUDE * HALF_PHASE_SINE *
                                                  HALF_PHASE_SINE;
                    const double S_TRANSMISSION =
                        1 - (S_REFLECTION_AMPLITUDE * S_REFLECTION_AMPLITUDE);
                    const double P_TRANSMISSION =
                        1 - (P_REFLECTION_AMPLITUDE * P_REFLECTION_AMPLITUDE);
                    const double REFLECTANCE =
                        UNPOLARIZED_WEIGHT *
                        ((S_INTERFERENCE / ((S_TRANSMISSION * S_TRANSMISSION) + S_INTERFERENCE)) +
                            (P_INTERFERENCE /
                                ((P_TRANSMISSION * P_TRANSMISSION) + P_INTERFERENCE)));
                    auto *reflected = reflected_xyz.begin();
                    for (const auto MATCH_VALUE : matching) {
                        *reflected += MATCH_VALUE * REFLECTANCE;
                        ++reflected;
                    }
                }
                const auto REFLECTED_RGB = linear_srgb(reflected_xyz);
                const auto *mirror = PERFECT_MIRROR_RGB.begin();
                for (const auto REFLECTED_VALUE : REFLECTED_RGB) {
                    *output++ =
                        static_cast<float>(std::clamp(REFLECTED_VALUE / *mirror++, 0.0, 1.0));
                }
            }
        }
        return table;
    }
}  // namespace hyprimate
