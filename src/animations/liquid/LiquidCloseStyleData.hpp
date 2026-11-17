#pragma once
#include <optional>

#include "LiquidPose.hpp"

struct SLiquidCloseData {
    hyprimate::Vec2 compensation{.x = 1.0, .y = 1.0};
    double strength = 0.0;
    std::optional<hyprimate::EdgeFrame> frame;
    bool splash = true;
    hyprimate::TransitionParams params{};
    bool has_liquid = false;
    bool vertices_stale = false;
    hyprimate::Box liquid_device{};
};
