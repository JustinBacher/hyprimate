#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "animations/bubble/BubbleOptics.hpp"

namespace hyprimate {
    namespace {
        constexpr int kColourChannels = 3;
        using Colour = std::array<double, kColourChannels>;

        constexpr int kFirstWavelengthNm = 380;
        constexpr int kLastWavelengthNm = 780;
        constexpr int kWavelengthStepNm = 5;
        constexpr int kWavelengthSamples =
            (kLastWavelengthNm - kFirstWavelengthNm) / kWavelengthStepNm + 1;
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
        constexpr MatchingLobe kXLongWavelengthLobe{1.056, 599.8, 0.0264, 0.0323};
        constexpr MatchingLobe kXShortWavelengthLobe{0.362, 442.0, 0.0624, 0.0374};
        constexpr MatchingLobe kXSubtractiveLobe{0.065, 501.1, 0.0490, 0.0382};
        constexpr MatchingLobe kYLongWavelengthLobe{0.821, 568.8, 0.0213, 0.0247};
        constexpr MatchingLobe kYShortWavelengthLobe{0.286, 530.9, 0.0613, 0.0322};
        constexpr MatchingLobe kZShortWavelengthLobe{1.217, 437.0, 0.0845, 0.0278};
        constexpr MatchingLobe kZLongWavelengthLobe{0.681, 459.0, 0.0385, 0.0725};

        double matchingLobe(double wavelengthNm, const MatchingLobe &lobe) {
            const double inverseWidth = wavelengthNm < lobe.centreNm ? lobe.leftInverseWidthPerNm
                                                                     : lobe.rightInverseWidthPerNm;
            const double normalizedOffset = (wavelengthNm - lobe.centreNm) * inverseWidth;
            // Unit Gaussian exp(-x^2 / 2), scaled by the fitted amplitude.
            return lobe.amplitude * std::exp(-0.5 * normalizedOffset * normalizedOffset);
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
                result[channel] = row[0] * xyz[0] + row[1] * xyz[1] + row[2] * xyz[2];
            }
            return result;
        }

        constexpr double wavelengthNm(int index) {
            return kFirstWavelengthNm + kWavelengthStepNm * index;
        }
    }  // namespace

    BubbleThinFilmTable bubbleThinFilmTable(
        int thicknessSamples, int cosineSamples, double maxThickness) {
        constexpr int kMinimumGridSamples = 2;  // Both endpoints must be represented.
        if (thicknessSamples < kMinimumGridSamples || cosineSamples < kMinimumGridSamples ||
            !std::isfinite(maxThickness) || maxThickness <= 0)
            return {};
        BubbleThinFilmTable table{thicknessSamples,
            cosineSamples,
            maxThickness,
            std::vector<float>(std::size_t(thicknessSamples) * cosineSamples * kColourChannels)};
        // Colour matching is evaluated once per wavelength, not once per table cell.
        std::array<Colour, kWavelengthSamples> matchingByWavelength{};
        Colour equalEnergyWhiteXyz{};
        for (int i = 0; i < kWavelengthSamples; ++i) {
            matchingByWavelength[i] = colourMatching(wavelengthNm(i));
            for (int channel = 0; channel < kColourChannels; ++channel)
                equalEnergyWhiteXyz[channel] += matchingByWavelength[i][channel];
        }
        // Normalize each channel against a perfect reflector under an equal-energy spectrum.
        const auto perfectMirrorRgb = linearSrgb(equalEnergyWhiteXyz);
        for (int y = 0; y < cosineSamples; ++y) {
            const double incidenceCosine = double(y) / (cosineSamples - 1);
            // Snell's law, with air's refractive index treated as one.
            const double transmissionCosine =
                std::sqrt(1.0 - (1.0 - incidenceCosine * incidenceCosine) /
                                    (kFilmRefractiveIndex * kFilmRefractiveIndex));
            const double sReflectionAmplitude =
                (incidenceCosine - kFilmRefractiveIndex * transmissionCosine) /
                (incidenceCosine + kFilmRefractiveIndex * transmissionCosine);
            const double pReflectionAmplitude =
                (kFilmRefractiveIndex * incidenceCosine - transmissionCosine) /
                (kFilmRefractiveIndex * incidenceCosine + transmissionCosine);
            // The zero-thickness column remains zero: the two interfaces cancel exactly.
            for (int x = 1; x < thicknessSamples; ++x) {
                const double thicknessNm = maxThickness * x / (thicknessSamples - 1);
                Colour reflectedXyz{};
                for (int i = 0; i < kWavelengthSamples; ++i) {
                    const double roundTripPhase = kRoundTripPhaseFactor * kFilmRefractiveIndex *
                                                  thicknessNm * transmissionCosine /
                                                  wavelengthNm(i);
                    const double halfPhaseSine = std::sin(roundTripPhase * 0.5);
                    // Airy interference terms for each polarization of an air/film/air layer.
                    const double sInterference = 4 * sReflectionAmplitude * sReflectionAmplitude *
                                                 halfPhaseSine * halfPhaseSine;
                    const double pInterference = 4 * pReflectionAmplitude * pReflectionAmplitude *
                                                 halfPhaseSine * halfPhaseSine;
                    const double sTransmission = 1 - sReflectionAmplitude * sReflectionAmplitude;
                    const double pTransmission = 1 - pReflectionAmplitude * pReflectionAmplitude;
                    const double reflectance =
                        kUnpolarizedWeight *
                        (sInterference / (sTransmission * sTransmission + sInterference) +
                            pInterference / (pTransmission * pTransmission + pInterference));
                    for (int channel = 0; channel < kColourChannels; ++channel)
                        reflectedXyz[channel] += matchingByWavelength[i][channel] * reflectance;
                }
                const auto reflectedRgb = linearSrgb(reflectedXyz);
                for (int channel = 0; channel < kColourChannels; ++channel)
                    table.rgb[kColourChannels * (std::size_t(y) * thicknessSamples + x) + channel] =
                        float(std::clamp(
                            reflectedRgb[channel] / perfectMirrorRgb[channel], 0.0, 1.0));
            }
        }
        return table;
    }
}  // namespace hyprimate
