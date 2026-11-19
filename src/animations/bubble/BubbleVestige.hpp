#pragma once

#include <chrono>
#include <optional>
#include <span>
#include <vector>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include "WindowAnimationFacts.hpp"
#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleWake.hpp"

class CBubbleRenderer;
namespace Render { class CRenderContext; }

struct SBubbleImpactClock {
    double time = 0;
    bool cancelled = false;
    bool ended = false;
};

class CBubbleVestige {
  public:
    using MonitorId = hyprimate::animation::MonitorId;
    using WindowId = hyprimate::animation::WindowId;
    using ScreenFacts = hyprimate::animation::ScreenFacts;
    bool deposit(MonitorId monitor, hyprimate::Vec2 centre,
        std::span<const hyprimate::BubbleImpact> impacts, const SP<SBubbleImpactClock> &clock);
    std::uint64_t trail(MonitorId monitor, WindowId owner, std::uint64_t wake,
        hyprimate::Vec2 centre, std::span<const hyprimate::BubbleTrailPoint> points,
        std::span<const hyprimate::BubbleRelease> releases);
    void tick(const ScreenFacts &screen);
    void queue_under(Render::CRenderContext &ctx, const ScreenFacts &screen, WindowId window,
        const SP<CBubbleRenderer> &renderer, bool live);
    void queue(Render::CRenderContext &ctx, const ScreenFacts &screen,
        const SP<CBubbleRenderer> &renderer, bool live);
    [[nodiscard]] bool active(MonitorId monitor) const;
    void set_enabled(bool enabled);
    void clear(std::optional<MonitorId> monitor = std::nullopt);

  private:
    struct Deposit {
        MonitorId monitor = 0;
        hyprimate::Vec2 position{};
        double radius = 0, impact_radius = 0;
        hyprimate::Vec2 drift{};
        double lifetime = 0;
        std::chrono::steady_clock::time_point last{};
        double age = 0;
        SP<SBubbleImpactClock> clock;
        double land = 0;
    };
    struct Wake {
        MonitorId monitor = 0;
        WindowId owner = 0;
        std::uint64_t id = 0;
        hyprimate::BubbleWake film;
        std::chrono::steady_clock::time_point last;
        bool queued = false;
    };
    Wake *wake_for(MonitorId monitor, std::uint64_t wake_id);
    Wake &begin_wake(MonitorId monitor, WindowId owner);
    void queue_wake(Render::CRenderContext &ctx, const ScreenFacts &screen, Wake &wake,
        const SP<CBubbleRenderer> &renderer, bool live);
    bool queued(const Wake &wake, bool live) const;

    bool m_enabled = true;
    std::vector<Deposit> m_deposits;
    std::vector<Wake> m_wakes;
    std::vector<std::uint64_t> m_capture_wakes;
    std::uint64_t m_next_wake = 1;
};
