#pragma once

#include <utility>

#include "PluginRuntime.hpp"

// The configured style is selected once here; compositor orchestration invokes the same operations
// for every style.
#define LIVE_STYLE_OPERATIONS(kind, name, module)                                          \
    struct name : module {                                                                 \
        using Module = module;                                                             \
        using Tracked = CPluginRuntime::STracked;                                         \
        static Prepared prepare_settings(const Settings &);                                \
        static State initial(const Resources &, std::uint64_t);                            \
        static void prepare(const Settings &, Resources &, bool selected);                 \
        static bool movement_requested(const hyprimate::TrackDecision &) noexcept;         \
        static double strength(const Resources &);                                         \
        static bool usable(const Resources &);                                             \
        static bool open(CPluginRuntime &, CPluginRuntime::STracked &, const PHLWINDOW &, Resources &); \
        static void movement(CPluginRuntime &,                                             \
            CAnimationTransformer &,                                                       \
            const PHLWINDOW &,                                                             \
            const hyprimate::TrackDecision &,                                              \
            double,                                                                        \
            bool,                                                                          \
            bool,                                                                          \
            Resources &);                                                                  \
        static void snapshot(const CAnimationTransformer *, hyprimate::WindowSnapshot &,  \
            const Resources &);                                                            \
        static void cancelOpening(CAnimationTransformer &);                                \
        static void cancelMovement(CAnimationTransformer &);                               \
        static bool available(const State &) noexcept;                                     \
        static bool deforming(const State &) noexcept;                                     \
        static bool builtFor(const State &, const Resources &) noexcept;                   \
        static void step(CPluginRuntime &, CAnimationTransformer &, double,                \
            hyprimate::Vec2, bool, Resources &);                                            \
        static void screen_tick(CPluginRuntime &, Resources &,                             \
            std::chrono::steady_clock::time_point, const PHLMONITOR &);                     \
        static bool screen_needs_frame(const CPluginRuntime &, const Resources &,           \
            const PHLMONITOR &);                                                           \
        static void screen_before_window(CPluginRuntime &, Resources &,                    \
            Render::CRenderContext &, const PHLMONITOR &, const PHLWINDOW &);               \
        static void screen_after_windows(CPluginRuntime &, Resources &,                    \
            Render::CRenderContext &, const PHLMONITOR &);                                  \
        static void screen_clear(CPluginRuntime &, Resources &);                            \
        static std::optional<CBox> screen_window_capture(const CPluginRuntime &,            \
            const Resources &, const PHLWINDOW &, const CAnimationTransformer *);           \
    };
HYPRIMATE_STYLE_LIST(LIVE_STYLE_OPERATIONS, HYPRIMATE_STYLE_EMPTY)

#define HYPRIMATE_LIVE_FOR(kind, name, module)                                              \
    inline name live_style_for([[maybe_unused]] const module::State &state) { return {}; }
HYPRIMATE_STYLE_LIST(HYPRIMATE_LIVE_FOR, HYPRIMATE_STYLE_EMPTY)
#undef HYPRIMATE_LIVE_FOR
#undef LIVE_STYLE_OPERATIONS

template <class F>
decltype(auto) with_live_style(hyprimate::config::Style style, F &&operation) {
    switch (style) {
#define HYPRIMATE_CASE(kind, name, module)                                                  \
        case hyprimate::config::Style::kind: return std::forward<F>(operation)(name{});
        HYPRIMATE_STYLE_LIST(HYPRIMATE_CASE, HYPRIMATE_STYLE_EMPTY)
#undef HYPRIMATE_CASE
    }
    return std::forward<F>(operation)(SDefaultLiveStyle{});
}
