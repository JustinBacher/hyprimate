#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "animations/shared/Core.hpp"

namespace hyprimate {

    // Liquid material tuning. Finite values are clamped to [0, 1]; nonfinite Liquid values reset
    // before integration.
    struct LiquidParams {
        double viscosity = 0.75;  // [0,1] Liquid flow resistance (existing semantics)
        double elasticity =
            0.5;  // [0,1] restoring stiffness / recovery speed; higher = snappier, firmer return
        double momentum = 0.5;  // [0,1] inertia carried past a stop (overshoot amount); 0 = none
        // [0,1] Liquid-only per-motion randomness: each movement from rest draws its own timing,
        // damping, lag, bow shape, skew, sideways lean and response latency from the mesh's seed. 0
        // = every mesh responds identically.
        double variation = 0.0;
        bool operator==(const LiquidParams &) const = default;
    };

    // Logical-pixel material state; no compositor or graphics dependency.
    class LiquidMesh {
      public:
        // Meshes with the same params and seed respond identically; give each window its own seed.
        explicit LiquidMesh(LiquidParams params = {}, std::uint64_t seed = 0) noexcept;
        // Retune the current character without disturbing simulation or random state.
        void set_params(LiquidParams params) noexcept;
        [[nodiscard]] const LiquidParams &params() const noexcept {
            return m_params;
        }
        [[nodiscard]] double viscosity() const noexcept {
            return m_params.viscosity;
        }
        void reset() noexcept;
        void record_translation(Vec2 delta, Vec2 size, double strength) noexcept;
        bool advance(double elapsed_seconds, Vec2 size) noexcept;
        [[nodiscard]] bool settled() const noexcept;
        // C1-smooth interpolation of the 9x9 lattice field; both fractions are clamped to [0, 1].
        [[nodiscard]] Vec2 sample_displacement(
            double horizontal_fraction, double vertical_fraction) const noexcept;
        [[nodiscard]] const std::array<Vec2, 81> &displacements() const noexcept {
            return m_displacements;
        }

      private:
        bool constrain(Vec2 size) noexcept;
        [[nodiscard]] bool valid_triangles(Vec2 size) const noexcept;
        void integrate_liquid_substep(Vec2 size) noexcept;
        // Draws a new Liquid character (gains and shape) scaled by params.variation; neutral when
        // variation is 0.
        void roll_character() noexcept;
        void update_coefficients() noexcept;
        double draw_unit() noexcept;  // uniform in [-1, 1] from m_rng

        LiquidParams m_params;
        // Viscosity-derived velocity coefficient for internal shear flow.
        double m_shear_damping;
        // Liquid body and head spring gains, derived from elasticity, momentum and the current
        // character.
        double m_body_stiffness = 0;
        double m_body_damping = 0;
        double m_head_stiffness = 0;
        double m_head_damping = 0;
        // Liquid character, redrawn by roll_character at each motion onset; all neutral when
        // variation is 0.
        double m_lag_gain = 1;  // trailing lag multiplier
        double m_bow_depth = 0;  // trailing-edge bow depth (corner lag share removed)
        double m_taper_ratio = 0;  // cross-axis tail taper per unit lag
        double m_skew = 0;  // cross-axis offset of the bow's deepest point, in s units
        double m_lean = 0;  // sideways tail shear per unit lag
        double m_kick_gain = 1;  // stop-kick multiplier
        double m_velocity_seconds = 0;  // window-velocity filter time constant (response latency)
        std::uint64_t m_rng = 0;  // splitmix64 state
        std::array<double, 9> m_character{};

        std::array<Vec2, 81> m_displacements{};
        std::array<Vec2, 81> m_velocities{};
        std::array<Vec2, 81> m_accelerations{};
        // Liquid-only scratch: divergence of the pre-substep relative field, recomputed every
        // substep.
        std::array<double, 81> m_divergence{};
        // Liquid-only: per-node head grip in [0, 1] (1 = firm leading face), relaxed toward its
        // target each substep.
        std::array<double, 81> m_grip{};
        // Liquid-only: translation recorded since the last advance, and the window velocity it
        // implies.
        Vec2 m_pending_delta{};
        Vec2 m_window_velocity{};
        Vec2 m_smooth_delta{};  // exponentially weighted translation (px), decays with the velocity
                                // time constant
        double m_smooth_time =
            0;  // exponentially weighted time (s), same weights as m_smooth_delta
        Vec2 m_slow_delta{};  // slower (inertia) estimate of translation (px), averages over
                              // zero-delta frames
        double m_slow_time = 0;  // matching slower time (s)
        Vec2 m_slow_velocity{};  // slower velocity the inertia is differentiated from
        Vec2 m_inertia{};  // pseudo-force from smoothed deceleration, applied on every substep of
                           // one advance
        double m_strength = 0;

        double m_accumulator = 0;
        unsigned m_quiet_steps = 0;
    };

}  // namespace hyprimate
