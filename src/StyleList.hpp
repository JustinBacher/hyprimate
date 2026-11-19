#pragma once

#define HYPRIMATE_STYLE_LIST(X, SEP) \
    X(Liquid, SLiquidLiveStyle, SLiquidModule) SEP() \
    X(Paper, SPaperLiveStyle, SPaperModule) SEP() \
    X(Cloth, SClothLiveStyle, SClothModule) SEP() \
    X(Swarm, SSwarmLiveStyle, SSwarmModule) SEP() \
    X(Bubble, SBubbleLiveStyle, SBubbleModule) SEP() \
    X(Smoke, SSmokeLiveStyle, SSmokeModule)

#define HYPRIMATE_STYLE_COMMA() ,
#define HYPRIMATE_STYLE_EMPTY()
