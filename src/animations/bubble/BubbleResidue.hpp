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
// the wakes of film and microbubbles that moving windows leave behind. Beads lie on the glass over
// every window; a wake lies just under the window that left it.
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
        const PHLWINDOW &owner,
        std::uint64_t wake,
        Vector2D centre,
        std::span<const hyprimate::BubbleTrailPoint> points,
        std::span<const hyprimate::BubbleRelease> releases);
    // Advances the monitor's residue; once per frame, before it renders.
    void tick(std::chrono::steady_clock::time_point now, const PHLMONITOR &monitor);
    // Before a window renders: the wakes it left, beneath it.
    void queue_under(Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const PHLWINDOW &window,
        const SP<CBubbleRenderer> &renderer);
    // After every window: the beads, and wakes no window drew this frame.
    void queue(Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        const SP<CBubbleRenderer> &renderer);
    [[nodiscard]] bool active(const PHLMONITOR &monitor) const;
    void set_enabled(bool enabled);
    void clear();

  private:
    struct Deposit {
        PHLMONITORREF monitor;
        Vector2D position;
        double radius;
        double impact_radius;
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
        PHLWINDOWREF owner;  // the window it lies under
        std::uint64_t id = 0;
        hyprimate::BubbleWake film;
        std::chrono::steady_clock::time_point last;
        bool queued = false;  // drawn in the monitor's current frame
    };
    Wake *wake_for(const PHLMONITOR &monitor, std::uint64_t wake_id);
    Wake &begin_wake(const PHLMONITOR &monitor, const PHLWINDOW &owner);
    static void queue_wake(Render::CRenderContext &ctx,
        const PHLMONITOR &monitor,
        Wake &wake,
        const SP<CBubbleRenderer> &renderer);

    bool m_enabled = true;
    std::vector<Deposit> m_deposits;
    std::vector<Wake> m_wakes;
    std::uint64_t m_next_wake = 1;
};
