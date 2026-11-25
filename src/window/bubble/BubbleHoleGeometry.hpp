#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

#include "window/bubble/Bubble.hpp"

namespace hyprimate::detail {
    inline constexpr double PI_VALUE = std::numbers::pi;

    inline Vec3 cross(Vec3 lhs, Vec3 rhs) {
        return {
            .x = (lhs.y * rhs.z) - (lhs.z * rhs.y),
            .y = (lhs.z * rhs.x) - (lhs.x * rhs.z),
            .z = (lhs.x * rhs.y) - (lhs.y * rhs.x),
        };
    }
    inline double dot(Vec3 lhs, Vec3 rhs) {
        return (lhs.x * rhs.x) + (lhs.y * rhs.y) + (lhs.z * rhs.z);
    }
    inline Vec3 normalize(Vec3 lhs) {
        const double LENGTH = std::sqrt(dot(lhs, lhs));
        return LENGTH > 0 ? Vec3{.x = lhs.x / LENGTH, .y = lhs.y / LENGTH, .z = lhs.z / LENGTH}
                          : Vec3{.x = 1, .y = 0, .z = 0};
    }
    inline void basis(Vec3 centre, Vec3 &tangent, Vec3 &bitangent) {
        tangent = normalize(cross(
            std::abs(centre.z) < .9 ? Vec3{.x = 0, .y = 0, .z = 1} : Vec3{.x = 1, .y = 0, .z = 0},
            centre));
        bitangent = cross(centre, tangent);
    }
    inline double radius_at(const BubbleSurface &surface, const BubbleHole &hole, double clock) {
        const double AGE = std::max(0.0, clock - hole.start);
        const double TRANSIENT = surface.spherical ? .05 : .075;
        return surface.speed * (AGE - (TRANSIENT * (1 - std::exp(-AGE / TRANSIENT))));
    }
}  // namespace hyprimate::detail
