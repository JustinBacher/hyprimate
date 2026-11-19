#pragma once

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "animations/bubble/BubbleLiveState.hpp"
#include "animations/bubble/BubbleRenderer.hpp"
#include "animations/shared/SheetResources.hpp"
#include "WindowStyle.hpp"
#include "HyprAnimationCanvas.hpp"
#include "config_generated.hpp"


struct SBubblePrepared {
    hyprimate::BubbleMotionParams movement;
    hyprimate::BubbleFormationParams formation;
    double open_duration = 0;
    double close_duration = 0;
};

struct SBubbleResources : hyprimate::animation::ResourcesBase<SBubbleStyle,
                              hyprimate::config::SBubbleSettings, SBubblePrepared> {
    explicit SBubbleResources([[maybe_unused]] SSharedStyleResources &shared) {}
    SP<CBubbleRenderer> renderer = makeShared<CBubbleRenderer>();
    SP<CBubbleRenderer> scratch_renderer = makeShared<CBubbleRenderer>();
    CBubbleVestige vestige;
    AnimationPool animations;
};
struct SBubbleModule {
    using State = SBubbleStyle;
    using Prepared = SBubblePrepared;
    using Resources = SBubbleResources;
    using Settings = hyprimate::config::SBubbleSettings;
    static const Settings &settings(const auto &config) { return config.bubble; }
    static constexpr auto KIND = hyprimate::config::Style::Bubble;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
    static constexpr bool DRAWS_UNDER_BLUR = false;
    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &resources, std::uint64_t seed);
    static bool open(State &state, const hyprimate::animation::Place &place, Resources &resources,
        hyprimate::animation::Effects &effects);
    static hyprimate::animation::Frame advance(State &state, const hyprimate::animation::Step &step,
        Resources &resources, hyprimate::animation::Effects &effects);
    static hyprimate::animation::CloseStart close(State &state,
        const hyprimate::animation::Place &place, Resources &resources);
    static void end(State &state, Resources &resources, hyprimate::animation::End why) noexcept;
    static bool moves(const hyprimate::animation::Motion &motion, const Resources &resources) noexcept;
    static void warm(Resources &resources);
    static bool draw(State &state, const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw,
        Resources &resources);
    struct Screen {
        static void tick(Resources &resources, const hyprimate::animation::ScreenFacts &facts);
        static bool needs_frame(const Resources &resources, hyprimate::animation::MonitorId monitor);
        static void under_window(Resources &resources,
            const hyprimate::animation::ScreenDraw<CHyprAnimationCanvas> &draw);
        static void after_windows(Resources &resources,
            const hyprimate::animation::ScreenDraw<CHyprAnimationCanvas> &draw);
        static void clear(Resources &resources,
            std::optional<hyprimate::animation::MonitorId> monitor);
    };
};
// Hands each surface's glass impacts to the Vestige once, as soon as its first hole has opened.
// Pending beads land when the transition's shared clock reaches them, so they neither lead their
// droplets nor outlive an aborted opening. Callers exclude capture and read-only passes. centre is
// the frame centre in monitor-local logical coordinates.
inline void deposit_bubble_impacts(SBubbleStyle &state, CBubbleVestige &vestige,
    const hyprimate::BubbleFrame &frame,
    hyprimate::animation::MonitorId monitor,
    hyprimate::Vec2 centre) {
    if (!monitor) {
        return;
    }
    const auto DEPOSIT = [&](const hyprimate::BubbleSurface &surface, bool &done) {
        if (done) {
            return;
        }
        const auto HOLES =
            std::span(surface.holes).first(std::min(surface.hole_count, surface.holes.size()));
        if (std::ranges::none_of(
                HOLES, [](const hyprimate::BubbleHole &hole) { return hole.radius > 0; })) {
            return;
        }
        std::array<hyprimate::BubbleImpact, hyprimate::K_BUBBLE_MAX_IMPACTS> found{};
        const auto COUNT = hyprimate::bubble_impacts(surface, found);
        // A disabled Vestige declines the beads; they are not retried later.
        vestige.deposit(
            monitor, centre, std::span(found).first(COUNT), state.impacts.clock());
        done = true;
    };
    DEPOSIT(frame.shell, state.shell_impacts_deposited);
    DEPOSIT(frame.window, state.window_impacts_deposited);
}
