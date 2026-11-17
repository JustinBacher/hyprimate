#pragma once

#include "animations/cloth/ClothFlutter.hpp"
#include "animations/shared/LiveSheetState.hpp"

struct SClothStyle {
    hyprimate::ClothFlutter cloth;
    hyprimate::ClothMovementParams movement{};  // settings this state was built for
    SSheetBuffers sheet{};
};
