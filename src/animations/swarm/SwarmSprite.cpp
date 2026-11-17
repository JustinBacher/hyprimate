#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animations/swarm/SwarmSprite.hpp"

namespace hyprimate {
    std::string spritePath(std::string_view value, std::string_view directory) {
        if (value.empty() || value.find('/') != std::string_view::npos) {
            return std::string(value);
        }
        std::string result(directory);
        result += '/';
        result += value;
        if (!value.ends_with(".png")) {
            result += ".png";
        }
        return result;
    }

    namespace {

        constexpr std::uint8_t kSolid = 128;
        constexpr double kFar = 1e18;
        constexpr double kCoverMargin =
            1.06;  // the core overshoots its tile slightly, so coverage is complete before lock 1

        // One pass of the exact squared Euclidean distance transform (lower envelope of parabolas)
        // over n samples with the given stride. `v` and `z` are scratch of n and n + 1 entries,
        // `out` of n.
        void distancePass(double *f,
            std::size_t stride,
            int n,
            std::vector<int> &v,
            std::vector<double> &z,
            std::vector<double> &out) {
            const auto SAMPLE = [&](int i) { return f[static_cast<std::size_t>(i) * stride]; };
            int envelopeIndex = 0;
            v[0] = 0;
            z[0] = -kFar;
            z[1] = kFar;
            for (int candidateIndex = 1; candidateIndex < n; ++candidateIndex) {
                double intersection = 0.0;
                while (true) {
                    const int PREVIOUS_SITE = v[envelopeIndex];
                    intersection = ((SAMPLE(candidateIndex) +
                                        (static_cast<double>(candidateIndex) * candidateIndex)) -
                                       (SAMPLE(PREVIOUS_SITE) +
                                           (static_cast<double>(PREVIOUS_SITE) * PREVIOUS_SITE))) /
                                   (2.0 * (candidateIndex - PREVIOUS_SITE));
                    if (intersection > z[envelopeIndex] || envelopeIndex == 0) {
                        break;
                    }
                    --envelopeIndex;
                }
                if (intersection > z[envelopeIndex]) {
                    ++envelopeIndex;
                }
                v[envelopeIndex] = candidateIndex;
                z[envelopeIndex] = intersection;
                z[envelopeIndex + 1] = kFar;
            }
            envelopeIndex = 0;
            for (int queryIndex = 0; queryIndex < n; ++queryIndex) {
                while (z[envelopeIndex + 1] < queryIndex) {
                    ++envelopeIndex;
                }
                const int SITE_INDEX = v[envelopeIndex];
                out[queryIndex] =
                    (static_cast<double>(queryIndex - SITE_INDEX) * (queryIndex - SITE_INDEX)) +
                    SAMPLE(SITE_INDEX);
            }
            for (int sampleIndex = 0; sampleIndex < n; ++sampleIndex) {
                f[static_cast<std::size_t>(sampleIndex) * stride] = out[sampleIndex];
            }
        }

    }  // namespace

    SpriteFrame measureSprite(std::span<const std::uint8_t> alpha, int side) {
        SpriteFrame frame;
        if (side <= 0 || alpha.size() < static_cast<std::size_t>(side) * side) {
            return frame;
        }

        // Squared distance from every texel to the nearest empty one. A ring of empty texels stands
        // in for everything outside the frame.
        const int PADDED_SIDE = side + 2;
        std::vector<double> field(static_cast<std::size_t>(PADDED_SIDE) * PADDED_SIDE, 0.0);
        bool any = false;
        for (int alphaRow = 0; alphaRow < side; ++alphaRow) {
            for (int alphaCol = 0; alphaCol < side; ++alphaCol) {
                if (alpha[(static_cast<std::size_t>(alphaRow) * side) + alphaCol] >= kSolid) {
                    field[(static_cast<std::size_t>(alphaRow + 1) * PADDED_SIDE) + alphaCol + 1] =
                        kFar;
                    any = true;
                }
            }
        }
        if (!any) {
            return frame;
        }

        std::vector<int> sites(PADDED_SIDE);
        std::vector<double> boundaries(PADDED_SIDE + 1);
        std::vector<double> out(PADDED_SIDE);
        for (int fieldRow = 0; fieldRow < PADDED_SIDE; ++fieldRow) {
            distancePass(field.data() + (static_cast<std::size_t>(fieldRow) * PADDED_SIDE),
                1,
                PADDED_SIDE,
                sites,
                boundaries,
                out);
        }
        for (int fieldCol = 0; fieldCol < PADDED_SIDE; ++fieldCol) {
            distancePass(field.data() + fieldCol,
                static_cast<std::size_t>(PADDED_SIDE),
                PADDED_SIDE,
                sites,
                boundaries,
                out);
        }

        double deepest = 0.0;
        int coreX = 0;
        int coreY = 0;
        for (int coreRow = 0; coreRow < side; ++coreRow) {
            for (int coreCol = 0; coreCol < side; ++coreCol) {
                const double DISTANCE_SQUARED =
                    field[(static_cast<std::size_t>(coreRow + 1) * PADDED_SIDE) + coreCol + 1];
                if (DISTANCE_SQUARED > deepest) {
                    deepest = DISTANCE_SQUARED;
                    coreX = coreCol;
                    coreY = coreRow;
                }
            }
        }
        double farthest = 0.0;
        for (int row = 0; row < side; ++row) {
            for (int col = 0; col < side; ++col) {
                if (alpha[(static_cast<std::size_t>(row) * side) + col] >= kSolid) {
                    farthest = std::max(farthest, std::hypot(col - coreX, row - coreY));
                }
            }
        }

        // Texel centres; the solid disc stops half a texel short of the nearest empty centre, and
        // the silhouette runs half a texel past its farthest solid centre.
        frame.centre = {.x = (coreX + 0.5) / side, .y = (coreY + 0.5) / side};
        frame.core = std::max(0.0, std::sqrt(deepest) - 0.5) / side;
        frame.reach = (farthest + 0.5) / side;
        return frame;
    }

    int walkFrame(double travel, double stride, int frames) noexcept {
        if (frames <= 1 || !(stride > 0.0) || !std::isfinite(travel)) {
            return 0;
        }
        const double CYCLES = travel / stride;
        return std::clamp(static_cast<int>((CYCLES - std::floor(CYCLES)) * frames), 0, frames - 1);
    }

    double spriteRotation(double heading, double from, double turn) noexcept {
        return std::clamp(turn, 0.0, 1.0) * std::remainder(heading - from, 2.0 * std::numbers::pi);
    }

    double spriteSize(const SpriteFrame &frame, double fit, double cover, double lock) noexcept {
        if (!(frame.reach > 0.0) || !(frame.core > 0.0) || !(fit > 0.0)) {
            return 0.0;
        }
        const double LOOSE = fit / frame.reach;
        const double WHOLE = std::max(LOOSE, kCoverMargin * cover / frame.core);
        return LOOSE * std::pow(WHOLE / LOOSE, std::clamp(lock, 0.0, 1.0));
    }

}  // namespace hyprimate
