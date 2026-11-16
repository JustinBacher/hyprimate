#pragma once
#include <cstdint>
#include <string>

#include "Swarm.hpp"
#include "SwarmSprite.hpp"

struct SSwarmCloseData {
    hyprimate::SwarmParams params{};
    std::uint64_t seed = 0;
    hyprimate::SwarmPlacement placement{};
    hyprimate::EdgeSet sides{};
    std::string creature;
};
