#pragma once

#include "PluginRuntime.hpp"

// The configured style is selected once here; compositor orchestration invokes the same operations for every style.
#define LIVE_STYLE_OPERATIONS(name) \
struct name { \
    static SStyleState initial(liquid_glass::MaterialParams, liquid_glass::ClothMovementParams, std::uint64_t); \
    static double strength(const CPluginRuntime &); \
    static bool usable(const CPluginRuntime &); \
    static bool open(CPluginRuntime &, CPluginRuntime::STracked &, const PHLWINDOW &); \
    static void movement(CPluginRuntime &, CAnimationTransformer &, const PHLWINDOW &, \
                         const liquid_glass::TrackDecision &, double, bool, bool); \
    static void snapshot(const CAnimationTransformer *, liquid_glass::WindowSnapshot &); \
    static void cancelOpening(CAnimationTransformer &); \
    static void cancelMovement(CAnimationTransformer &); \
    static bool available(const CAnimationTransformer &) noexcept; \
    static bool deforming(const CAnimationTransformer &) noexcept; \
    static bool builtFor(const CAnimationTransformer &, liquid_glass::config::Style, \
                         liquid_glass::MaterialParams, liquid_glass::ClothMovementParams) noexcept; \
    static void step(CPluginRuntime &, CAnimationTransformer &, double, liquid_glass::Vec2, bool); \
};
LIVE_STYLE_OPERATIONS(SLiquidLiveStyle)
LIVE_STYLE_OPERATIONS(SPaperLiveStyle)
LIVE_STYLE_OPERATIONS(SClothLiveStyle)
LIVE_STYLE_OPERATIONS(SSwarmLiveStyle)

inline SLiquidLiveStyle liveStyleFor(const SLiquidStyle &) { return {}; }
inline SPaperLiveStyle liveStyleFor(const SPaperStyle &) { return {}; }
inline SClothLiveStyle liveStyleFor(const SClothStyle &) { return {}; }
inline SSwarmLiveStyle liveStyleFor(const SSwarmStyle &) { return {}; }
#undef LIVE_STYLE_OPERATIONS

template <class F>
decltype(auto) withLiveStyle(liquid_glass::config::Style style, F &&f) {
    switch (style) {
    case liquid_glass::config::Style::Paper: return f(SPaperLiveStyle{});
    case liquid_glass::config::Style::Cloth: return f(SClothLiveStyle{});
    case liquid_glass::config::Style::Swarm: return f(SSwarmLiveStyle{});
    case liquid_glass::config::Style::Liquid: return f(SLiquidLiveStyle{});
    }
    return f(SLiquidLiveStyle{});
}
