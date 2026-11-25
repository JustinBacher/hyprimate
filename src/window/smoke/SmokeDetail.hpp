#pragma once

#include <algorithm>
#include <cmath>

#include "window/smoke/Smoke.hpp"

namespace hyprimate::smoke_detail {
    inline constexpr double STEP = SUBSTEP_SECONDS;
    inline constexpr double MAX_SPEED = 3000.0;

    inline Vec2 add(Vec2 a, Vec2 b) noexcept {
        return {.x = a.x + b.x, .y = a.y + b.y};
    }
    inline Vec2 sub(Vec2 a, Vec2 b) noexcept {
        return {.x = a.x - b.x, .y = a.y - b.y};
    }
    inline Vec2 mul(Vec2 a, double b) noexcept {
        return {.x = a.x * b, .y = a.y * b};
    }
    inline double length(Vec2 a) noexcept {
        return std::hypot(a.x, a.y);
    }
    inline Vec2 limit(Vec2 a, double maximum) noexcept {
        const double MAGNITUDE = length(a);
        return MAGNITUDE > maximum ? mul(a, maximum / MAGNITUDE) : a;
    }
    inline Vec2 safe_size(Vec2 size) noexcept {
        return {.x = std::isfinite(size.x) ? std::clamp(size.x, 1.0, 32768.0) : 1.0,
            .y = std::isfinite(size.y) ? std::clamp(size.y, 1.0, 32768.0) : 1.0};
    }
    inline double blend(double a, double b, double t) noexcept {
        return a + ((b - a) * t);
    }
    inline Vec2 blend(Vec2 a, Vec2 b, double t) noexcept {
        return {.x = blend(a.x, b.x, t), .y = blend(a.y, b.y, t)};
    }

    // Fully contracted body: roundness evens out the proportions at constant area and
    // rounds the corners; size scales both axes. Neither changes the other.
    struct Contracted {
        Vec2 half;
        double corner = 0;
    };
    inline Contracted contracted(Vec2 window, double mass_size, double roundness) noexcept {
        const double SCALE = std::clamp(mass_size, 0.05, 1.0);
        const double ROUND = std::clamp(roundness, 0.0, 1.0);
        const Vec2 SCALED{.x = window.x * SCALE, .y = window.y * SCALE};
        const double EVEN = std::sqrt(SCALED.x * SCALED.y);
        const Vec2 HALF{.x = blend(SCALED.x, EVEN, ROUND), .y = blend(SCALED.y, EVEN, ROUND)};
        return {.half = HALF, .corner = blend(0.12, 1.0, ROUND) * std::min(HALF.x, HALF.y)};
    }
}  // namespace hyprimate::smoke_detail
