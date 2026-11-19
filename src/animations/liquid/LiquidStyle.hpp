#pragma once

#include "animations/liquid/LiquidLiveState.hpp"
#include "animations/liquid/LiquidCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/shared/SheetResources.hpp"

struct SLiquidResources {
    explicit SLiquidResources([[maybe_unused]] SSharedStyleResources &shared) {}
    CSolidFill open_solid;
    CSolidFill close_solid;
};
struct SLiquidModule {
    using State = SLiquidStyle;
    using CloseData = SLiquidCloseData;
    using Inputs = hyprimate::LiquidParams;
    using Resources = SLiquidResources;
    static constexpr auto KIND = hyprimate::config::Style::Liquid;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
};

hyprimate::LiquidPose live_pose(const SLiquidStyle &state);

inline hyprimate::LiquidParams liquid_inputs(const hyprimate::config::SConfig &config) {
    const auto &liquid = config.liquid.movement;
    return {.viscosity = liquid.viscosity, .elasticity = liquid.elasticity,
        .momentum = liquid.momentum, .variation = liquid.variation};
}
