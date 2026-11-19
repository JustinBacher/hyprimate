#pragma once

#include "animations/paper/PaperLiveState.hpp"
#include "animations/paper/PaperCloseStyleData.hpp"
#include "config_generated.hpp"

struct SPaperResources;
struct SPaperModule {
    using State = SPaperStyle;
    using CloseData = SPaperCloseData;
    using Resources = SPaperResources;
    static constexpr auto KIND = hyprimate::config::Style::Paper;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
};
