#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mdspan>

#include "shared/Core.hpp"
#include "window/liquid/LiquidMesh.hpp"

namespace hyprimate {
    namespace {

        using Field = std::array<Vec2, 81>;

        constexpr unsigned SIDE = 9;  // nodes per lattice side
        constexpr unsigned CELLS = 8;  // cells per lattice side
        using GridExtents = std::extents<std::size_t, SIDE, SIDE>;
        using ConstFieldView = std::mdspan<const Vec2, GridExtents, std::layout_left>;
        using FieldView = std::mdspan<Vec2, GridExtents, std::layout_left>;
        using ScalarView = std::mdspan<double, GridExtents, std::layout_left>;
        using ConstScalarView = std::mdspan<const double, GridExtents, std::layout_left>;

        // Tolerates floating-point drift so frame cadences that land exactly on a substep boundary
        // (for example 144 Hz frames summed to 0.5 s) take that substep rather than deferring it.
        constexpr double ACCUMULATOR_TOLERANCE = SUBSTEP_SECONDS * 1e-9;
        constexpr double MAX_FRAME_SECONDS = 0.05;
        constexpr unsigned MAX_SUBSTEPS_PER_ADVANCE = 12;

        // Liquid mode.
        // Body natural frequency (rad/s) at elasticity 0.5; elasticity scales it by 2^(elasticity -
        // 0.5).
        constexpr double LIQUID_BASE_OMEGA = 26.0;
        // Head springs are 4x body stiffness and critically damped, so the leading face grips.
        constexpr double LIQUID_HEAD_STIFFNESS_SCALE = 4.0;
        constexpr double LIQUID_SHEAR_STIFFNESS = 60.0;
        // Viscosity maps linearly onto shear-velocity damping: 4 at viscosity 0, 48 at viscosity 1.
        constexpr double LIQUID_SHEAR_DAMPING_MIN = 4.0;
        constexpr double LIQUID_SHEAR_DAMPING_RANGE = 44.0;
        // Pressure wave speed as a multiple of the shorter window side.
        constexpr double LIQUID_WAVE_SPEED_SCALE = 1.5;
        // Steady trailing lag in px at strength 1: lag = K * V^2 / (V + V0), V the window speed in
        // px/s. Quadratic for slow drags (gentle), about linear for fast ones.
        constexpr double LIQUID_LAG_SECONDS = 0.075;
        constexpr double LIQUID_LAG_SPEED = 600.0;
        // Trailing edge bows back at its middle: corners lag (1 - LIQUID_BOW_DEPTH) of the centre.
        constexpr double LIQUID_BOW_DEPTH = 0.5;
        // Trailing corners taper inward cross-axis by this fraction of the centre lag. The taper
        // and the bow both follow t^2, so the side edges sag into curves toward the tail instead of
        // sloping straight.
        constexpr double LIQUID_TAPER_RATIO = 0.18;
        // Grip relaxes toward its target with this time constant, so a stop releases the leading
        // face over ~100 ms.
        constexpr double LIQUID_GRIP_RELAX_SECONDS = 0.04;
        // Inertia gain, scaled by momentum: the leading face takes the smoothed deceleration as a
        // forward push at a stop.
        constexpr double LIQUID_STOP_KICK = 2.2;
        // Trailing edge share of the stop kick relative to the leading face, which sets the forward
        // surge.
        constexpr double LIQUID_STOP_TRAIL_RATIO = 1.0;
        // Time constant of the window-velocity estimate, so irregular position updates still read
        // as steady motion.
        constexpr double LIQUID_VELOCITY_SECONDS = 0.06;
        // Time constant of the slower velocity the inertia is differentiated from; averages
        // irregular frames further.
        constexpr double LIQUID_INERTIA_SECONDS = 0.15;
        // Smoothed speed (px/s) below which the head grip releases, so only a sustained stop frees
        // the leading face.
        constexpr double LIQUID_GRIP_QUIET_SPEED = 2.0;
        // Deceleration rate (1/s) relative to speed that counts as a landing and releases the grip
        // at once; a pure stop reads 1/LIQUID_VELOCITY_SECONDS, so steady drag ripple stays below
        // this.
        constexpr double LIQUID_GRIP_LANDING_RATE = 4.0;
        // Window speed cap in shorter sides per second, so a bad sample cannot launch the lattice.
        constexpr double LIQUID_MAX_SPEED_SCALE = 10.0;
        // Head grip ramps from 0 at this q (on the trailing side of the leading face) to 1 at q
        // = 1.
        constexpr double LIQUID_GRIP_START_Q = 0.25;

        // Per-motion variation spreads at variation 1; each factor takes an independent uniform
        // draw r in [-1, 1].
        constexpr double VARY_OMEGA_OCTAVES = 0.4;  // body frequency x 2^(0.4 r): -24% to +32%
        constexpr double VARY_DAMPING =
            0.35;  // underdamped ratio x (1 + 0.35 r), never above critical
        constexpr double VARY_LAG = 0.3;  // lag x (1 + 0.3 r)
        constexpr double VARY_BOW_DEPTH = 0.3;  // bow depth 0.5 + 0.3 r
        constexpr double VARY_TAPER = 0.6;  // taper x (1 + 0.6 r)
        constexpr double VARY_SKEW = 0.5;  // bow centre offset across the motion, in s units
        constexpr double VARY_LEAN = 0.25;  // sideways tail shear as a share of the lag
        constexpr double VARY_KICK = 0.4;  // stop kick x (1 + 0.4 r)
        constexpr double VARY_VELOCITY_SECONDS =
            0.35;  // velocity filter x (1 + 0.35 r): response latency 39-81 ms
        constexpr double MIN_DAMPING_RATIO = 0.1;

        constexpr double MAX_DISPLACEMENT_LIMIT = 120.0;
        constexpr double MAX_DISPLACEMENT_FRACTION = 0.2;
        constexpr double MIN_AREA_RATIO = 0.2;
        constexpr unsigned MAX_HALVINGS = 8;

        constexpr double QUIET_DISPLACEMENT = 0.15;
        constexpr double QUIET_VELOCITY = 2.0;
        constexpr unsigned QUIET_SUBSTEPS = 3;

        struct GridPoint {
            unsigned x;
            unsigned y;
        };

        bool is_finite(Vec2 value) noexcept {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        bool is_valid_size(Vec2 size) noexcept {
            return is_finite(size) && size.x > 0.0 && size.y > 0.0;
        }

        // Finite parameters are clamped to [0, 1]. Nonfinite input is kept so Liquid can reject it
        // before integration, rather than being replaced by a fabricated default.
        double normalize_param(double value) noexcept {
            return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : value;
        }

        LiquidParams normalize_params(LiquidParams params) noexcept {
            params.viscosity = normalize_param(params.viscosity);
            params.elasticity = normalize_param(params.elasticity);
            params.momentum = normalize_param(params.momentum);
            params.variation = normalize_param(params.variation);
            return params;
        }

        bool has_finite_params(const LiquidParams &params) noexcept {
            return std::isfinite(params.viscosity) && std::isfinite(params.elasticity) &&
                   std::isfinite(params.momentum) && std::isfinite(params.variation);
        }

        // Damping ratio from momentum: 1 = critically damped (no overshoot). Momentum 0.5 gives a
        // trailing surge of about 20% of the lag after a stop; momentum 1 gives a larger, still
        // decaying surge.
        double damping_ratio(double momentum) noexcept {
            return momentum <= 0.5 ? 1.0 - (1.2 * momentum) : 0.4 - (0.4 * (momentum - 0.5));
        }

        // Displacement magnitude bound: a soft saturation with this scale replaces the old hard
        // clamp for Liquid.
        double displacement_limit(Vec2 size) noexcept {
            return std::min(
                MAX_DISPLACEMENT_LIMIT, MAX_DISPLACEMENT_FRACTION * std::min(size.x, size.y));
        }

        // Soft saturation of one displacement. Magnitudes up to 0.75 of the limit pass through
        // unchanged, so routine per-call constraint adds no damping and the response is independent
        // of call cadence. Above the knee the magnitude follows a tanh that is C1 at the knee and
        // approaches limit. The outward component of velocity is scaled by sech^2 of the tanh, the
        // derivative of the map above the knee, so saturation does not inject energy. Below the
        // knee nothing changes.
        struct SaturationRange {
            double magnitude;
            double limit;
        };

        void soft_saturate(Vec2 &displacement, Vec2 &velocity, SaturationRange range) noexcept {
            const double KNEE = 0.75 * range.limit;
            if (range.magnitude <= KNEE) {
                return;
            }
            const double SPAN = range.limit - KNEE;
            const double CURVE = std::tanh((range.magnitude - KNEE) / SPAN);
            const double MAPPED = KNEE + (SPAN * CURVE);
            const double NORMAL_X = displacement.x / range.magnitude;
            const double NORMAL_Y = displacement.y / range.magnitude;
            const double RADIAL_VELOCITY = (velocity.x * NORMAL_X) + (velocity.y * NORMAL_Y);
            const double SECH2 = 1.0 - (CURVE * CURVE);
            velocity.x -= (1.0 - SECH2) * RADIAL_VELOCITY * NORMAL_X;
            velocity.y -= (1.0 - SECH2) * RADIAL_VELOCITY * NORMAL_Y;
            displacement = {.x = NORMAL_X * MAPPED, .y = NORMAL_Y * MAPPED};
        }

        bool is_zero(const Field &field) noexcept {
            return !std::ranges::any_of(
                field, [](const Vec2 &position) { return position.x != 0.0 || position.y != 0.0; });
        }

        bool is_zero(Vec2 value) noexcept {
            return value.x == 0.0 && value.y == 0.0;
        }

        Vec2 rest_position(GridPoint point, Vec2 size) noexcept {
            return {.x = size.x * point.x / CELLS, .y = size.y * point.y / CELLS};
        }

        double signed_area(Vec2 first, Vec2 second, Vec2 third) noexcept {
            return 0.5 * (((second.x - first.x) * (third.y - first.y)) -
                             ((second.y - first.y) * (third.x - first.x)));
        }

        // Corners of the two triangles in the given cell, in renderer order.
        std::array<GridPoint, 3> triangle_corners(GridPoint cell, unsigned triangle) noexcept {
            if (triangle == 0) {
                return {
                    GridPoint{.x = cell.x, .y = cell.y},
                    GridPoint{.x = cell.x, .y = cell.y + 1},
                    GridPoint{.x = cell.x + 1, .y = cell.y},
                };
            }
            return {
                GridPoint{.x = cell.x + 1, .y = cell.y},
                GridPoint{.x = cell.x, .y = cell.y + 1},
                GridPoint{.x = cell.x + 1, .y = cell.y + 1},
            };
        }

        struct MotionFields {
            std::reference_wrapper<const Field> displacements;
            std::reference_wrapper<const Field> velocities;
        };

        bool is_quiet(MotionFields fields) noexcept {
            const Field &displacements = fields.displacements.get();
            const Field &velocities = fields.velocities.get();
            constexpr double MAX_DISPLACEMENT2 = QUIET_DISPLACEMENT * QUIET_DISPLACEMENT;
            constexpr double MAX_VELOCITY2 = QUIET_VELOCITY * QUIET_VELOCITY;
            const auto *velocity_it = velocities.begin();
            for (const Vec2 &displacement : displacements) {
                const Vec2 &velocity = *velocity_it++;
                if ((displacement.x * displacement.x) + (displacement.y * displacement.y) >=
                        MAX_DISPLACEMENT2 ||
                    (velocity.x * velocity.x) + (velocity.y * velocity.y) >= MAX_VELOCITY2) {
                    return false;
                }
            }
            return true;
        }

        // Head grip for a lattice coordinate q in [-1, 1] (q = 1 is the leading face): 0 on the
        // body, rising smoothly to 1 at the leading face. Smoothstep keeps forces continuous across
        // the ramp.
        double head_grip(double coordinate) noexcept {
            return smoothstep(LIQUID_GRIP_START_Q, 1.0, coordinate);
        }

        // Derivative along one lattice line: central inside, one-sided at the two free edges.
        // `sample(index)` returns the scalar along the line; `spacing` is the lattice spacing.
        template <typename Sample>
        double line_slope(Sample sample, unsigned index, double spacing) noexcept {
            if (index == 0) {
                return (sample(1) - sample(0)) / spacing;
            }
            if (index == SIDE - 1) {
                return (sample(SIDE - 1) - sample(SIDE - 2)) / spacing;
            }
            return (sample(index + 1) - sample(index - 1)) / (2.0 * spacing);
        }

        // Second difference along one lattice line with mirror ghosts at the free edges.
        // The ghost takes the interior neighbour's value, so the edge node feels no outward pull.
        template <typename Sample>
        double line_laplacian(Sample sample, unsigned index) noexcept {
            const double LEFT = (index == 0) ? sample(1) : sample(index - 1);
            const double RIGHT = (index == SIDE - 1) ? sample(SIDE - 2) : sample(index + 1);
            return LEFT + RIGHT - (2.0 * sample(index));
        }

        struct LatticeColumn {
            unsigned value;
        };
        struct LatticeRow {
            unsigned value;
        };
        struct ShearWeightX {
            double value;
        };
        struct ShearWeightY {
            double value;
        };

        // Anisotropic index-space Laplacian of one Vec2 component; weights scale each axis.
        double shear_laplacian(ConstFieldView field,
            double Vec2::*component,
            LatticeColumn column,
            LatticeRow row,
            ShearWeightX weight_x,
            ShearWeightY weight_y) noexcept {
            const double LAPLACE_X = line_laplacian(
                [&](unsigned index) { return field[index, row.value].*component; }, column.value);
            const double LAPLACE_Y = line_laplacian(
                [&](unsigned index) { return field[column.value, index].*component; }, row.value);
            return (weight_x.value * LAPLACE_X) + (weight_y.value * LAPLACE_Y);
        }

        // Catmull-Rom cubic from start to end as fraction runs from 0 to 1.
        double catmull_rom(
            double before, double start, double end, double after, double fraction) noexcept {
            return start +
                   (0.5 * fraction *
                       (end - before +
                           (fraction *
                               ((2.0 * before) - (5.0 * start) + (4.0 * end) - after +
                                   (fraction * ((3.0 * (start - end)) + after - before))))));
        }

    }  // namespace

    LiquidMesh::LiquidMesh(LiquidParams params, std::uint64_t seed) noexcept
        : m_params(normalize_params(params)),
          m_shear_damping(
              LIQUID_SHEAR_DAMPING_MIN + (LIQUID_SHEAR_DAMPING_RANGE * m_params.viscosity)),
          m_rng(seed) {
        roll_character();
    }

    double LiquidMesh::draw_unit() noexcept {
        // Uniform in [-1, 1) from the top 53 bits of m_rng.
        return (2.0 * unit_draw(m_rng)) - 1.0;
    }

    void LiquidMesh::roll_character() noexcept {
        for (double &value : m_character) {
            value = draw_unit();
        }
        update_coefficients();
    }

    void LiquidMesh::set_params(LiquidParams params) noexcept {
        m_params = normalize_params(params);
        update_coefficients();
    }

    void LiquidMesh::update_coefficients() noexcept {
        m_shear_damping =
            LIQUID_SHEAR_DAMPING_MIN + (LIQUID_SHEAR_DAMPING_RANGE * m_params.viscosity);
        const double AMOUNT = std::isfinite(m_params.variation) ? m_params.variation : 0.0;
        std::array<double, 9> draws{};
        for (std::size_t i = 0; i < draws.size(); ++i) {
            draws.at(i) = AMOUNT * m_character.at(i);
        }

        const double OMEGA =
            LIQUID_BASE_OMEGA *
            std::exp2(m_params.elasticity - 0.5 + (VARY_OMEGA_OCTAVES * std::get<0>(draws)));
        double zeta = damping_ratio(m_params.momentum);
        if (zeta < 1.0) {
            zeta = std::clamp(
                zeta * (1.0 + (VARY_DAMPING * std::get<1>(draws))), MIN_DAMPING_RATIO, 1.0);
        }
        m_body_stiffness = OMEGA * OMEGA;
        m_body_damping = 2.0 * zeta * OMEGA;
        m_head_stiffness = LIQUID_HEAD_STIFFNESS_SCALE * m_body_stiffness;
        // Head is always critically damped, so its damping is 2 * sqrt(stiffness).
        m_head_damping = 2.0 * std::sqrt(m_head_stiffness);

        m_lag_gain = 1.0 + (VARY_LAG * std::get<2>(draws));
        m_bow_depth = LIQUID_BOW_DEPTH + (VARY_BOW_DEPTH * std::get<3>(draws));
        m_taper_ratio = LIQUID_TAPER_RATIO * (1.0 + (VARY_TAPER * std::get<4>(draws)));
        m_skew = VARY_SKEW * std::get<5>(draws);
        m_lean = VARY_LEAN * std::get<6>(draws);
        m_kick_gain = 1.0 + (VARY_KICK * std::get<7>(draws));
        m_velocity_seconds =
            LIQUID_VELOCITY_SECONDS * (1.0 + (VARY_VELOCITY_SECONDS * std::get<8>(draws)));
    }

    void LiquidMesh::reset() noexcept {
        m_displacements.fill(Vec2{});
        m_velocities.fill(Vec2{});
        m_grip.fill(0.0);
        m_pending_delta = {};
        m_window_velocity = {};
        m_smooth_delta = {};
        m_smooth_time = 0.0;
        m_inertia = {};
        m_slow_delta = {};
        m_slow_time = 0.0;
        m_slow_velocity = {};
        m_strength = 0.0;
        m_accumulator = 0.0;
        m_quiet_steps = 0;
    }

    void LiquidMesh::record_translation(Vec2 delta, Vec2 size, double strength) noexcept {
        // A nonfinite Liquid parameter has no defined response, so the field must not be excited.
        if (!is_finite(delta) || !is_valid_size(size) || !std::isfinite(strength) ||
            strength <= 0.0 || !has_finite_params(m_params)) {
            reset();
            return;
        }

        const double LENGTH = std::hypot(delta.x, delta.y);
        if (!std::isfinite(LENGTH)) {
            reset();
            return;
        }

        if (LENGTH > 0.0) {
            // Each movement from rest gets its own character, so meshes moved by the same delta
            // still differ.
            if (settled()) {
                roll_character();
            }
            m_pending_delta.x += delta.x;
            m_pending_delta.y += delta.y;
            m_strength = strength;
            m_quiet_steps = 0;
        }

        // Applies bounds and triangle protection to the field, including for zero delta.
        constrain(size);
    }

    // Liquid substep. The field d relaxes toward a steady lag field that depends
    // only on window velocity: the trailing edge bows back at its middle, the trailing corners
    // taper inward cross-axis, and the leading face carries no lag. Each node blends body and head
    // stiffness by its grip. Neighbour coupling (shear and pressure) smooths the field into one
    // continuous body. When motion stops the lag field returns to zero, so the trailing mass surges
    // forward and rings down. Divergence and every acceleration read only old state, so the update
    // is simultaneous.
    void LiquidMesh::integrate_liquid_substep(Vec2 size) noexcept {
        Field &displacements = m_displacements;
        Field &velocities = m_velocities;
        Field &accelerations = m_accelerations;
        const ConstFieldView FIELD{displacements.data()};
        const ConstFieldView VELOCITY_FIELD{velocities.data()};
        const FieldView ACCELERATION_FIELD{accelerations.data()};
        const ScalarView DIVERGENCE{m_divergence.data()};
        const ConstScalarView DIVERGENCE_FIELD{m_divergence.data()};
        const ScalarView GRIP_FIELD{m_grip.data()};

        const Vec2 SPACING{.x = size.x / CELLS, .y = size.y / CELLS};
        // Scale each axis's shear so the tighter axis keeps the index-space stiffness and
        // the looser axis is weakened in proportion, giving comparable physical coupling.
        const double TIGHTER = std::min(SPACING.x, SPACING.y);
        const double WEIGHT_X = (TIGHTER / SPACING.x) * (TIGHTER / SPACING.x);
        const double WEIGHT_Y = (TIGHTER / SPACING.y) * (TIGHTER / SPACING.y);
        const double WAVE_SPEED = LIQUID_WAVE_SPEED_SCALE * std::min(size.x, size.y);
        const double WAVE_SPEED2 = WAVE_SPEED * WAVE_SPEED;

        for (unsigned div_row = 0; div_row < SIDE; ++div_row) {
            for (unsigned div_col = 0; div_col < SIDE; ++div_col) {
                const double SLOPE_X = line_slope(
                    [&](unsigned column) { return FIELD[column, div_row].x; }, div_col, SPACING.x);
                const double SLOPE_Y = line_slope(
                    [&](unsigned row) { return FIELD[div_col, row].y; }, div_row, SPACING.y);
                DIVERGENCE[div_col, div_row] = SLOPE_X + SLOPE_Y;
            }
        }

        // Lag magnitude in px: quadratic in speed for slow drags, about linear for fast ones.
        const double SPEED = std::hypot(m_window_velocity.x, m_window_velocity.y);
        const double LAG = SPEED > 0.0 ? LIQUID_LAG_SECONDS * m_lag_gain * m_strength * SPEED *
                                             SPEED / (SPEED + LIQUID_LAG_SPEED)
                                       : 0.0;
        // With no motion the lag is zero, so the direction is unused and any unit vector serves.
        const double DIR_X = SPEED > 0.0 ? m_window_velocity.x / SPEED : 1.0;
        const double DIR_Y = SPEED > 0.0 ? m_window_velocity.y / SPEED : 0.0;
        const double PERP_X = -DIR_Y;
        const double PERP_Y = DIR_X;
        // A unit direction has |dx| + |dy| >= 1, so this normalizer is always positive.
        const double NORMALIZER = 0.5 * (std::abs(DIR_X) + std::abs(DIR_Y));
        const double GRIP_BLEND = 1.0 - std::exp(-SUBSTEP_SECONDS / LIQUID_GRIP_RELAX_SECONDS);
        const double SHEAR_DAMPING = m_shear_damping;
        // The bow's deepest point sits at s = m_skew; dividing by 1 + |skew| keeps the far corner's
        // lag at (1 - bow depth) of the deepest point, so a skewed bow never turns a trailing
        // corner forward.
        const double SKEW_SCALE = 1.0 / (1.0 + std::abs(m_skew));
        // Landing: smoothed deceleration is a large share of speed, so the front releases at once
        // instead of waiting for the smoothed speed to fall through the quiet threshold.
        const bool LANDING =
            std::hypot(m_inertia.x, m_inertia.y) > LIQUID_GRIP_LANDING_RATE * SPEED;

        for (unsigned row = 0; row < SIDE; ++row) {
            for (unsigned col = 0; col < SIDE; ++col) {
                // Lattice position relative to the centre, in [-0.5, 0.5].
                const double LATTICE_X = (static_cast<double>(col) / CELLS) - 0.5;
                const double LATTICE_Y = (static_cast<double>(row) / CELLS) - 0.5;
                // q: -1 at the trailing edge, +1 at the leading face. s: -1..1 across the motion.
                const double ALONG_MOTION =
                    std::clamp(((LATTICE_X * DIR_X) + (LATTICE_Y * DIR_Y)) / NORMALIZER, -1.0, 1.0);
                const double ACROSS_MOTION = std::clamp(
                    ((LATTICE_X * PERP_X) + (LATTICE_Y * PERP_Y)) / NORMALIZER, -1.0, 1.0);
                // Grip relaxes toward head grip while moving and toward zero when still, so a stop
                // releases the front.
                GRIP_FIELD[col, row] +=
                    ((SPEED > LIQUID_GRIP_QUIET_SPEED && !LANDING ? head_grip(ALONG_MOTION) : 0.0) -
                        GRIP_FIELD[col, row]) *
                    GRIP_BLEND;
                const double GRIP = GRIP_FIELD[col, row];
                const double STIFFNESS =
                    (m_body_stiffness * (1.0 - GRIP)) + (m_head_stiffness * GRIP);
                const double LEAD = std::max(ALONG_MOTION, 0.0);
                const double TRAIL = std::max(-ALONG_MOTION, 0.0);
                // Inertia weights: the leading face carries the push forward and the trailing edge
                // a weaker share.
                const double KICK_WEIGHT =
                    LIQUID_STOP_KICK * m_kick_gain * m_params.momentum *
                    ((LEAD * LEAD) + (LIQUID_STOP_TRAIL_RATIO * TRAIL * TRAIL));
                const double DAMPING = (m_body_damping * (1.0 - GRIP)) + (m_head_damping * GRIP);

                // t: 1 at the trailing edge, 0 at the leading face. The trailing edge bows back at
                // its middle and the trailing corners taper cross-axis; both follow t^2 so the side
                // edges curve toward the tail. The character skews the bow off-centre and leans the
                // tail sideways (a shear along perp).
                const double TAIL_BLEND = 0.5 * (1.0 - ALONG_MOTION);
                const double TAIL_BLEND_SQUARED = TAIL_BLEND * TAIL_BLEND;
                const double OFFSET = (ACROSS_MOTION - m_skew) * SKEW_SCALE;
                const double BOW = TAIL_BLEND_SQUARED * (1.0 - (m_bow_depth * OFFSET * OFFSET));
                const double TAPER =
                    LAG * TAIL_BLEND_SQUARED * ((m_taper_ratio * ACROSS_MOTION) + m_lean);
                const Vec2 LAG_FIELD{
                    .x = -((LAG * BOW * DIR_X) + (TAPER * PERP_X)),
                    .y = -((LAG * BOW * DIR_Y) + (TAPER * PERP_Y)),
                };

                const double GRAD_X = line_slope(
                    [&](unsigned column) { return DIVERGENCE_FIELD[column, row]; }, col, SPACING.x);
                const double GRAD_Y = line_slope(
                    [&](unsigned index) { return DIVERGENCE_FIELD[col, index]; }, row, SPACING.y);

                const double COUPLED_X = (LIQUID_SHEAR_STIFFNESS * shear_laplacian(FIELD,
                                                                       &Vec2::x,
                                                                       LatticeColumn{col},
                                                                       LatticeRow{row},
                                                                       ShearWeightX{WEIGHT_X},
                                                                       ShearWeightY{WEIGHT_Y})) +
                                         (SHEAR_DAMPING * shear_laplacian(VELOCITY_FIELD,
                                                              &Vec2::x,
                                                              LatticeColumn{col},
                                                              LatticeRow{row},
                                                              ShearWeightX{WEIGHT_X},
                                                              ShearWeightY{WEIGHT_Y})) +
                                         (WAVE_SPEED2 * GRAD_X);
                const double COUPLED_Y = (LIQUID_SHEAR_STIFFNESS * shear_laplacian(FIELD,
                                                                       &Vec2::y,
                                                                       LatticeColumn{col},
                                                                       LatticeRow{row},
                                                                       ShearWeightX{WEIGHT_X},
                                                                       ShearWeightY{WEIGHT_Y})) +
                                         (SHEAR_DAMPING * shear_laplacian(VELOCITY_FIELD,
                                                              &Vec2::y,
                                                              LatticeColumn{col},
                                                              LatticeRow{row},
                                                              ShearWeightX{WEIGHT_X},
                                                              ShearWeightY{WEIGHT_Y})) +
                                         (WAVE_SPEED2 * GRAD_Y);

                ACCELERATION_FIELD[col, row] = {
                    .x = (-STIFFNESS * (FIELD[col, row].x - LAG_FIELD.x)) -
                         (DAMPING * VELOCITY_FIELD[col, row].x) + COUPLED_X +
                         (m_inertia.x * KICK_WEIGHT),
                    .y = (-STIFFNESS * (FIELD[col, row].y - LAG_FIELD.y)) -
                         (DAMPING * VELOCITY_FIELD[col, row].y) + COUPLED_Y +
                         (m_inertia.y * KICK_WEIGHT),
                };
            }
        }
        // The pseudo-force decays exponentially across each tick, so its shape does not depend on
        // the tick length.
        const double INERTIA_DECAY = std::exp(-SUBSTEP_SECONDS / LIQUID_INERTIA_SECONDS);
        m_inertia = {.x = m_inertia.x * INERTIA_DECAY, .y = m_inertia.y * INERTIA_DECAY};

        for (std::size_t i = 0; i < displacements.size(); ++i) {
            velocities.at(i).x += accelerations.at(i).x * SUBSTEP_SECONDS;
            velocities.at(i).y += accelerations.at(i).y * SUBSTEP_SECONDS;
            displacements.at(i).x += velocities.at(i).x * SUBSTEP_SECONDS;
            displacements.at(i).y += velocities.at(i).y * SUBSTEP_SECONDS;
        }
    }

    bool LiquidMesh::advance(double elapsed_seconds, Vec2 size) noexcept {
        if (!is_valid_size(size) || !std::isfinite(elapsed_seconds) ||
            elapsed_seconds > STALL_SECONDS || !has_finite_params(m_params)) {
            reset();
            return false;
        }

        // Window velocity is the ratio of exponentially weighted translation to weighted time, so
        // irregular or zero-delta frames average into the true speed instead of reading as a stop.
        // Its change between ticks gives the inertia pseudo-force: only deceleration or reversal
        // along the previous motion is carried forward.
        if (elapsed_seconds > 0.0) {
            const double DECAY = std::exp(-elapsed_seconds / m_velocity_seconds);
            m_smooth_delta = {
                .x = (m_smooth_delta.x * DECAY) + m_pending_delta.x,
                .y = (m_smooth_delta.y * DECAY) + m_pending_delta.y,
            };
            m_smooth_time = (m_smooth_time * DECAY) + elapsed_seconds;
            Vec2 velocity{
                .x = m_smooth_delta.x / m_smooth_time,
                .y = m_smooth_delta.y / m_smooth_time,
            };
            if (!is_finite(velocity)) {
                reset();
                return false;
            }
            const double SPEED = std::hypot(velocity.x, velocity.y);
            const double CAP = LIQUID_MAX_SPEED_SCALE * std::min(size.x, size.y);
            if (SPEED > CAP) {
                velocity.x *= CAP / SPEED;
                velocity.y *= CAP / SPEED;
            }
            // Slower velocity for the inertia: averaging over zero-delta frames keeps lumpy
            // arrivals from reading as deceleration, so only a real stop or reversal pushes the
            // leading face.
            const double SLOW_DECAY = std::exp(-elapsed_seconds / LIQUID_INERTIA_SECONDS);
            m_slow_delta = {
                .x = (m_slow_delta.x * SLOW_DECAY) + m_pending_delta.x,
                .y = (m_slow_delta.y * SLOW_DECAY) + m_pending_delta.y,
            };
            m_slow_time = (m_slow_time * SLOW_DECAY) + elapsed_seconds;
            Vec2 slow_velocity{
                .x = m_slow_delta.x / m_slow_time,
                .y = m_slow_delta.y / m_slow_time,
            };
            if (!is_finite(slow_velocity)) {
                reset();
                return false;
            }
            // Bound the estimate before differentiating and storing it, not just the lag speed.
            const double SLOW_SPEED = std::hypot(slow_velocity.x, slow_velocity.y);
            if (SLOW_SPEED > CAP) {
                slow_velocity.x *= CAP / SLOW_SPEED;
                slow_velocity.y *= CAP / SLOW_SPEED;
            }
            // Raw deceleration along the previous slow motion; zero on acceleration or steady drag.
            Vec2 raw_decel{};
            const double PREVIOUS_SLOW_SPEED = std::hypot(m_slow_velocity.x, m_slow_velocity.y);
            if (PREVIOUS_SLOW_SPEED > 0.0) {
                const double DIR_X = m_slow_velocity.x / PREVIOUS_SLOW_SPEED;
                const double DIR_Y = m_slow_velocity.y / PREVIOUS_SLOW_SPEED;
                const double DVX = (slow_velocity.x - m_slow_velocity.x) / elapsed_seconds;
                const double DVY = (slow_velocity.y - m_slow_velocity.y) / elapsed_seconds;
                const double DECEL = -((DVX * DIR_X) + (DVY * DIR_Y));
                if (DECEL > 0.0) {
                    raw_decel = {.x = DIR_X * DECEL, .y = DIR_Y * DECEL};
                }
            }
            m_slow_velocity = slow_velocity;
            // Tick-start rate of an exponential pseudo-force whose integral over this tick equals
            // the velocity change: R0 * tau * (1 - exp(-dt/tau)) = raw_decel * dt. Substeps decay
            // it, so cadence does not change the push.
            const double TICK_SHAPE = -std::expm1(-elapsed_seconds / LIQUID_INERTIA_SECONDS);
            const double TICK_GAIN = elapsed_seconds / (LIQUID_INERTIA_SECONDS * TICK_SHAPE);
            m_inertia = {.x = raw_decel.x * TICK_GAIN, .y = raw_decel.y * TICK_GAIN};
            m_window_velocity = velocity;
            m_pending_delta = {};
        }

        // Applies the current geometry's bounds even when no substep runs.
        if (!constrain(size)) {
            return false;
        }

        if (settled()) {
            m_accumulator = 0.0;
            m_quiet_steps = 0;
            return false;
        }

        m_accumulator += std::max(0.0, std::min(elapsed_seconds, MAX_FRAME_SECONDS));

        unsigned steps = 0;
        while (m_accumulator >= SUBSTEP_SECONDS - ACCUMULATOR_TOLERANCE) {
            if (steps == MAX_SUBSTEPS_PER_ADVANCE) {
                m_accumulator = 0.0;
                break;
            }
            m_accumulator -= SUBSTEP_SECONDS;
            ++steps;

            integrate_liquid_substep(size);

            if (!constrain(size)) {
                return false;
            }

            const bool QUIET =
                is_quiet({.displacements = m_displacements, .velocities = m_velocities});
            if (QUIET) {
                if (++m_quiet_steps >= QUIET_SUBSTEPS) {
                    reset();
                    return false;
                }
            } else {
                m_quiet_steps = 0;
            }
        }

        return !settled();
    }

    bool LiquidMesh::settled() const noexcept {
        if (!is_zero(m_displacements) || !is_zero(m_velocities)) {
            return false;
        }
        return is_zero(m_window_velocity) && is_zero(m_pending_delta);
    }

    Vec2 LiquidMesh::sample_displacement(
        double horizontal_fraction, double vertical_fraction) const noexcept {
        if (!std::isfinite(horizontal_fraction) || !std::isfinite(vertical_fraction)) {
            return {};
        }

        const double GRID_X = std::clamp(horizontal_fraction, 0.0, 1.0) * CELLS;
        const double GRID_Y = std::clamp(vertical_fraction, 0.0, 1.0) * CELLS;
        const int CELL_COUNT = static_cast<int>(CELLS);
        const int COL = std::min(static_cast<int>(std::floor(GRID_X)), CELL_COUNT - 1);
        const int ROW = std::min(static_cast<int>(std::floor(GRID_Y)), CELL_COUNT - 1);
        const double FRACTION_X = GRID_X - COL;
        const double FRACTION_Y = GRID_Y - ROW;

        const ConstFieldView FIELD{m_displacements.data()};
        // Node values with edge clamping, so the spline reproduces the boundary field.
        const auto NODE = [&](int column, int row) -> Vec2 {
            const unsigned CLAMPED_COL = static_cast<unsigned>(std::clamp(column, 0, CELL_COUNT));
            const unsigned CLAMPED_ROW = static_cast<unsigned>(std::clamp(row, 0, CELL_COUNT));
            return FIELD[CLAMPED_COL, CLAMPED_ROW];
        };

        // Interpolate along x for the four rows around the sample, then along y.
        std::array<Vec2, 4> column{};
        int row_offset = 0;
        for (auto &sample : column) {
            const int ROW_INDEX = ROW - 1 + row_offset++;
            const Vec2 SAMPLE_BACK = NODE(COL - 1, ROW_INDEX);
            const Vec2 SAMPLE_LEFT = NODE(COL, ROW_INDEX);
            const Vec2 SAMPLE_RIGHT = NODE(COL + 1, ROW_INDEX);
            const Vec2 SAMPLE_NEXT = NODE(COL + 2, ROW_INDEX);
            sample = {
                .x = catmull_rom(
                    SAMPLE_BACK.x, SAMPLE_LEFT.x, SAMPLE_RIGHT.x, SAMPLE_NEXT.x, FRACTION_X),
                .y = catmull_rom(
                    SAMPLE_BACK.y, SAMPLE_LEFT.y, SAMPLE_RIGHT.y, SAMPLE_NEXT.y, FRACTION_X),
            };
        }
        return {
            .x = catmull_rom(std::get<0>(column).x,
                std::get<1>(column).x,
                std::get<2>(column).x,
                std::get<3>(column).x,
                FRACTION_Y),
            .y = catmull_rom(std::get<0>(column).y,
                std::get<1>(column).y,
                std::get<2>(column).y,
                std::get<3>(column).y,
                FRACTION_Y),
        };
    }

    bool LiquidMesh::constrain(Vec2 size) noexcept {
        for (std::size_t i = 0; i < m_displacements.size(); ++i) {
            if (!is_finite(m_displacements.at(i)) || !is_finite(m_velocities.at(i))) {
                reset();
                return false;
            }
        }

        const double LIMIT = displacement_limit(size);
        for (std::size_t i = 0; i < m_displacements.size(); ++i) {
            Vec2 &displacement = m_displacements.at(i);
            Vec2 &velocity = m_velocities.at(i);
            const double MAGNITUDE = std::hypot(displacement.x, displacement.y);
            if (MAGNITUDE > 0.0) {
                soft_saturate(displacement, velocity, {.magnitude = MAGNITUDE, .limit = LIMIT});
            }
        }

        for (unsigned halvings = 0;; ++halvings) {
            if (valid_triangles(size)) {
                return true;
            }
            if (halvings == MAX_HALVINGS) {
                break;
            }
            for (std::size_t i = 0; i < m_displacements.size(); ++i) {
                m_displacements.at(i) = {
                    .x = m_displacements.at(i).x * 0.5,
                    .y = m_displacements.at(i).y * 0.5,
                };
                m_velocities.at(i) = {
                    .x = m_velocities.at(i).x * 0.5, .y = m_velocities.at(i).y * 0.5};
            }
        }

        reset();
        return false;
    }

    bool LiquidMesh::valid_triangles(Vec2 size) const noexcept {
        const ConstFieldView FIELD{m_displacements.data()};
        for (unsigned cell_row = 0; cell_row < CELLS; ++cell_row) {
            for (unsigned cell_column = 0; cell_column < CELLS; ++cell_column) {
                for (unsigned triangle = 0; triangle < 2; ++triangle) {
                    const auto CORNERS =
                        triangle_corners({.x = cell_column, .y = cell_row}, triangle);
                    std::array<Vec2, 3> rest{};
                    std::array<Vec2, 3> deformed{};
                    auto *rest_point = rest.begin();
                    auto *deformed_point = deformed.begin();
                    for (const GridPoint &corner : CORNERS) {
                        *rest_point = rest_position(corner, size);
                        const Vec2 DISPLACEMENT = FIELD[corner.x, corner.y];
                        *deformed_point = {
                            .x = rest_point->x + DISPLACEMENT.x,
                            .y = rest_point->y + DISPLACEMENT.y,
                        };
                        ++rest_point;
                        ++deformed_point;
                    }

                    // Rest orientation is the same sign for all triangles, so the ratio is positive
                    // while orientation is kept and negative or zero after a foldover or collapse.
                    const double REST_AREA =
                        signed_area(std::get<0>(rest), std::get<1>(rest), std::get<2>(rest));
                    if (!std::isfinite(REST_AREA) || REST_AREA == 0.0) {
                        return false;
                    }
                    const double AREA = signed_area(
                        std::get<0>(deformed), std::get<1>(deformed), std::get<2>(deformed));
                    if (!(AREA / REST_AREA >= MIN_AREA_RATIO)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

}  // namespace hyprimate
