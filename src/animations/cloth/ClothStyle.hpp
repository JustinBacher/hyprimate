#pragma once

#include "animations/cloth/ClothLiveState.hpp"
#include "animations/cloth/ClothCloseStyleData.hpp"
#include "config_generated.hpp"

struct SClothResources;
struct SClothModule {
    using State = SClothStyle;
    using CloseData = SClothCloseData;
    using Resources = SClothResources;
    static constexpr auto KIND = hyprimate::config::Style::Cloth;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
};
