#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "config_settings_generated.hpp"
#include "window/WindowStyle.hpp"
#include "window/burn/BurnDynamics.hpp"
#include "window/burn/BurnGround.hpp"
#include "window/burn/BurnGroundDraw.hpp"

struct SSharedStyleResources;
class CHyprAnimationCanvas;
class CBurnRenderer;
struct SBurnStyle {
    std::unique_ptr<hyprimate::BurnSimulation> simulation;
    std::uint64_t seed = 0;
};
struct SBurnPrepared {
    hyprimate::BurnParams open, close;
};
struct SBurnResources : hyprimate::animation::ResourcesBase<SBurnStyle,
                            hyprimate::config::SBurnSettings,
                            SBurnPrepared> {
    SBurnResources();
    explicit SBurnResources(SSharedStyleResources &);
    ~SBurnResources();
    AnimationPool animations;
    CBurnGround ground;
    CBurnGroundDraw ground_draw;
    std::unique_ptr<CBurnRenderer> renderer, scratch_renderer;
};
struct SBurnModule {
    using State = SBurnStyle;
    using Prepared = SBurnPrepared;
    using Resources = SBurnResources;
    using Settings = hyprimate::config::SBurnSettings;
    static const Settings &settings(const auto &config) {
        return config.burn;
    }
    static constexpr auto KIND = hyprimate::config::Style::Burn;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
    static constexpr bool DRAWS_UNDER_BLUR = false;
    static constexpr bool SWEEPS_LARGE_MOVES = true;
    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &resources, std::uint64_t seed);
    static bool open(State &state,
        const hyprimate::animation::Place &place,
        Resources &resources,
        hyprimate::animation::Effects &effects);
    static hyprimate::animation::Frame advance(State &state,
        const hyprimate::animation::Step &step,
        Resources &resources,
        hyprimate::animation::Effects &effects);
    static hyprimate::animation::CloseStart close(
        State &state, const hyprimate::animation::Place &place, Resources &resources);
    static void end(State &state, Resources &resources, hyprimate::animation::End why) noexcept;
    static bool moves(
        const hyprimate::animation::Motion &motion, const Resources &resources) noexcept;
    static bool warm(Resources &resources);
    static bool draw(State &state,
        const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw,
        Resources &resources);
    struct Screen {
        static void tick(Resources &resources, const hyprimate::animation::ScreenFacts &facts);
        static bool needs_frame(
            const Resources &resources, hyprimate::animation::MonitorId monitor);
        static void under_window(Resources &resources,
            const hyprimate::animation::ScreenDraw<CHyprAnimationCanvas> &draw);
        static void before_windows(Resources &resources,
            const hyprimate::animation::ScreenDraw<CHyprAnimationCanvas> &draw);
        static void after_windows(Resources &resources,
            const hyprimate::animation::ScreenDraw<CHyprAnimationCanvas> &draw);
        static void clear(
            Resources &resources, std::optional<hyprimate::animation::MonitorId> monitor);
    };
};
