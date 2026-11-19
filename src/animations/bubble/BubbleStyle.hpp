#pragma once

#include "animations/bubble/BubbleLiveState.hpp"
#include "animations/bubble/BubbleCloseStyleData.hpp"
#include "animations/bubble/BubbleRenderer.hpp"
#include "config_generated.hpp"
#include "animations/shared/SheetResources.hpp"
#include "StyleSettingsCache.hpp"

struct SBubblePrepared {
    hyprimate::BubbleMotionParams movement;
    hyprimate::BubbleFormationParams formation;
    double open_duration = 0;
    double close_duration = 0;
};

struct SBubbleResources : SStyleSettingsCache<hyprimate::config::SBubbleSettings, SBubblePrepared> {
    explicit SBubbleResources([[maybe_unused]] SSharedStyleResources &shared) {}
    SP<CBubbleRenderer> renderer = makeShared<CBubbleRenderer>();
    CBubbleVestige vestige;
};
struct SBubbleModule {
    using State = SBubbleStyle;
    using CloseData = SBubbleCloseData;
    using Prepared = SBubblePrepared;
    using Resources = SBubbleResources;
    using Settings = hyprimate::config::SBubbleSettings;
    static const Settings &settings(const auto &config) { return config.bubble; }
    static constexpr auto KIND = hyprimate::config::Style::Bubble;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
};
// Hands each surface's glass impacts to the Vestige once, as soon as its first hole has opened.
// Pending beads land when the transition's shared clock reaches them, so they neither lead their
// droplets nor outlive an aborted opening. Callers exclude capture and read-only passes. centre is
// the frame centre in monitor-local logical coordinates.
inline void deposit_bubble_impacts(SBubbleStyle &state, CBubbleVestige &vestige,
    const hyprimate::BubbleFrame &frame,
    const PHLMONITOR &monitor,
    Vector2D centre) {
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
