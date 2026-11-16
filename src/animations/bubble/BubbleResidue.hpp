#pragma once

#include <chrono>
#include <span>
#include <vector>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleWake.hpp"

class CBubbleRenderer;

// The animation clock of one bubble transition, shared with the beads its droplets will leave: a
// pending bead lands exactly when this clock reaches its impact, never on wall time the transition
// did not play. The transition advances it with the bubble's own stall clamp and, when it stops,
// marks it either cancelled (its droplets vanished) or ended.
struct SBubbleImpactClock {
    double time = 0;
    bool cancelled = false;
    bool ended = false;
};

// Screen-space deposits retain neither the window nor its textures: glass beads from ruptures, and
// the wakes of film and microbubbles that moving windows leave behind.
class CBubbleResidue {
  public:
    bool deposit(const PHLMONITOR &monitor,
        Vector2D centre,
        std::span<const hyprimate::BubbleImpact> impacts,
        const SP<SBubbleImpactClock> &clock);
    // Lays a moving window's film and released microbubbles down on the glass. centre is the frame
    // centre in monitor-local logical px; wake is the wake the window continued last (0: none).
    // Returns the wake to continue next time.
    std::uint64_t trail(const PHLMONITOR &monitor,
        std::uint64_t wake,
        Vector2D centre,
        std::span<const hyprimate::BubbleTrailPoint> points,
        std::span<const hyprimate::BubbleRelease> releases);
    void tick(std::chrono::steady_clock::time_point now, const PHLMONITOR &monitor);
    void queue(Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<CBubbleRenderer> &renderer);
    bool active(const PHLMONITOR &monitor) const;
    void setEnabled(bool enabled);
    void clear();

  private:
    struct Deposit {
        PHLMONITORREF monitor;
        Vector2D position;
        double radius;
        double impactRadius;
        Vector2D drift;
        double lifetime;
        std::chrono::steady_clock::time_point
            last;  // previous tick, once the bead keeps its own time
        double age = 0.0;
        SP<SBubbleImpactClock> clock;  // source transition until the bead has landed
        double land = 0;  // impact time on that clock
    };
    struct Wake {
        PHLMONITORREF monitor;
        std::uint64_t id = 0;
        hyprimate::BubbleWake film;
        std::chrono::steady_clock::time_point last;
    };
    Wake *wakeFor(const PHLMONITOR &monitor, std::uint64_t id);
    Wake &beginWake(const PHLMONITOR &monitor);

    bool m_enabled = true;
    std::vector<Deposit> m_deposits;
    std::vector<Wake> m_wakes;
    std::uint64_t m_nextWake = 1;
};
