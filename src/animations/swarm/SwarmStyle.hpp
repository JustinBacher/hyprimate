#pragma once

#include "animations/swarm/SwarmLiveState.hpp"
#include "animations/swarm/SwarmCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "animations/shared/SheetResources.hpp"
#include "StyleSettingsCache.hpp"

struct SSwarmPrepared {
    hyprimate::SwarmParams open;
    hyprimate::SwarmParams close;
    hyprimate::SwarmParams movement;
    hyprimate::EdgeSet open_sides;
    hyprimate::EdgeSet close_sides;
};

struct SSwarmResources : SStyleSettingsCache<hyprimate::config::SSwarmSettings, SSwarmPrepared> {
    explicit SSwarmResources([[maybe_unused]] SSharedStyleResources &shared) {}
    SP<CSwarmSprites> sprites = makeShared<CSwarmSprites>();
};
struct SSwarmModule {
    using State = SSwarmStyle;
    using CloseData = SSwarmCloseData;
    using Prepared = SSwarmPrepared;
    using Resources = SSwarmResources;
    using Settings = hyprimate::config::SSwarmSettings;
    static const Settings &settings(const auto &config) { return config.swarm; }
    static constexpr auto KIND = hyprimate::config::Style::Swarm;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
    static constexpr bool HAS_SCREEN_CONTENT = false;
};
