#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "BurnGround.hpp"

namespace burn_ground_detail {
    using namespace hyprimate;
    inline bool inside(Vec2 point, Box box) noexcept {
        return point.x >= box.x && point.y >= box.y && point.x <= box.x + box.w &&
               point.y <= box.y + box.h;
    }
    struct GridAxis {
        double origin, pitch;
        std::size_t count;

        [[nodiscard]] double boundary(std::size_t index) const noexcept {
            return origin + static_cast<double>(index) * pitch;
        }
        [[nodiscard]] std::pair<std::size_t, std::size_t> crossed(
            double low, double high) const noexcept {
            if (!(low < high)) return {};
            const double LIMIT = static_cast<double>(count);
            const double FIRST = std::clamp((low - origin) / pitch, 0., LIMIT);
            const double LAST = std::clamp((high - origin) / pitch, 0., LIMIT);
            return {static_cast<std::size_t>(std::floor(FIRST)),
                static_cast<std::size_t>(std::ceil(LAST))};
        }
    };
    inline float scorch_damage(
        float severity, double age, double lifetime, double healing) noexcept {
        if (severity <= 0) return 0;
        return severity * static_cast<float>(
                              1.0 - smoothstep(lifetime, lifetime + std::max(.001, healing), age));
    }
}  // namespace burn_ground_detail
