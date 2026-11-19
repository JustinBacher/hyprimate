#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <hyprland/src/helpers/DeformableMesh.hpp>
#include "animations/swarm/Swarm.hpp"
#include "animations/swarm/SwarmSprite.hpp"
#include "animations/swarm/SwarmSurfaceBody.hpp"

// Walking masks, analytic merging bodies, preserved animated creatures and window contents.
struct SSwarmMeshes {
    std::vector<SMeshRenderVertex>
        mask;  // creature silhouettes and merged tiles, each over its own tile
    std::vector<SMeshRenderVertex>
        creatures;  // original-size standing sprites over the smooth mass
    std::vector<SMeshRenderVertex> walking_cut;  // rounds detached, shrinking tiles
    std::vector<SMeshRenderVertex> content;  // the window, once under each half of the layer
    std::vector<SMeshRenderVertex>
        pieces;  // what stands in the window, then each walking tile where its agent is
    std::vector<hyprimate::SwarmSurfaceBody> bodies;  // continuous standing contours
    double standing_at = 0.0;  // start of the standing half, in layer pixels
    double smoothing_radius = 0.0;  // distance-field smooth-union length in device pixels
    double corner_radius = 0.0;  // outer rounding relaxes independently at native handoff
};

struct SSwarmStyle {
    hyprimate::Swarm swarm{};
    std::string creature;  // what the running animation is drawn as
    SSwarmMeshes meshes{};
    SSwarmMeshes scratch_meshes{};
    std::uint64_t seed = 0;
};
