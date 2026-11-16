#pragma once

#include "PluginRuntime.hpp"

// The configured style is selected once here; compositor orchestration invokes the same operations
// for every style.
#define LIVE_STYLE_OPERATIONS(name)                                                        \
    struct name {                                                                          \
        static SStyleState initial(                                                        \
            hyprimate::MaterialParams, hyprimate::ClothMovementParams, std::uint64_t);     \
        static double strength(const CPluginRuntime &);                                    \
        static bool usable(const CPluginRuntime &);                                        \
        static bool open(CPluginRuntime &, CPluginRuntime::STracked &, const PHLWINDOW &); \
        static void movement(CPluginRuntime &,                                             \
            CAnimationTransformer &,                                                       \
            const PHLWINDOW &,                                                             \
            const hyprimate::TrackDecision &,                                              \
            double,                                                                        \
            bool,                                                                          \
            bool);                                                                         \
        static void snapshot(const CAnimationTransformer *, hyprimate::WindowSnapshot &);  \
        static void cancelOpening(CAnimationTransformer &);                                \
        static void cancelMovement(CAnimationTransformer &);                               \
        static bool available(const CAnimationTransformer &) noexcept;                     \
        static bool deforming(const CAnimationTransformer &) noexcept;                     \
        static bool builtFor(const CAnimationTransformer &,                                \
            hyprimate::config::Style,                                                      \
            hyprimate::MaterialParams,                                                     \
            hyprimate::ClothMovementParams) noexcept;                                      \
        static void step(                                                                  \
            CPluginRuntime &, CAnimationTransformer &, double, hyprimate::Vec2, bool);     \
    };
LIVE_STYLE_OPERATIONS(SLiquidLiveStyle)
LIVE_STYLE_OPERATIONS(SPaperLiveStyle)
LIVE_STYLE_OPERATIONS(SClothLiveStyle)
LIVE_STYLE_OPERATIONS(SSwarmLiveStyle)
LIVE_STYLE_OPERATIONS(SBubbleLiveStyle)

inline SLiquidLiveStyle liveStyleFor(const SLiquidStyle &) {
    return {};
}
inline SPaperLiveStyle liveStyleFor(const SPaperStyle &) {
    return {};
}
inline SClothLiveStyle liveStyleFor(const SClothStyle &) {
    return {};
}
inline SSwarmLiveStyle liveStyleFor(const SSwarmStyle &) {
    return {};
}
inline SBubbleLiveStyle liveStyleFor(const SBubbleStyle &) {
    return {};
}
#undef LIVE_STYLE_OPERATIONS

template <class F>
decltype(auto) withLiveStyle(hyprimate::config::Style style, F &&f) {
    using enum hyprimate::config::Style;

    switch (style) {
        case Paper:
            return f(SPaperLiveStyle{});
        case Cloth:
            return f(SClothLiveStyle{});
        case Swarm:
            return f(SSwarmLiveStyle{});
        case Bubble:
            return f(SBubbleLiveStyle{});
        case Liquid:
            return f(SLiquidLiveStyle{});
    }

    return f(SLiquidLiveStyle{});
}
