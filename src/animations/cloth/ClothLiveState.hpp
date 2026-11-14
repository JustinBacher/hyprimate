#pragma once

#include "animations/shared/LiveSheetState.hpp"
#include "animations/cloth/ClothFlutter.hpp"

struct SClothStyle {
    liquid_glass::ClothFlutter cloth{};
    liquid_glass::ClothMovementParams movement{}; // settings this state was built for
    SSheetBuffers sheet{};
};
