#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>

#include "ConstructionArt.hpp"
#include "ConstructionCloseSite.hpp"

namespace hyprimate::construction::close_detail {
using Regional = studies::RegionalClose;

inline double right(Box b) noexcept { return b.x + b.w; }
inline double bottom(Box b) noexcept { return b.y + b.h; }
inline bool valid_box(Box b) noexcept {
    return is_usable(b) && right(b) > b.x && bottom(b) > b.y &&
        std::isfinite(right(b)) && std::isfinite(bottom(b));
}
inline Box united(Box a, Box b) noexcept {
    if (!is_usable(a)) return b;
    const double x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(right(a), right(b)) - x, std::max(bottom(a), bottom(b)) - y};
}
inline Vec3 unpack(std::array<float, 3> value) noexcept { return {value[0], value[1], value[2]}; }

bool import_close_faces(std::optional<std::span<const ConstructionQuad>> currently_visible, Box cap,
    std::array<Regional::VisibleFace, Regional::MAX_VISIBLE_FACES> &faces,
    std::size_t &count, Box &required_snapshot) noexcept;
void emit_close_effects(const Regional::Frame &frame, ConstructionBatch &effects, bool departing,
    std::span<const ConstructionQuad> saved, double elapsed, double seconds, double distance, bool blank) noexcept;
}  // namespace hyprimate::construction::close_detail
