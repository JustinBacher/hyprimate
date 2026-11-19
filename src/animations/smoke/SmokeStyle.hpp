#pragma once

#include "animations/smoke/SmokeLiveState.hpp"
#include "animations/smoke/SmokeCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/shared/SheetResources.hpp"
#include "StyleSettingsCache.hpp"

struct SSmokePrepared {
    hyprimate::SmokeParams movement;
    hyprimate::SmokeParams opening;
    hyprimate::SmokeParams closing;
    double open_duration = 0;
    double close_duration = 0;
};
struct SSmokeResources : SStyleSettingsCache<hyprimate::config::SSmokeSettings, SSmokePrepared> {
    explicit SSmokeResources([[maybe_unused]] SSharedStyleResources &shared) {}
    SP<CSmokeRenderer> renderer = makeShared<CSmokeRenderer>();
};
struct SSmokeModule {
    using State = SSmokeStyle;
    using CloseData = SSmokeCloseData;
    using Prepared = SSmokePrepared;
    using Resources = SSmokeResources;
    using Settings = hyprimate::config::SSmokeSettings;
    static const Settings &settings(const auto &config) { return config.smoke; }
    static constexpr auto KIND = hyprimate::config::Style::Smoke;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
    static constexpr bool HAS_SCREEN_CONTENT = false;
};
