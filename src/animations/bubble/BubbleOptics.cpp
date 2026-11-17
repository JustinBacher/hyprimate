#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include "animations/bubble/BubbleOptics.hpp"

namespace hyprimate {
    namespace {
        constexpr int kColourChannels = 3;
        using Colour = std::array<double, kColourChannels>;

        constexpr int kFirstWavelengthNm = 380;
        constexpr int kLastWavelengthNm = 780;
        constexpr int kWavelengthStepNm = 5;
        constexpr int kWavelengthSamples =
            ((kLastWavelengthNm - kFirstWavelengthNm) / kWavelengthStepNm) + 1;
        constexpr double kFilmRefractiveIndex =
            1.33;  // Water-like soap film; surrounding air is 1.
        constexpr double kUnpolarizedWeight =
            0.5;  // Equal contributions from s and p polarizations.
        constexpr double kRoundTripPhaseFactor =
            4.0 * std::numbers::pi;  // 2pi times two film traversals.

        // Asymmetric Gaussian fits to the CIE 1931 XYZ colour matching functions.
        // Wyman, Sloan and Shirley (2013), Table 1: https://jcgt.org/published/0002/02/01/paper.pdf
        // These are fitted coefficients, not animation tuning parameters.
        struct MatchingLobe {
            double amplitude;
            double centreNm;
            double leftInverseWidthPerNm;
            double rightInverseWidthPerNm;
        };
        constexpr MatchingLobe kXLongWavelengthLobe{.amplitude = 1.056,
            .centreNm = 599.8,
            .leftInverseWidthPerNm = 0.0264,
            .rightInverseWidthPerNm = 0.0323};
        constexpr MatchingLobe kXShortWavelengthLobe{.amplitude = 0.362,
            .centreNm = 442.0,
            .leftInverseWidthPerNm = 0.0624,
            .rightInverseWidthPerNm = 0.0374};
        constexpr MatchingLobe kXSubtractiveLobe{.amplitude = 0.065,
            .centreNm = 501.1,
            .leftInverseWidthPerNm = 0.0490,
            .rightInverseWidthPerNm = 0.0382};
        constexpr MatchingLobe kYLongWavelengthLobe{.amplitude = 0.821,
            .centreNm = 568.8,
            .leftInverseWidthPerNm = 0.0213,
            .rightInverseWidthPerNm = 0.0247};
        constexpr MatchingLobe kYShortWavelengthLobe{.amplitude = 0.286,
            .centreNm = 530.9,
            .leftInverseWidthPerNm = 0.0613,
            .rightInverseWidthPerNm = 0.0322};
        constexpr MatchingLobe kZShortWavelengthLobe{.amplitude = 1.217,
            .centreNm = 437.0,
            .leftInverseWidthPerNm = 0.0845,
            .rightInverseWidthPerNm = 0.0278};
        constexpr MatchingLobe kZLongWavelengthLobe{.amplitude = 0.681,
            .centreNm = 459.0,
            .leftInverseWidthPerNm = 0.0385,
            .rightInverseWidthPerNm = 0.0725};

        double matchingLobe(double wavelengthNm, const MatchingLobe &lobe) {
            const double INVERSE_WIDTH = wavelengthNm < lobe.centreNm ? lobe.leftInverseWidthPerNm
                                                                      : lobe.rightInverseWidthPerNm;
            const double NORMALIZED_OFFSET = (wavelengthNm - lobe.centreNm) * INVERSE_WIDTH;
            // Unit Gaussian exp(-x^2 / 2), scaled by the fitted amplitude.
            return lobe.amplitude * std::exp(-0.5 * NORMALIZED_OFFSET * NORMALIZED_OFFSET);
        }

        Colour colourMatching(double wavelengthNm) {
            return {matchingLobe(wavelengthNm, kXLongWavelengthLobe) +
                        matchingLobe(wavelengthNm, kXShortWavelengthLobe) -
                        matchingLobe(wavelengthNm, kXSubtractiveLobe),
                matchingLobe(wavelengthNm, kYLongWavelengthLobe) +
                    matchingLobe(wavelengthNm, kYShortWavelengthLobe),
                matchingLobe(wavelengthNm, kZShortWavelengthLobe) +
                    matchingLobe(wavelengthNm, kZLongWavelengthLobe)};
        }

        // CIE XYZ to linear sRGB (D65) conversion: rows are red, green and blue;
        // columns are X, Y and Z. No display gamma is applied to this reflectance table.
        constexpr std::array<Colour, kColourChannels> kXyzToLinearSrgb{{
            {3.2406, -1.5372, -0.4986},
            {-0.9689, 1.8758, 0.0415},
            {0.0557, -0.2040, 1.0570},
        }};

        Colour linearSrgb(const Colour &xyz) {
            Colour result{};
            for (int channel = 0; channel < kColourChannels; ++channel) {
                const auto &row = kXyzToLinearSrgb[channel];
                result[channel] = (row[0] * xyz[0]) + (row[1] * xyz[1]) + (row[2] * xyz[2]);
            }
            return result;
        }

        constexpr double wavelengthNm(int index) {
            return kFirstWavelengthNm + (kWavelengthStepNm * index);
        }
    }  // namespace

    BubbleThinFilmTable bubbleThinFilmTable(
        int thicknessSamples, int cosineSamples, double maxThickness) {
        constexpr int K_MINIMUM_GRID_SAMPLES = 2;  // Both endpoints must be represented.
        if (thicknessSamples < K_MINIMUM_GRID_SAMPLES || cosineSamples < K_MINIMUM_GRID_SAMPLES ||
            !std::isfinite(maxThickness) || maxThickness <= 0) {
            return {};
        }
        BubbleThinFilmTable table{.thicknessSamples = thicknessSamples,
            .cosineSamples = cosineSamples,
            .maxThickness = maxThickness,
            .rgb = std::vector<float>(
                static_cast<std::size_t>(thicknessSamples) * cosineSamples * kColourChannels)};
        // Colour matching is evaluated once per wavelength, not once per table cell.
        std::array<Colour, kWavelengthSamples> matchingByWavelength{};
        Colour equalEnergyWhiteXyz{};
        for (int i = 0; i < kWavelengthSamples; ++i) {
            matchingByWavelength[i] = colourMatching(wavelengthNm(i));
            for (int channel = 0; channel < kColourChannels; ++channel) {
                equalEnergyWhiteXyz[channel] += matchingByWavelength[i][channel];
            }
        }
        // Normalize each channel against a perfect reflector under an equal-energy spectrum.
        const auto PERFECT_MIRROR_RGB = linearSrgb(equalEnergyWhiteXyz);
        for (int y = 0; y < cosineSamples; ++y) {
            const double INCIDENCE_COSINE = static_cast<double>(y) / (cosineSamples - 1);
            // Snell's law, with air's refractive index treated as one.
            const double TRANSMISSION_COSINE =
                std::sqrt(1.0 - ((1.0 - (INCIDENCE_COSINE * INCIDENCE_COSINE)) /
                                    (kFilmRefractiveIndex * kFilmRefractiveIndex)));
            const double S_REFLECTION_AMPLITUDE =
                (INCIDENCE_COSINE - (kFilmRefractiveIndex * TRANSMISSION_COSINE)) /
                (INCIDENCE_COSINE + (kFilmRefractiveIndex * TRANSMISSION_COSINE));
            const double P_REFLECTION_AMPLITUDE =
                ((kFilmRefractiveIndex * INCIDENCE_COSINE) - TRANSMISSION_COSINE) /
                ((kFilmRefractiveIndex * INCIDENCE_COSINE) + TRANSMISSION_COSINE);
            // The zero-thickness column remains zero: the two interfaces cancel exactly.
            for (int x = 1; x < thicknessSamples; ++x) {
                const double THICKNESS_NM = maxThickness * x / (thicknessSamples - 1);
                Colour reflectedXyz{};
                for (int i = 0; i < kWavelengthSamples; ++i) {
                    const double ROUND_TRIP_PHASE = kRoundTripPhaseFactor * kFilmRefractiveIndex *
                                                    THICKNESS_NM * TRANSMISSION_COSINE /
                                                    wavelengthNm(i);
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
                        kUnpolarizedWeight *
                        ((S_INTERFERENCE / ((S_TRANSMISSION * S_TRANSMISSION) + S_INTERFERENCE)) +
                            (P_INTERFERENCE /
                                ((P_TRANSMISSION * P_TRANSMISSION) + P_INTERFERENCE)));
                    for (int channel = 0; channel < kColourChannels; ++channel) {
                        reflectedXyz[channel] += matchingByWavelength[i][channel] * REFLECTANCE;
                    }
                }
                const auto REFLECTED_RGB = linearSrgb(reflectedXyz);
                for (int channel = 0; channel < kColourChannels; ++channel) {
                    table.rgb[(kColourChannels *
                                  ((static_cast<std::size_t>(y) * thicknessSamples) + x)) +
                              channel] =
                        static_cast<float>(std::clamp(
                            REFLECTED_RGB[channel] / PERFECT_MIRROR_RGB[channel], 0.0, 1.0));
                }
            }
        }
        return table;
    }
}  // namespace hyprimate
