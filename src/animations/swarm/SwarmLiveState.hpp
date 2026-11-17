#pragma once

#include <string>
#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>

#include "animations/swarm/Swarm.hpp"
#include "animations/swarm/SwarmSprite.hpp"

// Alpha masks, the preserved animated creatures, window contents and final pieces.
struct SSwarmMeshes {
    std::vector<SMeshRenderVertex>
        mask{};  // creature silhouettes and merged tiles, each over its own tile
    std::vector<SMeshRenderVertex>
        creatures{};  // original-size standing sprites over the smooth mass
    std::vector<SMeshRenderVertex> walkingCut{};  // rounds detached, shrinking tiles
    std::vector<SMeshRenderVertex> content{};  // the window, once under each half of the layer
    std::vector<SMeshRenderVertex>
        pieces{};  // what stands in the window, then each walking tile where its agent is
    double standingAt = 0.0;  // start of the standing half, in layer pixels
    double smoothingRadius = 0.0;  // combined-outline smoothing in device pixels
};

struct SSwarmStyle {
    hyprimate::Swarm swarm{};
    std::string creature;  // what the running animation is drawn as
    SSwarmMeshes meshes{};
};
