#pragma once

#include <chrono>
#include <span>
#include <vector>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "animations/bubble/Bubble.hpp"

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

// Screen-space deposits retain neither the window nor its textures.
class CBubbleResidue {
  public:
    bool deposit(const PHLMONITOR &monitor,
        Vector2D centre,
        std::span<const hyprimate::BubbleImpact> impacts,
        const SP<SBubbleImpactClock> &clock);
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
    bool m_enabled = true;
    std::vector<Deposit> m_deposits;
};
