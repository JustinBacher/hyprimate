#pragma once

#include "animations/bubble/BubbleLiveState.hpp"
#include "animations/bubble/BubbleCloseStyleData.hpp"
#include "config_generated.hpp"

struct SBubbleResources;
struct SBubbleModule {
    using State = SBubbleStyle;
    using CloseData = SBubbleCloseData;
    using Resources = SBubbleResources;
    static constexpr auto KIND = hyprimate::config::Style::Bubble;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
};
