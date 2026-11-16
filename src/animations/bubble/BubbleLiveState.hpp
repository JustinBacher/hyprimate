#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include "Bubble.hpp"
#include "BubbleRenderer.hpp"
#include "BubbleResidue.hpp"

// A transition's hold on its impact clock. However the transition stops, the clock is marked:
// cancel() when its droplets vanish (an aborted opening), otherwise ended, so pending beads never
// wait on a clock that no longer advances.
class CBubbleImpactLease {
  public:
    CBubbleImpactLease() = default;
    CBubbleImpactLease(CBubbleImpactLease &&) noexcept = default;
    CBubbleImpactLease &operator=(CBubbleImpactLease &&other) noexcept {
        if (this != &other) {
            release();
            m_clock = std::move(other.m_clock);
        }
        return *this;
    }
    ~CBubbleImpactLease() {
        release();
    }

    void start() {
        release();
        m_clock = makeShared<SBubbleImpactClock>();
    }
    void advance(double elapsed) {
        if (m_clock && std::isfinite(elapsed) && elapsed > 0)
            m_clock->time += std::min(elapsed, hyprimate::kStallSeconds);
    }
    void cancel() {
        if (m_clock) m_clock->cancelled = true;
        m_clock.reset();
    }
    void release() {
        if (m_clock) m_clock->ended = true;
        m_clock.reset();
    }
    bool running() const {
        return bool(m_clock);
    }
    const SP<SBubbleImpactClock> &clock() const {
        return m_clock;
    }

  private:
    SP<SBubbleImpactClock> m_clock;
};

struct SBubbleStyle {
    hyprimate::Bubble bubble;
    SP<CBubbleRenderer> renderer;
    SP<SBubbleWallpaper> wallpaper;
    SP<CBubbleResidue> residue;
    bool shellImpactsDeposited = false;
    bool windowImpactsDeposited = false;
    CBubbleImpactLease impacts;
    std::uint64_t wake = 0;  // the residue wake this window's movement is laying down
};

// Hands each surface's glass impacts to the residue once, as soon as its first hole has opened.
// Pending beads land when the transition's shared clock reaches them, so they neither lead their
// droplets nor outlive an aborted opening. Callers exclude capture and read-only passes. centre is
// the frame centre in monitor-local logical coordinates.
inline void depositBubbleImpacts(SBubbleStyle &state,
    const hyprimate::BubbleFrame &frame,
    const PHLMONITOR &monitor,
    Vector2D centre) {
    if (!state.residue || !monitor) return;
    const auto deposit = [&](const hyprimate::BubbleSurface &surface, bool &done) {
        if (done) return;
        const auto holes =
            std::span(surface.holes).first(std::min(surface.holeCount, surface.holes.size()));
        if (std::ranges::none_of(
                holes, [](const hyprimate::BubbleHole &hole) { return hole.radius > 0; }))
            return;
        std::array<hyprimate::BubbleImpact, hyprimate::kBubbleMaxImpacts> found{};
        const auto count = hyprimate::bubbleImpacts(surface, found);
        // A disabled residue declines the beads; they are not retried later.
        state.residue->deposit(
            monitor, centre, std::span(found).first(count), state.impacts.clock());
        done = true;
    };
    deposit(frame.shell, state.shellImpactsDeposited);
    deposit(frame.window, state.windowImpactsDeposited);
}
