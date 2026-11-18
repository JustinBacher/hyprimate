#pragma once

#include <utility>

#include "PluginRuntime.hpp"

// The configured style is selected once here; compositor orchestration invokes the same operations
// for every style.
#define LIVE_STYLE_OPERATIONS(name, state)                                                 \
    struct name {                                                                          \
        static SStyleState initial(                                                        \
            hyprimate::LiquidParams, hyprimate::ClothMovementParams, std::uint64_t);     \
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
        static bool available(const state &) noexcept;                                     \
        static bool deforming(const state &) noexcept;                                     \
        static bool builtFor(const state &,                                                \
            hyprimate::config::Style,                                                      \
            hyprimate::LiquidParams,                                                     \
            hyprimate::ClothMovementParams) noexcept;                                      \
        static void step(                                                                  \
            CPluginRuntime &, CAnimationTransformer &, double, hyprimate::Vec2, bool);     \
    };
LIVE_STYLE_OPERATIONS(SLiquidLiveStyle, SLiquidStyle)
LIVE_STYLE_OPERATIONS(SPaperLiveStyle, SPaperStyle)
LIVE_STYLE_OPERATIONS(SClothLiveStyle, SClothStyle)
LIVE_STYLE_OPERATIONS(SSwarmLiveStyle, SSwarmStyle)
LIVE_STYLE_OPERATIONS(SBubbleLiveStyle, SBubbleStyle)
LIVE_STYLE_OPERATIONS(SSmokeLiveStyle, SSmokeStyle)

inline SLiquidLiveStyle live_style_for([[maybe_unused]] const SLiquidStyle &state) {
    return {};
}
inline SPaperLiveStyle live_style_for([[maybe_unused]] const SPaperStyle &state) {
    return {};
}
inline SClothLiveStyle live_style_for([[maybe_unused]] const SClothStyle &state) {
    return {};
}
inline SSwarmLiveStyle live_style_for([[maybe_unused]] const SSwarmStyle &state) {
    return {};
}
inline SBubbleLiveStyle live_style_for([[maybe_unused]] const SBubbleStyle &state) {
    return {};
}
inline SSmokeLiveStyle live_style_for([[maybe_unused]] const SSmokeStyle &state) {
    return {};
}
#undef LIVE_STYLE_OPERATIONS

template <class F>
decltype(auto) with_live_style(hyprimate::config::Style style, F &&operation) {
    using enum hyprimate::config::Style;

    switch (style) {
        case Paper:
            return std::forward<F>(operation)(SPaperLiveStyle{});
        case Cloth:
            return std::forward<F>(operation)(SClothLiveStyle{});
        case Swarm:
            return std::forward<F>(operation)(SSwarmLiveStyle{});
        case Bubble:
            return std::forward<F>(operation)(SBubbleLiveStyle{});
        case Smoke:
            return std::forward<F>(operation)(SSmokeLiveStyle{});
        case Liquid:
            return std::forward<F>(operation)(SLiquidLiveStyle{});
    }

    return std::forward<F>(operation)(SLiquidLiveStyle{});
}
