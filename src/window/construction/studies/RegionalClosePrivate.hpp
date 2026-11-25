#pragma once

#include <cmath>

#include "RegionalClose.hpp"

namespace hyprimate::construction::studies::regional_close_detail {
// The approved implosion-06 camera starts mid-check, then holds a beat before
// the local front descends. Keep all choreography on this normalized clock.
inline constexpr double CUE = .30;
inline Vec3 mix(Vec3 a, Vec3 b, double t) noexcept {
    return {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t), std::lerp(a.z, b.z, t)};
}

inline Vec3 rest_point(const RegionalClose::VisibleFace &face, Vec2 source) noexcept {
    const double u = (source.x - face.source.x) / face.source.w;
    const double v = (source.y - face.source.y) / face.source.h;
    return {face.origin.x + u * face.u.x + v * face.v.x,
        face.origin.y + u * face.u.y + v * face.v.y,
        face.origin.z + u * face.u.z + v * face.v.z};
}
}  // namespace hyprimate::construction::studies::regional_close_detail
