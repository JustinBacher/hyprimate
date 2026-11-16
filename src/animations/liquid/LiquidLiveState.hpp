#pragma once

#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/shared/EdgeTransition.hpp"

struct SLiquidStyle {
    hyprimate::LiquidMesh mesh{};
    hyprimate::EdgeTransition transition{};
    // While an opening drives the real window box, remember the layout goal and our last warp.
    bool boxDriven = false;
    CBox goalBody{};
    Vector2D warpPosition{};
    Vector2D warpSize{};
    bool warped = false;
    double displacementGain = 1.0;
    // The grid is invalidated on pose changes; render buffers are reused each frame.
    mutable hyprimate::LiquidPoseGrid grid{};
    std::vector<SMeshRenderVertex> vertices{};
    std::vector<SMeshRenderVertex> meniscusVertices{};
};
