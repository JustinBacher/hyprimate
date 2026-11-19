#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include "Bubble.hpp"
#include "BubbleWallpaper.hpp"
#include "BubbleVestige.hpp"

// A transition's hold on its impact clock. However the transition stops, the clock is marked:
// cancel() when its droplets vanish (an aborted opening), otherwise ended, so pending beads never
// wait on a clock that no longer advances.
class CBubbleImpactLease {
  public:
    CBubbleImpactLease() = default;
    CBubbleImpactLease(const CBubbleImpactLease &) = delete;
    CBubbleImpactLease &operator=(const CBubbleImpactLease &) = delete;
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
        if (m_clock && std::isfinite(elapsed) && elapsed > 0) {
            m_clock->time += std::min(elapsed, hyprimate::STALL_SECONDS);
        }
    }
    void cancel() {
        if (m_clock) {
            m_clock->cancelled = true;
        }
        m_clock.reset();
    }
    void release() {
        if (m_clock) {
            m_clock->ended = true;
        }
        m_clock.reset();
    }
    [[nodiscard]] bool running() const {
        return static_cast<bool>(m_clock);
    }
    [[nodiscard]] const SP<SBubbleImpactClock> &clock() const {
        return m_clock;
    }

  private:
    SP<SBubbleImpactClock> m_clock;
};

struct SBubbleStyle {
    hyprimate::Bubble bubble;
    SP<SBubbleWallpaper> wallpaper;
    bool shell_impacts_deposited = false;
    bool window_impacts_deposited = false;
    CBubbleImpactLease impacts;
    std::uint64_t wake = 0;  // the Vestige wake this window's movement is laying down
    PHLWINDOWREF window;  // the window the film belongs to; its wakes lie under it
};

