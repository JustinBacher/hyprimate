#pragma once

#include "animations/swarm/SwarmLiveState.hpp"
#include "animations/swarm/SwarmCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "animations/shared/SheetResources.hpp"

struct SSwarmInputs {};
struct SSwarmResources {
    explicit SSwarmResources([[maybe_unused]] SSharedStyleResources &shared) {}
    SP<CSwarmSprites> sprites = makeShared<CSwarmSprites>();
};
struct SSwarmModule {
    using State = SSwarmStyle;
    using CloseData = SSwarmCloseData;
    using Inputs = SSwarmInputs;
    using Resources = SSwarmResources;
    static constexpr auto KIND = hyprimate::config::Style::Swarm;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
};
