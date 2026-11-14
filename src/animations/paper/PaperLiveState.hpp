#pragma once

#include "animations/shared/LiveSheetState.hpp"
#include "animations/paper/PaperCrumple.hpp"

struct SPaperStyle {
    liquid_glass::PaperCrumple crumple{};
    SSheetBuffers sheet{};
};
