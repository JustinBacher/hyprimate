#pragma once

#include "StyleSettingsCache.hpp"
#include "animations/paper/PaperLiveState.hpp"
#include "animations/paper/PaperCloseStyleData.hpp"
#include "config_generated.hpp"
#include "animations/shared/SheetResources.hpp"

struct SPaperPrepared {
    hyprimate::PaperParams open;
    hyprimate::PaperParams close;
};
struct SPaperResources : SStyleSettingsCache<hyprimate::config::SPaperSettings, SPaperPrepared> {
    explicit SPaperResources(SSharedStyleResources &shared) : shade(shared.sheet_shade) {}
    CSheetShade &shade;
};
struct SPaperModule {
    using State = SPaperStyle;
    using CloseData = SPaperCloseData;
    using Prepared = SPaperPrepared;
    using Resources = SPaperResources;
    using Settings = hyprimate::config::SPaperSettings;
    static const Settings &settings(const auto &config) { return config.paper; }
    static constexpr auto KIND = hyprimate::config::Style::Paper;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
};
