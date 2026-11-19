#pragma once

#include "StyleSettingsCache.hpp"
#include "animations/cloth/ClothLiveState.hpp"
#include "animations/cloth/ClothCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/shared/SheetResources.hpp"

struct SClothPrepared {
    hyprimate::ClothMovementParams movement;
    hyprimate::ClothParams open;
    hyprimate::ClothParams close;
};

struct SClothResources : SStyleSettingsCache<hyprimate::config::SClothSettings, SClothPrepared> {
    explicit SClothResources(SSharedStyleResources &shared) : shade(shared.sheet_shade) {}
    hyprimate::ClothPhysics physics;
    CSheetShade &shade;
};
struct SClothModule {
    using State = SClothStyle;
    using CloseData = SClothCloseData;
    using Prepared = SClothPrepared;
    using Resources = SClothResources;
    using Settings = hyprimate::config::SClothSettings;
    static const Settings &settings(const auto &config) { return config.cloth; }
    static constexpr auto KIND = hyprimate::config::Style::Cloth;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
};
