#pragma once

#include <optional>

#include "window/liquid/LiquidPose.hpp"

struct SLiquidStyle {
    hyprimate::LiquidMesh mesh;
    hyprimate::EdgeTransition transition{};
    hyprimate::LiquidPoseGrid grid{};
    std::optional<hyprimate::Edge> origin;
    hyprimate::Vec2 compensation{.x = 1.0, .y = 1.0};
    double displacement_gain = 1.0;
    double close_strength = 0.0;
    bool box_driven = false;
    bool dying = false;
    bool owned = false;
};
