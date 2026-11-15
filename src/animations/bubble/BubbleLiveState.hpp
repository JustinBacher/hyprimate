#pragma once

#include "Bubble.hpp"
#include "BubbleRenderer.hpp"

struct SBubbleStyle {
    liquid_glass::Bubble bubble;
    SP<CBubbleRenderer>  renderer;
    SP<SBubbleWallpaper> wallpaper;
};
