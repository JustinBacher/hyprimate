#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

#include "window/bubble/BubbleDeformation.hpp"
#include "window/bubble/BubbleMotionPrivate.hpp"

namespace hyprimate {
    using namespace bubble_motion_detail;
    bool BubbleDeformation::still() const noexcept {
        return drag.x == 0 && drag.y == 0 && sheen == 0 && round == 0 && carry.x == 0 &&
               carry.y == 0 &&
               std::ranges::all_of(wave_cos, [](double component) { return component == 0; }) &&
               std::ranges::all_of(wave_sin, [](double value) { return value == 0; }) &&
               std::ranges::all_of(buds, [](const BubbleBud &bud) { return bud.height == 0; });
    }

    Vec2 bubble_round_half(Vec2 half_size, const BubbleDeformation &deformation) {
        const Vec2 HALF{.x = std::max(half_size.x, 1.0), .y = std::max(half_size.y, 1.0)};
        const Vec2 EXTENT{
            .x = std::max(deformation.extent.x, 1.0),
            .y = std::max(deformation.extent.y, 1.0),
        };
        return {
            .x = HALF.x + ((EXTENT.x - HALF.x) * deformation.round),
            .y = HALF.y + ((EXTENT.y - HALF.y) * deformation.round),
        };
    }

    Vec2 bubble_round_point(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point) {
        if (deformation.round == 0) {
            return point;
        }
        const Vec2 HALF{.x = std::max(half_size.x, 1.0), .y = std::max(half_size.y, 1.0)};
        const double NORM_X = std::clamp(point.x / HALF.x, -1.5, 1.5);
        const double NORM_Y = std::clamp(point.y / HALF.y, -1.5, 1.5);
        const Vec2 DISC{
            .x = deformation.extent.x * NORM_X * std::sqrt(std::max(0.0, 1 - NORM_Y * NORM_Y * .5)),
            .y = deformation.extent.y * NORM_Y * std::sqrt(std::max(0.0, 1 - NORM_X * NORM_X * .5)),
        };
        return add(point, mul(sub(DISC, point), deformation.round));
    }

    Vec2 bubble_motion_point(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point) {
        const Vec2 ROUNDED = bubble_round_point(half_size, deformation, point);
        return add(add(ROUNDED,
                       bubble_motion_offset(
                           bubble_round_half(half_size, deformation), deformation, ROUNDED)),
            deformation.carry);
    }

    Vec2 bubble_motion_offset(Vec2 half_size, const BubbleDeformation &deformation, Vec2 point) {
        const Vec2 HALF{.x = std::max(half_size.x, 1.0), .y = std::max(half_size.y, 1.0)};
        Vec2 offset{};
        const double DRAG = length(deformation.drag);
        if (DRAG > 1e-6) {
            const Vec2 DIR = mul(deformation.drag, 1 / DRAG);
            const double REACH = (std::abs(DIR.x) * HALF.x) + (std::abs(DIR.y) * HALF.y);
            const double BREADTH = (std::abs(DIR.y) * HALF.x) + (std::abs(DIR.x) * HALF.y);
            const double ALONG = dot(point, DIR);
            const double WEIGHT = smoothstep(-1.0, 1.0, std::clamp(ALONG / REACH, -1.0, 1.0));
            const double LEAD = smoothstep(0.0, 1.0, std::clamp(-ALONG / REACH, 0.0, 1.0));
            const Vec2 ACROSS = sub(point, mul(DIR, ALONG));
            // The middle of the trailing side lags most, so it bows out like a drop's tail while
            // the corners stay closer to the frame; the middle of the leading face is pressed back.
            const double SIDE = std::min(1.0, dot(ACROSS, ACROSS) / (BREADTH * BREADTH));
            offset = sub(mul(deformation.drag,
                             (WEIGHT * (1 - (.55 * SIDE))) + (LEAD_FLATTEN * LEAD * (1 - SIDE))),
                mul(ACROSS, .35 * DRAG / REACH * WEIGHT));
        }
        // Ripples run normal to the outline: a rounded rectangle's, turning into a circle's.
        const double ROUND = std::clamp(deformation.round, 0.0, 1.0);
        const Vec2 NORMALIZED{.x = point.x / HALF.x, .y = point.y / HALF.y};
        const Vec2 SQUARED{.x = NORMALIZED.x * NORMALIZED.x, .y = NORMALIZED.y * NORMALIZED.y};
        const double RHO4 = dot(SQUARED, SQUARED);
        if (RHO4 > 1e-12) {
            const double RHO = std::min((std::sqrt(std::sqrt(RHO4)) * (1 - ROUND)) +
                                            (std::sqrt(dot(NORMALIZED, NORMALIZED)) * ROUND),
                1.25);
            const double PHI = std::atan2(NORMALIZED.y, NORMALIZED.x);
            double radial = 0;
            const auto *cosine = deformation.wave_cos.begin();
            const auto *sine = deformation.wave_sin.begin();
            for (const double MODE : MODES) {
                radial += (*cosine++ * std::cos(MODE * PHI)) + (*sine++ * std::sin(MODE * PHI));
            }
            const Vec2 GRADIENT = outward(HALF, ROUND, point);
            const double GRADIENT_LENGTH = length(GRADIENT);
            if (GRADIENT_LENGTH > 0) {
                offset = add(offset, mul(GRADIENT, radial * RHO * RHO * RHO / GRADIENT_LENGTH));
            }
        }
        // Buds: the film swells outward around each foot, along the outline's normal there.
        for (const auto &bud : deformation.buds) {
            if (!(bud.height > 0) || !(bud.width > 0)) {
                continue;
            }
            const Vec2 DELTA = sub(point, bud.foot);
            const double FACTOR = std::exp(-dot(DELTA, DELTA) / (bud.width * bud.width));
            if (FACTOR < 1e-4) {
                continue;
            }
            const Vec2 NORMAL = outward(HALF, ROUND, bud.foot);
            const double NORMAL_LENGTH = length(NORMAL);
            if (NORMAL_LENGTH > 0) {
                offset = add(offset, mul(NORMAL, bud.height * FACTOR / NORMAL_LENGTH));
            }
        }
        return offset;
    }

}  // namespace hyprimate
