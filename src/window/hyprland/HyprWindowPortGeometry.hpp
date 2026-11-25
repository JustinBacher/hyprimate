#pragma once

#include "shared/Core.hpp"
#include "HyprWindowPort.hpp"

namespace hyprimate::window_port_detail {
inline CBox host_box(const hyprimate::Box &box) {
    return {box.x, box.y, box.w, box.h};
}
inline hyprimate::Vec2 vec_of(const Vector2D &value) {
    return {.x = value.x, .y = value.y};
}
}  // namespace hyprimate::window_port_detail
