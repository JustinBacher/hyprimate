#pragma once

#include "animations/swarm/SwarmLiveState.hpp"
#include "animations/swarm/SwarmCloseStyleData.hpp"
#include "config_generated.hpp"

struct SSwarmResources;
struct SSwarmModule {
    using State = SSwarmStyle;
    using CloseData = SSwarmCloseData;
    using Resources = SSwarmResources;
    static constexpr auto KIND = hyprimate::config::Style::Swarm;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
};
