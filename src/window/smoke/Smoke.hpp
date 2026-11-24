#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

#include "shared/Core.hpp"

namespace hyprimate {

    // The smoke body's underlying shape: a rounded rectangle around the window centre (plus
    // carry), stretched along `stretch` and displaced by inertia. Identity at mass zero. During
    // opening and closing it becomes the head.
    struct SmokeBody {
        Vec2 half;     // half extents before stretching
        double radius = 0;
        Vec2 offset;   // centre from the window centre
        Vec2 stretch;  // travel direction scaled by elongation, 0 at rest
    };

    // Per-frame drive for the GPU smoke fluid. Lengths are logical pixels.
    struct SmokeFrame {
        Vec2 size;
        Vec2 velocity;  // smoothed window velocity, px/s
        Vec2 lag;       // slower-responding velocity: the body's inertia and delayed stretch
        Vec2 carry;     // body offset from the window centre while a layout move settles
        Vec2 position;  // window translation so far, including any not yet stepped; anchors the fluid to the world
        Vec2 offset;    // fluid domain centre from the window centre: room for the wake behind
        double time = 0;
        double seed = 0;
        double smoke = 0;     // material coordinate: 0 exact source, 1 entirely smoke
        double presence = 1;  // window material still feeding the smoke body
        double turbulence = 0.65;
        double trail = 0.65;
        double life = 0.94;      // seconds after wake smoke leaves the body
        double trail_length = 0;  // logical pixels beyond the body; zero uses the automatic reach
        double trail_thickness = 1;  // multiplier for the width of shed smoke
        double gather = 0;     // opening: strength of loose streams converging on the window
        double cohesion = 0;   // pull of loose smoke back into the window rectangle
        double dispersal = 0;  // closing: outward breakup and accelerated decay
        double fade = 0;       // residual smoke visibility once nothing feeds the fluid
        double mass = 0;       // 0 resting window shape, 1 fully contracted moving body
        double drag_follow = 0;  // active attachment weight, fades back to free smoke on release
        double contraction = 0;  // body shrink rate in 1/s; negative while expanding
        double settle = 0;       // reforming after motion: strength of wisps peeling off the body
        double mass_size = 0.45;
        double mass_roundness = 0.7;
        // Opening and closing: the smoke body is a travelling mass (the head) rather than the
        // window. Positions are window-relative logical pixels.
        double transit = 0;    // 0 movement body, 1 head body
        Vec2 head;             // head centre
        Vec2 head_velocity;    // px/s; the body's flow follows it
        Vec2 head_lag;         // slower head velocity: the head's delayed stretch
        Vec2 head_half;
        double head_radius = 0;
        Vec2 origin_uv;        // window point where the opening forms or the close unravels first
        double order = 0;      // negative for opening and its wake; positive orders the close
        double attract = 0;    // pull of unravelled window material after the head
        double wind = 0;       // signed strength of the circulation around the head
        Vec2 edge_point;       // entering smoke streams in across the screen edge here
        Vec2 edge_dir;         // unit, from the window toward that edge; zero for none
        std::uint32_t color = 0;
        double density = 1;
        double variation = 0.5;
        // Solid source shape at close start, until the material is mapped to the window.
        std::optional<SmokeBody> close_source_body;
    };

    [[nodiscard]] SmokeBody smoke_body(const SmokeFrame &frame) noexcept;

    // Where opening smoke enters or closing smoke leaves.
    struct SmokeRoute {
        bool edge = false;  // false: inside the window
        Vec2 direction;     // unit, from the window toward the screen edge
        double reach = 0;   // capture centre to that edge along direction, logical px
    };

    struct SmokeParams {
        double strength = 0.85;
        double turbulence = 0.65;
        double trail = 0.65;
        double trail_length = 0;
        double trail_lifetime = 0;  // seconds; zero derives the lifetime from trail amount
        double trail_thickness = 1;
        double reform = 0.48;
        double settle_speed = 1;
        double mass = 0.45;
        double roundness = 0.7;
        std::uint32_t color = 0;
        double density = 1;
        double variation = 0.5;
        double drag_follow = 0;
    };

    // World-space extent of the fluid around the window centre. The margin bounds the wake.
    [[nodiscard]] inline Vec2 smoke_domain(Vec2 size, double trail_length = 0) noexcept {
        const double AUTO_MARGIN = std::clamp(0.85 * std::max(size.x, size.y), 200.0, 800.0);
        // Make room for the reach, including the feather at the domain edge and the largest
        // shift of the domain toward the wake. Short reaches trim the wake, not the fluid grid.
        const double MARGIN = trail_length > 0
                                  ? std::max(AUTO_MARGIN, trail_length + 64.0 + 0.6 * AUTO_MARGIN)
                                  : AUTO_MARGIN;
        return {.x = size.x + (2.0 * MARGIN), .y = size.y + (2.0 * MARGIN)};
    }

    class Smoke {
      public:
        void seed(std::uint64_t value) noexcept;
        void begin(Transition direction, double duration, Vec2 size, SmokeRoute route = {}) noexcept;
        void drive(Vec2 translation, bool layout_animated = false, Vec2 size_change = {}) noexcept;
        void advance(
            double elapsed, Vec2 size, SmokeParams params = {}, bool pointer_drag = false) noexcept;
        void reset() noexcept;
        void stop_motion() noexcept;
        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool opening() const noexcept;
        [[nodiscard]] bool closing() const noexcept;
        [[nodiscard]] bool closed() const noexcept;
        [[nodiscard]] SmokeFrame frame(Vec2 size) const noexcept;
        // Bounds relative to the capture's top-left: the whole fluid domain.
        [[nodiscard]] Box bounds(Vec2 size) const noexcept;

      private:
        // Head, conversion and flow drive for opening or closing at progress in [0, 1].
        void plan_transit(double progress) noexcept;

        SmokeFrame m_frame;
        SmokeParams m_params;
        SmokeRoute m_route;
        Vec2 m_layout_pending;
        Vec2 m_pending;
        double m_resize_pending = 0;  // accumulated half-extent travel, including centred resizes
        Vec2 m_carry_velocity;
        Vec2 m_trail;  // recent path of the window, weighted like the wake it leaves; centres the domain
        std::uint64_t m_rng = 1;
        double m_seed = 0;  // raw per-window seed; variation scales how far it moves the noise
        // Per-transition draws in [0, 1]; variation scales their effect each step, so the
        // randomness stays constant within an animation.
        std::array<double, 6> m_draw{0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
        Vec2 m_from_head;  // head at the start of a transition interrupting another
        Vec2 m_from_half;
        double m_from_radius = 0;
        double m_from_transit = 0;
        double m_accumulator = 0;
        double m_motion_smoke = 0;
        double m_motion_speed = 0;
        double m_elapsed = 0;
        double m_duration = 1;
        double m_start_smoke = 0;
        double m_start_presence = 1;
        double m_residual = 0;
        bool m_opening = false;
        bool m_closing = false;
        bool m_closed = false;
    };

}  // namespace hyprimate
