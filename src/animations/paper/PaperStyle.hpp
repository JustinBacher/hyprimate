#pragma once

#include "animations/paper/PaperLiveState.hpp"
#include "animations/paper/PaperCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/shared/SheetResources.hpp"

struct SPaperInputs {};
struct SPaperResources {
    explicit SPaperResources(SSharedStyleResources &shared) : shade(shared.sheet_shade) {}
    CSheetShade &shade;
};
struct SPaperModule {
    using State = SPaperStyle;
    using CloseData = SPaperCloseData;
    using Inputs = SPaperInputs;
    using Resources = SPaperResources;
    static constexpr auto KIND = hyprimate::config::Style::Paper;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
};
