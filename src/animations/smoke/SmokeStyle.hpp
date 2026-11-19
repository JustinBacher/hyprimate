#pragma once

#include "animations/smoke/SmokeLiveState.hpp"
#include "animations/smoke/SmokeCloseStyleData.hpp"
#include "config_generated.hpp"

struct SSmokeResources;
struct SSmokeModule {
    using State = SSmokeStyle;
    using CloseData = SSmokeCloseData;
    using Resources = SSmokeResources;
    static constexpr auto KIND = hyprimate::config::Style::Smoke;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
};
