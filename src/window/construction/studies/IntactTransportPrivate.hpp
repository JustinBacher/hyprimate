#pragma once

#include <algorithm>
#include <cmath>

#include "IntactTransport.hpp"

// Shared episode math; mode-specific solvers and constants stay in their own units.
namespace hyprimate::construction::studies::intact_transport_detail {
    constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept {
        return {a.x + b.x, a.y + b.y};
    }
    constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept {
        return {a.x - b.x, a.y - b.y};
    }
    constexpr Vec2 operator*(Vec2 a, double s) noexcept {
        return {a.x * s, a.y * s};
    }
    constexpr double right(Box b) noexcept {
        return b.x + b.w;
    }
    constexpr double bottom(Box b) noexcept {
        return b.y + b.h;
    }
    constexpr double dot(Vec2 a, Vec2 b) noexcept {
        return a.x * b.x + a.y * b.y;
    }
    inline double length(Vec2 v) noexcept {
        return std::hypot(v.x, v.y);
    }
    inline bool usable(Box b) noexcept {
        return is_usable(b) && std::isfinite(right(b)) && std::isfinite(bottom(b));
    }
    inline double ease(double x) noexcept {
        return smoothstep(0, 1, x);
    }
    inline double approach(double from, double to, double amount) noexcept {
        return from + std::clamp(to - from, -amount, amount);
    }
}
