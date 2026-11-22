#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "window/swarm/SwarmSprite.hpp"

namespace hyprimate {
    std::string sprite_path(SpriteName value, SpriteDirectory directory) {
        if (value.value.empty() || value.value.contains('/')) {
            return std::string(value.value);
        }
        std::string result(directory.value);
        result += '/';
        result += value.value;
        if (!value.value.ends_with(".png")) {
            result += ".png";
        }
        return result;
    }

    namespace {

        constexpr std::uint8_t SOLID = 128;
        constexpr double FAR = 1e18;
        constexpr double COVER_MARGIN =
            1.06;  // the core overshoots its tile slightly, so coverage is complete before lock 1

        // One pass of the exact squared Euclidean distance transform (lower envelope of parabolas)
        // over n samples with the given stride. `sites` and `boundaries` are scratch of n and n + 1
        // entries, `out` of n.
        void distance_pass(std::span<double> field,
            std::size_t offset,
            std::size_t stride,
            int n,
            std::vector<int> &sites,
            std::vector<double> &boundaries,
            std::vector<double> &out) {
            const auto SAMPLE = [&](int index) {
                return field[offset + (static_cast<std::size_t>(index) * stride)];
            };
            int envelope_index = 0;
            sites[0] = 0;
            boundaries[0] = -FAR;
            boundaries[1] = FAR;
            for (int candidate_index = 1; candidate_index < n; ++candidate_index) {
                double intersection = 0.0;
                while (true) {
                    const int PREVIOUS_SITE = sites[envelope_index];
                    intersection = ((SAMPLE(candidate_index) +
                                        (static_cast<double>(candidate_index) * candidate_index)) -
                                       (SAMPLE(PREVIOUS_SITE) +
                                           (static_cast<double>(PREVIOUS_SITE) * PREVIOUS_SITE))) /
                                   (2.0 * (candidate_index - PREVIOUS_SITE));
                    if (intersection > boundaries[envelope_index] || envelope_index == 0) {
                        break;
                    }
                    --envelope_index;
                }
                if (intersection > boundaries[envelope_index]) {
                    ++envelope_index;
                }
                sites[envelope_index] = candidate_index;
                boundaries[envelope_index] = intersection;
                boundaries[envelope_index + 1] = FAR;
            }
            envelope_index = 0;
            for (int query_index = 0; query_index < n; ++query_index) {
                while (boundaries[envelope_index + 1] < query_index) {
                    ++envelope_index;
                }
                const int SITE_INDEX = sites[envelope_index];
                out[query_index] =
                    (static_cast<double>(query_index - SITE_INDEX) * (query_index - SITE_INDEX)) +
                    SAMPLE(SITE_INDEX);
            }
            for (int sample_index = 0; sample_index < n; ++sample_index) {
                field[offset + (static_cast<std::size_t>(sample_index) * stride)] =
                    out[sample_index];
            }
        }

    }  // namespace

    SpriteFrame measure_sprite(std::span<const std::uint8_t> alpha, int side) {
        SpriteFrame frame;
        if (side <= 0 || side > std::numeric_limits<int>::max() - 2 ||
            static_cast<std::size_t>(side) > alpha.size() / static_cast<std::size_t>(side)) {
            return frame;
        }

        // Squared distance from every texel to the nearest empty one. A ring of empty texels stands
        // in for everything outside the frame.
        const int PADDED_SIDE = side + 2;
        const auto EXTENT = static_cast<std::size_t>(PADDED_SIDE);
        std::vector<double> field;
        if (EXTENT > field.max_size() / EXTENT) {
            return frame;
        }
        field.resize(EXTENT * EXTENT);
        bool any = false;
        for (int alpha_row = 0; alpha_row < side; ++alpha_row) {
            for (int alpha_col = 0; alpha_col < side; ++alpha_col) {
                if (alpha[(static_cast<std::size_t>(alpha_row) * side) + alpha_col] >= SOLID) {
                    field[(static_cast<std::size_t>(alpha_row + 1) * PADDED_SIDE) + alpha_col + 1] =
                        FAR;
                    any = true;
                }
            }
        }
        if (!any) {
            return frame;
        }

        std::vector<int> sites(PADDED_SIDE);
        std::vector<double> boundaries(EXTENT + 1);
        std::vector<double> out(PADDED_SIDE);
        for (int field_row = 0; field_row < PADDED_SIDE; ++field_row) {
            distance_pass(field,
                static_cast<std::size_t>(field_row) * PADDED_SIDE,
                1,
                PADDED_SIDE,
                sites,
                boundaries,
                out);
        }
        for (int field_col = 0; field_col < PADDED_SIDE; ++field_col) {
            distance_pass(field,
                static_cast<std::size_t>(field_col),
                static_cast<std::size_t>(PADDED_SIDE),
                PADDED_SIDE,
                sites,
                boundaries,
                out);
        }

        double deepest = 0.0;
        int core_x = 0;
        int core_y = 0;
        for (int core_row = 0; core_row < side; ++core_row) {
            for (int core_col = 0; core_col < side; ++core_col) {
                const double DISTANCE_SQUARED =
                    field[(static_cast<std::size_t>(core_row + 1) * PADDED_SIDE) + core_col + 1];
                if (DISTANCE_SQUARED > deepest) {
                    deepest = DISTANCE_SQUARED;
                    core_x = core_col;
                    core_y = core_row;
                }
            }
        }
        double farthest = 0.0;
        for (int row = 0; row < side; ++row) {
            for (int col = 0; col < side; ++col) {
                if (alpha[(static_cast<std::size_t>(row) * side) + col] >= SOLID) {
                    farthest = std::max(farthest, std::hypot(col - core_x, row - core_y));
                }
            }
        }

        // Texel centres; the solid disc stops half a texel short of the nearest empty centre, and
        // the silhouette runs half a texel past its farthest solid centre.
        frame.centre = {.x = (core_x + 0.5) / side, .y = (core_y + 0.5) / side};
        frame.core = std::max(0.0, std::sqrt(deepest) - 0.5) / side;
        frame.reach = (farthest + 0.5) / side;
        return frame;
    }

    int walk_frame(double travel, double stride, int frames) noexcept {
        if (frames <= 1 || !(stride > 0.0) || !std::isfinite(travel)) {
            return 0;
        }
        double cycles = travel / stride;
        // Beyond 2^52 a double cannot retain a fractional cycle. Reduce the distance
        // before division in this rare case, including when the quotient overflowed.
        if (std::abs(cycles) >= 0x1p52) {
            cycles = std::fmod(travel, stride) / stride;
        }
        return std::clamp(static_cast<int>((cycles - std::floor(cycles)) * frames), 0, frames - 1);
    }

    double sprite_rotation(double heading, double from, double turn) noexcept {
        return std::clamp(turn, 0.0, 1.0) * std::remainder(heading - from, 2.0 * std::numbers::pi);
    }

    double sprite_size(
        const SpriteFrame &frame, SpriteFit fit, SpriteCover cover, SpriteLock lock) noexcept {
        if (!(frame.reach > 0.0) || !(frame.core > 0.0) || !(fit.value > 0.0)) {
            return 0.0;
        }
        const double LOOSE = fit.value / frame.reach;
        const double WHOLE = std::max(LOOSE, COVER_MARGIN * cover.value / frame.core);
        return LOOSE * std::pow(WHOLE / LOOSE, std::clamp(lock.value, 0.0, 1.0));
    }

}  // namespace hyprimate
