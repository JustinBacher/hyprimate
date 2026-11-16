#pragma once
#include <cstdint>

#include "Swarm.hpp"
#include "SwarmSprite.hpp"

struct SSwarmCloseData {
    hyprimate::SwarmParams params{};
    std::uint64_t seed = 0;
    hyprimate::SwarmPlacement placement{};
    hyprimate::EdgeSet sides{};
    hyprimate::SwarmCreature creature = hyprimate::SwarmCreature::Spider;
};
