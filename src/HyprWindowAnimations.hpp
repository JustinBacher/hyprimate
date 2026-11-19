#pragma once

#include "WindowStyleList.hpp"

class CHyprWindowPort;
struct SLiquidModule;
struct SPaperModule;
struct SClothModule;
struct SBubbleModule;
struct SSwarmModule;
struct SSmokeModule;

namespace hyprimate::animation {
template<class Port, class List> class CWindowAnimations;
}

using ProductionStyles = hyprimate::animation::StyleList<SLiquidModule, SPaperModule,
    SClothModule, SBubbleModule, SSwarmModule, SSmokeModule>;
using HyprWindowAnimations = hyprimate::animation::CWindowAnimations<CHyprWindowPort, ProductionStyles>;
