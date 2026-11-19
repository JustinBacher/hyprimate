#pragma once

#include "animations/liquid/LiquidLiveState.hpp"
#include "animations/liquid/LiquidCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/liquid/LiquidPose.hpp"

struct SLiquidResources;
struct SLiquidModule {
    using State = SLiquidStyle;
    using CloseData = SLiquidCloseData;
    using Resources = SLiquidResources;
    static constexpr auto KIND = hyprimate::config::Style::Liquid;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
};

hyprimate::LiquidPose live_pose(const SLiquidStyle &state);
