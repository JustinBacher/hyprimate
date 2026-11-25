#pragma once

#include <cmath>

#include "window/paper/PaperSurface.hpp"

namespace paper_render_mesh_math {
    using hyprimate::Vec3;
    inline Vec3 subtract(Vec3 a, Vec3 b) {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    inline Vec3 cross(Vec3 a, Vec3 b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    inline Vec3 add(Vec3 lhs, Vec3 rhs) {
        return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    }
    inline Vec3 scale(Vec3 point, double factor) {
        return {point.x * factor, point.y * factor, point.z * factor};
    }
    inline double dot(Vec3 lhs, Vec3 rhs) {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }
    inline Vec3 normalized(Vec3 point) {
        const double LENGTH = std::sqrt(dot(point, point));
        return LENGTH > 1e-15 ? scale(point, 1 / LENGTH) : Vec3{0, 0, 1};
    }
}
