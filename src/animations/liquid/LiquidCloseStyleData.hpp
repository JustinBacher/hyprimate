#pragma once
#include "LiquidPose.hpp"
#include <optional>

struct SLiquidCloseData {
    liquid_glass::Vec2 compensation{1.0, 1.0};
    double strength = 0.0;
    std::optional<liquid_glass::EdgeFrame> frame;
    bool splash = true;
    liquid_glass::TransitionParams params{};
    bool hasLiquid = false;
    bool verticesStale = false;
    liquid_glass::Box liquidDevice{};
};
