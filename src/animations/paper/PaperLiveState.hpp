#pragma once

#include "animations/paper/PaperCrumple.hpp"
#include "animations/shared/LiveSheetState.hpp"

struct SPaperStyle {
    hyprimate::PaperCrumple crumple{};
    SSheetBuffers sheet{};
};
