#pragma once

#include "animations/smoke/SmokeLiveState.hpp"
#include "animations/smoke/SmokeCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/shared/SheetResources.hpp"

struct SSmokeInputs {};
struct SSmokeResources {
    explicit SSmokeResources([[maybe_unused]] SSharedStyleResources &shared) {}
    SP<CSmokeRenderer> renderer = makeShared<CSmokeRenderer>();
};
struct SSmokeModule {
    using State = SSmokeStyle;
    using CloseData = SSmokeCloseData;
    using Inputs = SSmokeInputs;
    using Resources = SSmokeResources;
    static constexpr auto KIND = hyprimate::config::Style::Smoke;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
};
