#pragma once
#include "Swarm.hpp"
#include "SwarmSprite.hpp"
#include <cstdint>

struct SSwarmCloseData {
    liquid_glass::SwarmParams    params{};
    std::uint64_t                seed = 0;
    liquid_glass::SwarmPlacement placement{};
    liquid_glass::EdgeSet        sides{};
    liquid_glass::SwarmCreature  creature = liquid_glass::SwarmCreature::Spider;
};
