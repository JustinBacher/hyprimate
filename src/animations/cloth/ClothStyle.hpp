#pragma once

#include "animations/cloth/ClothLiveState.hpp"
#include "animations/cloth/ClothCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/shared/SheetResources.hpp"

struct SClothResources {
    explicit SClothResources(SSharedStyleResources &shared) : shade(shared.sheet_shade) {}
    hyprimate::ClothPhysics physics;
    CSheetShade &shade;
};
struct SClothModule {
    using State = SClothStyle;
    using CloseData = SClothCloseData;
    using Inputs = hyprimate::ClothMovementParams;
    using Resources = SClothResources;
    static constexpr auto KIND = hyprimate::config::Style::Cloth;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
};
