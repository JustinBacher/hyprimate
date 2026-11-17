#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "animations/shared/Core.hpp"

namespace hyprimate {

    // Swarm tuning. `count`, `speed` and `stagger` are absolute; the rest are clamped to [0, 1].
    struct SwarmParams {
        int count = 250;  // agents; each carries one piece of the window
        double speed = 1400.0;  // cruise speed, logical px per second
        double stagger =
            0.6;  // seconds over which agents arrive (open) or let go (close) one after another
        double cohesion = 0.5;  // pull that keeps neighbours travelling together
        double separation = 0.5;  // personal space: sidestepping, and how hard bodies collide
        double responsiveness =
            0.5;  // acceleration, and the window speed an agent holds formation through
        double settling = 0.5;  // eagerness to lock back into formation
        double wander = 0.5;  // meander and hesitation while travelling
        double variation = 0.5;  // per-agent spread of pace, grip and route
        bool operator==(const SwarmParams &) const = default;
    };

    inline constexpr int kSwarmMaxAgents = 400;

    // An agent's body, which other agents cannot walk through: a disc of this share of its `fit`.
    // Smaller than the creature drawn, so legs may cross, and small enough that neighbours in
    // formation never touch.
    inline constexpr double kSwarmBodyShare = 0.6;

    // Where a window sits, in one logical-pixel space. `capture` is the decorated box including its
    // shadow, `body` the window inside it, `screen` the monitor the swarm enters and leaves
    // through.
    struct SwarmPlacement {
        Box capture;
        Box body;
        Box screen;
    };

    // One member of the swarm. Positions are relative to the capture box's top-left corner.
    struct SwarmAgent {
        // What a renderer reads.
        Box tile;  // the piece of the capture box this agent carries
        Vec2 home;  // where `position` rests in formation: the centre of the tile's share of the
                    // body
        double fit = 0.0;  // radius of the largest circle around `home` inside that share
        double reach = 0.0;  // distance from `home` to the tile's farthest corner
        Vec2 position;  // where `home` is now
        double heading = 0.0;  // direction of travel, radians (atan2(y, x), y down)
        double rest = 0.0;  // per-agent reference angle for the carried tile, not creature facing
        double turn = 0.0;  // 0 tile upright .. 1 tile rotated by heading minus rest
        double lock = 0.0;  // 0 free creature .. 1 merged into the window as its whole tile
        double travel = 0.0;  // px walked so far
        double walk = 0.0;  // readable gait phase, advancing until fully merged
        bool visible = false;  // false while waiting beyond the screen and after leaving

        // Simulation state.
        Vec2 velocity;  // px per second, in the screen's frame
        Vec2 exit;  // closing: the point beyond the screen it runs to
        double delay = 0.0;  // seconds before it enters (open) or lets go (close)
        double due = 0.0;  // opening: seconds until it is meant to reach home; it hurries or
                           // dawdles to keep to it
        double pace = 1.0;  // its share of the cruise speed
        double grip = 0.0;  // window speed (px/s) it stays locked through
        double reaction = 0.0;  // seconds it stays put after the window shakes it loose
        double pause = 0.0;  // what is left of that
        double settleWait =
            -1.0;  // movement: randomized wait at home before docking; negative until arrival
        double arc =
            0.0;  // radians its approach is turned off the straight line; it comes in on a curve
        double weave = 0.0;  // meander phase
        double weaveRate = 0.0;  // meander phase speed, rad/s
        double gait = 0.0;  // hesitation phase
        double gaitRate = 0.0;  // hesitation phase speed, rad/s
        bool docked = false;  // resting on `home` and riding with the window
        bool leaving = false;  // closing: heading for `exit`
        bool gone = false;  // closing: has left the screen
    };

    // A flock of agents that carry a window between them. The window's capture box is cut into one
    // tile per agent, laid in rows of near-square tiles over the body; the outermost tiles also
    // take the decoration margin, so the tiles together are exactly the capture box. Each agent
    // steers a point (its tile's `home`) on a fixed 1/120 s step. It does not walk straight at its
    // target: it approaches on its own arc, meanders and hesitates on the way, sidesteps agents in
    // its path, and cannot walk through another loose agent's body. Near its home a spring takes
    // over and places it exactly, facing its own `rest` direction. In formation an agent is docked:
    // locked on its home and riding with the window; docked agents are part of the window and loose
    // ones walk over them. Opening sends each agent in from a random point beyond a random one of
    // the screen edges, timed to arrive at a random moment within `stagger`, so the window fills in
    // no order; movement shakes loose those whose grip the window outruns, and they chase their
    // homes; closing lets them go in random order over `stagger` and runs each off a random point
    // of the screen edges. No compositor dependency; equal seeds and inputs give equal motion.
    class Swarm {
      public:
        // Open: agents start beyond `sides` of the screen and travel into formation. Close: agents
        // leave through `sides`; a close begun while agents are loose continues from where they
        // are. Unusable boxes, an empty `sides` or non-finite parameters leave the model as it was.
        void begin(Transition kind,
            const SwarmParams &params,
            std::uint64_t seed,
            const SwarmPlacement &placement,
            EdgeSet sides);
        // Starts in formation, ready to follow the window. Ignored unless idle.
        void beginMovement(
            const SwarmParams &params, std::uint64_t seed, const SwarmPlacement &placement);
        // The window is now at `placement` in screen space. Translation is applied on the next
        // advance: loose agents keep their screen positions and chase their moving homes.
        // A new size re-cuts the tiles. Ignored unless opening or moving.
        void updateMovement(const SwarmPlacement &placement);
        // Steps the simulation. Non-finite or negative time counts as zero; more than kStallSeconds
        // is clamped to it. While a movement gesture is held, wander keeps agents loose even when
        // the window is stationary; releasing it lets them regroup. Returns active().
        bool advance(double elapsedSeconds, bool movementHeld = false);
        // Back to idle, in formation.
        void reset() noexcept;

        bool active() const noexcept {
            return m_phase != Phase::Idle && m_phase != Phase::Closed;
        }
        bool opening() const noexcept {
            return m_phase == Phase::Opening;
        }
        bool moving() const noexcept {
            return m_phase == Phase::Movement;
        }
        bool closed() const noexcept {
            return m_phase == Phase::Closed;
        }

        const SwarmParams &params() const noexcept {
            return m_params;
        }
        std::span<const SwarmAgent> agents() const noexcept {
            return m_agents;
        }
        // Global surface rounding: 1 while assembling, 0 at native-window handoff.
        double rounding() const noexcept {
            return m_rounding;
        }
        // Size of the capture box the tiles are cut from.
        Vec2 size() const noexcept {
            return m_size;
        }
        // Box around every visible agent's tile at any rotation, relative to the capture box's
        // top-left corner and clipped to the screen. Zero-sized when nothing is visible.
        Box bounds() const noexcept;

      private:
        enum class Phase { Idle, Opening, Movement, Closing, Closed };

        void build(const SwarmParams &params, std::uint64_t seed, const SwarmPlacement &placement);
        void layout(const SwarmPlacement &placement);
        void step(bool wandering);
        void hash();
        // What the loose agents around one agent do to it.
        struct Crowd {
            Vec2 steer;  // acceleration: sidestep those in its way, keep its distance, match and
                         // follow the group
            Vec2 shove;  // displacement out of the bodies it overlaps
            Vec2 check;  // velocity change that stops it closing on those bodies
        };
        Crowd crowd(std::size_t index, Vec2 forward, double force) const;

        Phase m_phase = Phase::Idle;
        SwarmParams m_params{};
        std::vector<SwarmAgent> m_agents;
        int m_rows = 0;  // fixed for an animation so resizing does not reassign tiles
        Vec2 m_origin{};  // capture top-left in the placement's space
        Vec2 m_size{};  // capture size
        Box m_body{};  // body, relative to the capture's top-left
        Box m_screen{};  // screen, relative to the capture's top-left
        double m_spacing = 1.0;  // mean tile side; scales every neighbourhood radius
        Vec2 m_shift{};  // window translation not yet applied
        double m_windowSpeed = 0.0;  // px/s, held briefly so docking waits for the window to slow
        double m_still = 0.0;  // seconds since the window last moved
        double m_time = 0.0;  // seconds since the phase began
        double m_rounding = 0.0;
        double m_accumulator = 0.0;
        std::uint64_t m_rng = 0;

        // Spatial hash of the loose agents, rebuilt each step; buffers are reused.
        std::vector<std::uint32_t> m_cellStart;
        std::vector<std::uint32_t> m_cellItems;
        std::vector<std::uint32_t> m_cellOf;
    };

}  // namespace hyprimate
