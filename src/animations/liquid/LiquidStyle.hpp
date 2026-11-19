#pragma once

#include "animations/liquid/LiquidLiveState.hpp"
#include "animations/liquid/LiquidCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/liquid/SolidFill.hpp"
#include "animations/shared/SheetResources.hpp"
#include "StyleSettingsCache.hpp"

struct SLiquidPrepared {
    hyprimate::LiquidParams movement;
    hyprimate::TransitionParams open;
    hyprimate::TransitionParams close;
    double movement_strength{};
    double open_strength{};
    double close_strength{};
    double emerge_drag{};
    bool close_splash{};
    CHyprColor open_color;
    CHyprColor close_color;
};

struct SLiquidResources : SStyleSettingsCache<hyprimate::config::SLiquidSettings, SLiquidPrepared> {
    explicit SLiquidResources([[maybe_unused]] SSharedStyleResources &shared) {}
    CSolidFill open_solid;
    CSolidFill close_solid;
};
struct SLiquidModule {
    using State = SLiquidStyle;
    using CloseData = SLiquidCloseData;
    using Prepared = SLiquidPrepared;
    using Resources = SLiquidResources;
    using Settings = hyprimate::config::SLiquidSettings;
    static const Settings &settings(const auto &config) { return config.liquid; }
    static constexpr auto KIND = hyprimate::config::Style::Liquid;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
    static constexpr bool HAS_SCREEN_CONTENT = false;
};

hyprimate::LiquidPose live_pose(const SLiquidStyle &state);

