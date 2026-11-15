#pragma once

#include "animations/swarm/Swarm.hpp"
#include "animations/swarm/SwarmSprite.hpp"
#include <hyprland/src/helpers/DeformableMesh.hpp>
#include <vector>

// Render meshes, refilled each frame from the simulation. The first four draw the layer, in this order.
struct SSwarmMeshes {
    std::vector<SMeshRenderVertex> mask{};    // creature silhouettes and merged tiles, each over its own tile
    std::vector<SMeshRenderVertex> cut{};     // quarter discs that round tile corners off
    std::vector<SMeshRenderVertex> fill{};    // the notches between merged tiles
    std::vector<SMeshRenderVertex> content{}; // the window, once under each half of the layer
    std::vector<SMeshRenderVertex> pieces{};  // what stands in the window, then each walking tile where its agent is
};

struct SSwarmStyle {
    liquid_glass::Swarm         swarm{};
    liquid_glass::SwarmCreature creature =
        liquid_glass::SwarmCreature::Spider; // what the running animation is drawn as
    SSwarmMeshes meshes{};
};
