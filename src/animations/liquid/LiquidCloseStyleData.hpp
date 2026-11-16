#pragma once
#include <optional>

#include "LiquidPose.hpp"

struct SLiquidCloseData {
    hyprimate::Vec2 compensation{1.0, 1.0};
    double strength = 0.0;
    std::optional<hyprimate::EdgeFrame> frame;
    bool splash = true;
    hyprimate::TransitionParams params{};
    bool hasLiquid = false;
    bool verticesStale = false;
    hyprimate::Box liquidDevice{};
};
