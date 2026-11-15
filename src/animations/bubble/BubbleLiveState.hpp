#pragma once

#include "Bubble.hpp"
#include "BubbleRenderer.hpp"
#include "BubbleResidue.hpp"

struct SBubbleStyle {
    liquid_glass::Bubble bubble;
    SP<CBubbleRenderer>  renderer;
    SP<SBubbleWallpaper> wallpaper;
    SP<CBubbleResidue>   residue;
    bool                 residueEmitted = false;
};
