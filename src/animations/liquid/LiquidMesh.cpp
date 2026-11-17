#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "animations/liquid/LiquidMesh.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {
    namespace {

        using Field = std::array<Vec2, 81>;
        using Scalars = std::array<double, 81>;

        constexpr unsigned kSide = 9;  // nodes per lattice side
        constexpr unsigned kCells = 8;  // cells per lattice side

        // Tolerates floating-point drift so frame cadences that land exactly on a substep boundary
        // (for example 144 Hz frames summed to 0.5 s) take that substep rather than deferring it.
        constexpr double kAccumulatorTolerance = SUBSTEP_SECONDS * 1e-9;
        constexpr double kMaxFrameSeconds = 0.05;
        constexpr unsigned kMaxSubstepsPerAdvance = 12;

        // Liquid mode.
        // Body natural frequency (rad/s) at elasticity 0.5; elasticity scales it by 2^(elasticity -
        // 0.5).
        constexpr double kLiquidBaseOmega = 26.0;
        // Head springs are 4x body stiffness and critically damped, so the leading face grips.
        constexpr double kLiquidHeadStiffnessScale = 4.0;
        constexpr double kLiquidShearStiffness = 60.0;
        // Viscosity maps linearly onto shear-velocity damping: 4 at viscosity 0, 48 at viscosity 1.
        constexpr double kLiquidShearDampingMin = 4.0;
        constexpr double kLiquidShearDampingRange = 44.0;
        // Pressure wave speed as a multiple of the shorter window side.
        constexpr double kLiquidWaveSpeedScale = 1.5;
        // Steady trailing lag in px at strength 1: lag = K * V^2 / (V + V0), V the window speed in
        // px/s. Quadratic for slow drags (gentle), about linear for fast ones.
        constexpr double kLiquidLagSeconds = 0.075;
        constexpr double kLiquidLagSpeed = 600.0;
        // Trailing edge bows back at its middle: corners lag (1 - kLiquidBowDepth) of the centre.
        constexpr double kLiquidBowDepth = 0.5;
        // Trailing corners taper inward cross-axis by this fraction of the centre lag. The taper
        // and the bow both follow t^2, so the side edges sag into curves toward the tail instead of
        // sloping straight.
        constexpr double kLiquidTaperRatio = 0.18;
        // Grip relaxes toward its target with this time constant, so a stop releases the leading
        // face over ~100 ms.
        constexpr double kLiquidGripRelaxSeconds = 0.04;
        // Inertia gain, scaled by momentum: the leading face takes the smoothed deceleration as a
        // forward push at a stop.
        constexpr double kLiquidStopKick = 2.2;
        // Trailing edge share of the stop kick relative to the leading face, which sets the forward
        // surge.
        constexpr double kLiquidStopTrailRatio = 1.0;
        // Time constant of the window-velocity estimate, so irregular position updates still read
        // as steady motion.
        constexpr double kLiquidVelocitySeconds = 0.06;
        // Time constant of the slower velocity the inertia is differentiated from; averages
        // irregular frames further.
        constexpr double kLiquidInertiaSeconds = 0.15;
        // Smoothed speed (px/s) below which the head grip releases, so only a sustained stop frees
        // the leading face.
        constexpr double kLiquidGripQuietSpeed = 2.0;
        // Deceleration rate (1/s) relative to speed that counts as a landing and releases the grip
        // at once; a pure stop reads 1/kLiquidVelocitySeconds, so steady drag ripple stays below
        // this.
        constexpr double kLiquidGripLandingRate = 4.0;
        // Window speed cap in shorter sides per second, so a bad sample cannot launch the lattice.
        constexpr double kLiquidMaxSpeedScale = 10.0;
        // Head grip ramps from 0 at this q (on the trailing side of the leading face) to 1 at q
        // = 1.
        constexpr double kLiquidGripStartQ = 0.25;

        // Per-motion variation spreads at variation 1; each factor takes an independent uniform
        // draw r in [-1, 1].
        constexpr double kVaryOmegaOctaves = 0.4;  // body frequency x 2^(0.4 r): -24% to +32%
        constexpr double kVaryDamping =
            0.35;  // underdamped ratio x (1 + 0.35 r), never above critical
        constexpr double kVaryLag = 0.3;  // lag x (1 + 0.3 r)
        constexpr double kVaryBowDepth = 0.3;  // bow depth 0.5 + 0.3 r
        constexpr double kVaryTaper = 0.6;  // taper x (1 + 0.6 r)
        constexpr double kVarySkew = 0.5;  // bow centre offset across the motion, in s units
        constexpr double kVaryLean = 0.25;  // sideways tail shear as a share of the lag
        constexpr double kVaryKick = 0.4;  // stop kick x (1 + 0.4 r)
        constexpr double kVaryVelocitySeconds =
            0.35;  // velocity filter x (1 + 0.35 r): response latency 39-81 ms
        constexpr double kMinDampingRatio = 0.1;

        constexpr double kMaxDisplacementLimit = 120.0;
        constexpr double kMaxDisplacementFraction = 0.2;
        constexpr double kMinAreaRatio = 0.2;
        constexpr unsigned kMaxHalvings = 8;

        constexpr double kQuietDisplacement = 0.15;
        constexpr double kQuietVelocity = 2.0;
        constexpr unsigned kQuietSubsteps = 3;

        struct GridPoint {
            unsigned x;
            unsigned y;
        };

        constexpr std::size_t gridIndex(unsigned x, unsigned y) noexcept {
            return (static_cast<std::size_t>(y) * kSide) + x;
        }

        bool isFinite(Vec2 v) noexcept {
            return std::isfinite(v.x) && std::isfinite(v.y);
        }

        bool isValidSize(Vec2 size) noexcept {
            return isFinite(size) && size.x > 0.0 && size.y > 0.0;
        }

        // Finite parameters are clamped to [0, 1]. Nonfinite input is kept so Liquid can reject it
        // before integration, rather than being replaced by a fabricated default.
        double normalizeParam(double value) noexcept {
            return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : value;
        }

        MaterialParams normalizeParams(MaterialParams p) noexcept {
            p.viscosity = normalizeParam(p.viscosity);
            p.elasticity = normalizeParam(p.elasticity);
            p.momentum = normalizeParam(p.momentum);
            p.variation = normalizeParam(p.variation);
            return p;
        }

        bool hasFiniteParams(const MaterialParams &p) noexcept {
            return std::isfinite(p.viscosity) && std::isfinite(p.elasticity) &&
                   std::isfinite(p.momentum) && std::isfinite(p.variation);
        }

        // Damping ratio from momentum: 1 = critically damped (no overshoot). Momentum 0.5 gives a
        // trailing surge of about 20% of the lag after a stop; momentum 1 gives a larger, still
        // decaying surge.
        double dampingRatio(double momentum) noexcept {
            return momentum <= 0.5 ? 1.0 - (1.2 * momentum) : 0.4 - (0.4 * (momentum - 0.5));
        }

        // Displacement magnitude bound: a soft saturation with this scale replaces the old hard
        // clamp for Liquid.
        double displacementLimit(Vec2 size) noexcept {
            return std::min(
                kMaxDisplacementLimit, kMaxDisplacementFraction * std::min(size.x, size.y));
        }

        // Soft saturation of one displacement. Magnitudes up to 0.75 of the limit pass through
        // unchanged, so routine per-call constraint adds no damping and the response is independent
        // of call cadence. Above the knee the magnitude follows a tanh that is C1 at the knee and
        // approaches limit. The outward component of velocity is scaled by sech^2 of the tanh, the
        // derivative of the map above the knee, so saturation does not inject energy. Below the
        // knee nothing changes.
        void softSaturate(Vec2 &d, Vec2 &v, double magnitude, double limit) noexcept {
            const double KNEE = 0.75 * limit;
            if (magnitude <= KNEE) {
                return;
            }
            const double SPAN = limit - KNEE;
            const double CURVE = std::tanh((magnitude - KNEE) / SPAN);
            const double MAPPED = KNEE + (SPAN * CURVE);
            const double NORMAL_X = d.x / magnitude;
            const double NORMAL_Y = d.y / magnitude;
            const double RADIAL_VELOCITY = (v.x * NORMAL_X) + (v.y * NORMAL_Y);
            const double SECH2 = 1.0 - (CURVE * CURVE);
            v.x -= (1.0 - SECH2) * RADIAL_VELOCITY * NORMAL_X;
            v.y -= (1.0 - SECH2) * RADIAL_VELOCITY * NORMAL_Y;
            d = {.x = NORMAL_X * MAPPED, .y = NORMAL_Y * MAPPED};
        }

        bool isZero(const Field &field) noexcept {
            for (const Vec2 &position : field) {
                if (position.x != 0.0 || position.y != 0.0) {
                    return false;
                }
            }
            return true;
        }

        bool isZero(Vec2 v) noexcept {
            return v.x == 0.0 && v.y == 0.0;
        }

        Vec2 restPosition(GridPoint p, Vec2 size) noexcept {
            return {.x = size.x * p.x / kCells, .y = size.y * p.y / kCells};
        }

        double signedArea(Vec2 a, Vec2 b, Vec2 c) noexcept {
            return 0.5 * (((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x)));
        }

        // Corners of the two triangles in cell (cx, cy), in renderer order.
        std::array<GridPoint, 3> triangleCorners(
            unsigned cx, unsigned cy, unsigned which) noexcept {
            if (which == 0) {
                return {GridPoint{.x = cx, .y = cy},
                    GridPoint{.x = cx, .y = cy + 1},
                    GridPoint{.x = cx + 1, .y = cy}};
            }
            return {GridPoint{.x = cx + 1, .y = cy},
                GridPoint{.x = cx, .y = cy + 1},
                GridPoint{.x = cx + 1, .y = cy + 1}};
        }

        bool isQuiet(const Field &displacements, const Field &velocities) noexcept {
            constexpr double MAX_DISPLACEMENT2 = kQuietDisplacement * kQuietDisplacement;
            constexpr double MAX_VELOCITY2 = kQuietVelocity * kQuietVelocity;
            for (std::size_t i = 0; i < displacements.size(); ++i) {
                const Vec2 &DISPLACEMENT = displacements[i];
                const Vec2 &VELOCITY = velocities[i];
                if ((DISPLACEMENT.x * DISPLACEMENT.x) + (DISPLACEMENT.y * DISPLACEMENT.y) >=
                        MAX_DISPLACEMENT2 ||
                    (VELOCITY.x * VELOCITY.x) + (VELOCITY.y * VELOCITY.y) >= MAX_VELOCITY2) {
                    return false;
                }
            }
            return true;
        }

        // Head grip for a lattice coordinate q in [-1, 1] (q = 1 is the leading face): 0 on the
        // body, rising smoothly to 1 at the leading face. Smoothstep keeps forces continuous across
        // the ramp.
        double headGrip(double q) noexcept {
            return smoothstep(kLiquidGripStartQ, 1.0, q);
        }

        // Derivative along one lattice line: central inside, one-sided at the two free edges.
        // `sample(k)` returns the scalar at position k along the line; `h` is the lattice spacing.
        template <typename Sample>
        double lineSlope(Sample sample, unsigned k, double h) noexcept {
            if (k == 0) {
                return (sample(1) - sample(0)) / h;
            }
            if (k == kSide - 1) {
                return (sample(kSide - 1) - sample(kSide - 2)) / h;
            }
            return (sample(k + 1) - sample(k - 1)) / (2.0 * h);
        }

        // Second difference along one lattice line with mirror ghosts at the free edges.
        // The ghost takes the interior neighbour's value, so the edge node feels no outward pull.
        template <typename Sample>
        double lineLaplacian(Sample sample, unsigned k) noexcept {
            const double LEFT = (k == 0) ? sample(1) : sample(k - 1);
            const double RIGHT = (k == kSide - 1) ? sample(kSide - 2) : sample(k + 1);
            return LEFT + RIGHT - (2.0 * sample(k));
        }

        // Anisotropic index-space Laplacian of one Vec2 component. wx and wy scale each axis.
        double shearLaplacian(const Field &f,
            double Vec2::*comp,
            unsigned x,
            unsigned y,
            double wx,
            double wy) noexcept {
            const double LAPLACE_X =
                lineLaplacian([&](unsigned k) { return f[gridIndex(k, y)].*comp; }, x);
            const double LAPLACE_Y =
                lineLaplacian([&](unsigned k) { return f[gridIndex(x, k)].*comp; }, y);
            return (wx * LAPLACE_X) + (wy * LAPLACE_Y);
        }

        // Catmull-Rom cubic through p1 and p2 at t in [0, 1]; equals p1 at t = 0 and p2 at t = 1.
        double catmullRom(double p0, double p1, double p2, double p3, double t) noexcept {
            return p1 + (0.5 * t *
                            (p2 - p0 +
                                (t * ((2.0 * p0) - (5.0 * p1) + (4.0 * p2) - p3 +
                                         (t * ((3.0 * (p1 - p2)) + p3 - p0))))));
        }

    }  // namespace

    LiquidMesh::LiquidMesh(MaterialParams params, std::uint64_t seed) noexcept
        : m_params(normalizeParams(params)),
          m_shearDamping(kLiquidShearDampingMin + (kLiquidShearDampingRange * m_params.viscosity)),
          m_rng(seed) {
        rollCharacter();
    }

    double LiquidMesh::drawUnit() noexcept {
        // Uniform in [-1, 1) from the top 53 bits of m_rng.
        return (2.0 * unit_draw(m_rng)) - 1.0;
    }

    void LiquidMesh::rollCharacter() noexcept {
        // Nonfinite variation is rejected before integration; draw neutral so the gains stay finite
        // meanwhile.
        const double AMOUNT = std::isfinite(m_params.variation) ? m_params.variation : 0.0;
        // Always draw every factor, so the stream position never depends on the variation amount.
        std::array<double, 9> draws{};
        for (double &value : draws) {
            value = AMOUNT * drawUnit();
        }

        const double OMEGA =
            kLiquidBaseOmega * std::exp2(m_params.elasticity - 0.5 + (kVaryOmegaOctaves * draws[0]));
        double zeta = dampingRatio(m_params.momentum);
        if (zeta < 1.0) {
            zeta = std::clamp(zeta * (1.0 + (kVaryDamping * draws[1])), kMinDampingRatio, 1.0);
        }
        m_bodyStiffness = OMEGA * OMEGA;
        m_bodyDamping = 2.0 * zeta * OMEGA;
        m_headStiffness = kLiquidHeadStiffnessScale * m_bodyStiffness;
        // Head is always critically damped, so its damping is 2 * sqrt(stiffness).
        m_headDamping = 2.0 * std::sqrt(m_headStiffness);

        m_lagGain = 1.0 + (kVaryLag * draws[2]);
        m_bowDepth = kLiquidBowDepth + (kVaryBowDepth * draws[3]);
        m_taperRatio = kLiquidTaperRatio * (1.0 + (kVaryTaper * draws[4]));
        m_skew = kVarySkew * draws[5];
        m_lean = kVaryLean * draws[6];
        m_kickGain = 1.0 + (kVaryKick * draws[7]);
        m_velocitySeconds = kLiquidVelocitySeconds * (1.0 + (kVaryVelocitySeconds * draws[8]));
    }

    void LiquidMesh::reset() noexcept {
        m_displacements.fill(Vec2{});
        m_velocities.fill(Vec2{});
        m_grip.fill(0.0);
        m_pendingDelta = {};
        m_windowVelocity = {};
        m_smoothDelta = {};
        m_smoothTime = 0.0;
        m_inertia = {};
        m_slowDelta = {};
        m_slowTime = 0.0;
        m_slowVelocity = {};
        m_strength = 0.0;
        m_accumulator = 0.0;
        m_quietSteps = 0;
    }

    void LiquidMesh::recordTranslation(Vec2 delta, Vec2 size, double strength) noexcept {
        // A nonfinite Liquid parameter has no defined response, so the field must not be excited.
        if (!isFinite(delta) || !isValidSize(size) || !std::isfinite(strength) || strength <= 0.0 ||
            !hasFiniteParams(m_params)) {
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
                rollCharacter();
            }
            m_pendingDelta.x += delta.x;
            m_pendingDelta.y += delta.y;
            m_strength = strength;
            m_quietSteps = 0;
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
    void LiquidMesh::integrateLiquidSubstep(Vec2 size) noexcept {
        Field &displacements = m_displacements;
        Field &velocities = m_velocities;
        Field &accelerations = m_accelerations;
        const Field &field = displacements;
        Scalars &div = m_divergence;

        const Vec2 SPACING{.x = size.x / kCells, .y = size.y / kCells};
        // Scale each axis's shear so the tighter axis keeps the index-space stiffness and
        // the looser axis is weakened in proportion, giving comparable physical coupling.
        const double TIGHTER = std::min(SPACING.x, SPACING.y);
        const double WEIGHT_X = (TIGHTER / SPACING.x) * (TIGHTER / SPACING.x);
        const double WEIGHT_Y = (TIGHTER / SPACING.y) * (TIGHTER / SPACING.y);
        const double WAVE_SPEED = kLiquidWaveSpeedScale * std::min(size.x, size.y);
        const double WAVE_SPEED2 = WAVE_SPEED * WAVE_SPEED;

        for (unsigned divRow = 0; divRow < kSide; ++divRow) {
            for (unsigned divCol = 0; divCol < kSide; ++divCol) {
                const double SLOPE_X =
                    lineSlope([&](unsigned k) { return field[gridIndex(k, divRow)].x; }, divCol, SPACING.x);
                const double SLOPE_Y =
                    lineSlope([&](unsigned k) { return field[gridIndex(divCol, k)].y; }, divRow, SPACING.y);
                div[gridIndex(divCol, divRow)] = SLOPE_X + SLOPE_Y;
            }
        }

        // Lag magnitude in px: quadratic in speed for slow drags, about linear for fast ones.
        const double SPEED = std::hypot(m_windowVelocity.x, m_windowVelocity.y);
        const double LAG = SPEED > 0.0 ? kLiquidLagSeconds * m_lagGain * m_strength * SPEED *
                                             SPEED / (SPEED + kLiquidLagSpeed)
                                       : 0.0;
        // With no motion the lag is zero, so the direction is unused and any unit vector serves.
        const double DIR_X = SPEED > 0.0 ? m_windowVelocity.x / SPEED : 1.0;
        const double DIR_Y = SPEED > 0.0 ? m_windowVelocity.y / SPEED : 0.0;
        const double PERP_X = -DIR_Y;
        const double PERP_Y = DIR_X;
        // A unit direction has |dx| + |dy| >= 1, so this normalizer is always positive.
        const double NORMALIZER = 0.5 * (std::abs(DIR_X) + std::abs(DIR_Y));
        const double GRIP_BLEND = 1.0 - std::exp(-SUBSTEP_SECONDS / kLiquidGripRelaxSeconds);
        const double SHEAR_DAMPING = m_shearDamping;
        // The bow's deepest point sits at s = m_skew; dividing by 1 + |skew| keeps the far corner's
        // lag at (1 - bow depth) of the deepest point, so a skewed bow never turns a trailing
        // corner forward.
        const double SKEW_SCALE = 1.0 / (1.0 + std::abs(m_skew));
        // Landing: smoothed deceleration is a large share of speed, so the front releases at once
        // instead of waiting for the smoothed speed to fall through the quiet threshold.
        const bool LANDING = std::hypot(m_inertia.x, m_inertia.y) > kLiquidGripLandingRate * SPEED;

        for (unsigned row = 0; row < kSide; ++row) {
            for (unsigned col = 0; col < kSide; ++col) {
                const std::size_t INDEX = gridIndex(col, row);
                // Lattice position relative to the centre, in [-0.5, 0.5].
                const double LATTICE_X = (static_cast<double>(col) / kCells) - 0.5;
                const double LATTICE_Y = (static_cast<double>(row) / kCells) - 0.5;
                // q: -1 at the trailing edge, +1 at the leading face. s: -1..1 across the motion.
                const double ALONG_MOTION = std::clamp(((LATTICE_X * DIR_X) + (LATTICE_Y * DIR_Y)) / NORMALIZER, -1.0, 1.0);
                const double ACROSS_MOTION = std::clamp(((LATTICE_X * PERP_X) + (LATTICE_Y * PERP_Y)) / NORMALIZER, -1.0, 1.0);
                // Grip relaxes toward head grip while moving and toward zero when still, so a stop
                // releases the front.
                m_grip[INDEX] += ((SPEED > kLiquidGripQuietSpeed && !LANDING ? headGrip(ALONG_MOTION) : 0.0) -
                                     m_grip[INDEX]) *
                                 GRIP_BLEND;
                const double GRIP = m_grip[INDEX];
                const double STIFFNESS =
                    (m_bodyStiffness * (1.0 - GRIP)) + (m_headStiffness * GRIP);
                const double LEAD = std::max(ALONG_MOTION, 0.0);
                const double TRAIL = std::max(-ALONG_MOTION, 0.0);
                // Inertia weights: the leading face carries the push forward and the trailing edge
                // a weaker share.
                const double KICK_WEIGHT =
                    kLiquidStopKick * m_kickGain * m_params.momentum *
                    ((LEAD * LEAD) + (kLiquidStopTrailRatio * TRAIL * TRAIL));
                const double DAMPING = (m_bodyDamping * (1.0 - GRIP)) + (m_headDamping * GRIP);

                // t: 1 at the trailing edge, 0 at the leading face. The trailing edge bows back at
                // its middle and the trailing corners taper cross-axis; both follow t^2 so the side
                // edges curve toward the tail. The character skews the bow off-centre and leans the
                // tail sideways (a shear along perp).
                const double TAIL_BLEND = 0.5 * (1.0 - ALONG_MOTION);
                const double TAIL_BLEND_SQUARED = TAIL_BLEND * TAIL_BLEND;
                const double OFFSET = (ACROSS_MOTION - m_skew) * SKEW_SCALE;
                const double BOW = TAIL_BLEND_SQUARED * (1.0 - (m_bowDepth * OFFSET * OFFSET));
                const double TAPER = LAG * TAIL_BLEND_SQUARED * ((m_taperRatio * ACROSS_MOTION) + m_lean);
                const Vec2 LAG_FIELD{.x = -((LAG * BOW * DIR_X) + (TAPER * PERP_X)),
                    .y = -((LAG * BOW * DIR_Y) + (TAPER * PERP_Y))};

                const double GRAD_X =
                    lineSlope([&](unsigned k) { return div[gridIndex(k, row)]; }, col, SPACING.x);
                const double GRAD_Y =
                    lineSlope([&](unsigned k) { return div[gridIndex(col, k)]; }, row, SPACING.y);

                const double COUPLED_X =
                    (kLiquidShearStiffness * shearLaplacian(field, &Vec2::x, col, row, WEIGHT_X, WEIGHT_Y)) +
                    (SHEAR_DAMPING * shearLaplacian(velocities, &Vec2::x, col, row, WEIGHT_X, WEIGHT_Y)) +
                    (WAVE_SPEED2 * GRAD_X);
                const double COUPLED_Y =
                    (kLiquidShearStiffness * shearLaplacian(field, &Vec2::y, col, row, WEIGHT_X, WEIGHT_Y)) +
                    (SHEAR_DAMPING * shearLaplacian(velocities, &Vec2::y, col, row, WEIGHT_X, WEIGHT_Y)) +
                    (WAVE_SPEED2 * GRAD_Y);

                accelerations[INDEX] = {.x = (-STIFFNESS * (field[INDEX].x - LAG_FIELD.x)) -
                                             (DAMPING * velocities[INDEX].x) + COUPLED_X +
                                             (m_inertia.x * KICK_WEIGHT),
                    .y = (-STIFFNESS * (field[INDEX].y - LAG_FIELD.y)) -
                         (DAMPING * velocities[INDEX].y) + COUPLED_Y + (m_inertia.y * KICK_WEIGHT)};
            }
        }
        // The pseudo-force decays exponentially across each tick, so its shape does not depend on
        // the tick length.
        const double INERTIA_DECAY = std::exp(-SUBSTEP_SECONDS / kLiquidInertiaSeconds);
        m_inertia = {.x = m_inertia.x * INERTIA_DECAY, .y = m_inertia.y * INERTIA_DECAY};

        for (std::size_t i = 0; i < displacements.size(); ++i) {
            velocities[i].x += accelerations[i].x * SUBSTEP_SECONDS;
            velocities[i].y += accelerations[i].y * SUBSTEP_SECONDS;
            displacements[i].x += velocities[i].x * SUBSTEP_SECONDS;
            displacements[i].y += velocities[i].y * SUBSTEP_SECONDS;
        }
    }

    bool LiquidMesh::advance(double elapsedSeconds, Vec2 size) noexcept {
        if (!isValidSize(size) || !std::isfinite(elapsedSeconds) ||
            elapsedSeconds > STALL_SECONDS || !hasFiniteParams(m_params)) {
            reset();
            return false;
        }

        // Window velocity is the ratio of exponentially weighted translation to weighted time, so
        // irregular or zero-delta frames average into the true speed instead of reading as a stop.
        // Its change between ticks gives the inertia pseudo-force: only deceleration or reversal
        // along the previous motion is carried forward.
        if (elapsedSeconds > 0.0) {
            const double DECAY = std::exp(-elapsedSeconds / m_velocitySeconds);
            m_smoothDelta = {.x = (m_smoothDelta.x * DECAY) + m_pendingDelta.x,
                .y = (m_smoothDelta.y * DECAY) + m_pendingDelta.y};
            m_smoothTime = (m_smoothTime * DECAY) + elapsedSeconds;
            Vec2 velocity{.x = m_smoothDelta.x / m_smoothTime, .y = m_smoothDelta.y / m_smoothTime};
            if (!isFinite(velocity)) {
                reset();
                return false;
            }
            const double SPEED = std::hypot(velocity.x, velocity.y);
            const double CAP = kLiquidMaxSpeedScale * std::min(size.x, size.y);
            if (SPEED > CAP) {
                velocity.x *= CAP / SPEED;
                velocity.y *= CAP / SPEED;
            }
            // Slower velocity for the inertia: averaging over zero-delta frames keeps lumpy
            // arrivals from reading as deceleration, so only a real stop or reversal pushes the
            // leading face.
            const double SLOW_DECAY = std::exp(-elapsedSeconds / kLiquidInertiaSeconds);
            m_slowDelta = {.x = (m_slowDelta.x * SLOW_DECAY) + m_pendingDelta.x,
                .y = (m_slowDelta.y * SLOW_DECAY) + m_pendingDelta.y};
            m_slowTime = (m_slowTime * SLOW_DECAY) + elapsedSeconds;
            const Vec2 SLOW_VELOCITY{
                .x = m_slowDelta.x / m_slowTime, .y = m_slowDelta.y / m_slowTime};
            if (!isFinite(SLOW_VELOCITY)) {
                reset();
                return false;
            }
            // Raw deceleration along the previous slow motion; zero on acceleration or steady drag.
            Vec2 rawDecel{};
            const double PREVIOUS_SLOW_SPEED = std::hypot(m_slowVelocity.x, m_slowVelocity.y);
            if (PREVIOUS_SLOW_SPEED > 0.0) {
                const double DIR_X = m_slowVelocity.x / PREVIOUS_SLOW_SPEED;
                const double DIR_Y = m_slowVelocity.y / PREVIOUS_SLOW_SPEED;
                const double DVX = (SLOW_VELOCITY.x - m_slowVelocity.x) / elapsedSeconds;
                const double DVY = (SLOW_VELOCITY.y - m_slowVelocity.y) / elapsedSeconds;
                const double DECEL = -((DVX * DIR_X) + (DVY * DIR_Y));
                if (DECEL > 0.0) {
                    rawDecel = {.x = DIR_X * DECEL, .y = DIR_Y * DECEL};
                }
            }
            m_slowVelocity = SLOW_VELOCITY;
            // Tick-start rate of an exponential pseudo-force whose integral over this tick equals
            // the velocity change: R0 * tau * (1 - exp(-dt/tau)) = rawDecel * dt. Substeps decay
            // it, so cadence does not change the push.
            const double TICK_SHAPE = -std::expm1(-elapsedSeconds / kLiquidInertiaSeconds);
            const double TICK_GAIN = elapsedSeconds / (kLiquidInertiaSeconds * TICK_SHAPE);
            m_inertia = {.x = rawDecel.x * TICK_GAIN, .y = rawDecel.y * TICK_GAIN};
            m_windowVelocity = velocity;
            m_pendingDelta = {};
        }

        // Applies the current geometry's bounds even when no substep runs.
        if (!constrain(size)) {
            return false;
        }

        if (settled()) {
            m_accumulator = 0.0;
            m_quietSteps = 0;
            return false;
        }

        m_accumulator += std::max(0.0, std::min(elapsedSeconds, kMaxFrameSeconds));

        unsigned steps = 0;
        while (m_accumulator >= SUBSTEP_SECONDS - kAccumulatorTolerance) {
            if (steps == kMaxSubstepsPerAdvance) {
                m_accumulator = 0.0;
                break;
            }
            m_accumulator -= SUBSTEP_SECONDS;
            ++steps;

            integrateLiquidSubstep(size);

            if (!constrain(size)) {
                return false;
            }

            const bool QUIET = isQuiet(m_displacements, m_velocities);
            if (QUIET) {
                if (++m_quietSteps >= kQuietSubsteps) {
                    reset();
                    return false;
                }
            } else {
                m_quietSteps = 0;
            }
        }

        return !settled();
    }

    bool LiquidMesh::settled() const noexcept {
        if (!isZero(m_displacements) || !isZero(m_velocities)) {
            return false;
        }
        return isZero(m_windowVelocity) && isZero(m_pendingDelta);
    }

    Vec2 LiquidMesh::sampleDisplacement(double u, double v) const noexcept {
        if (!std::isfinite(u) || !std::isfinite(v)) {
            return {};
        }

        const double GRID_X = std::clamp(u, 0.0, 1.0) * kCells;
        const double GRID_Y = std::clamp(v, 0.0, 1.0) * kCells;
        const int CELLS = static_cast<int>(kCells);
        const int COL = std::min(static_cast<int>(std::floor(GRID_X)), CELLS - 1);
        const int ROW = std::min(static_cast<int>(std::floor(GRID_Y)), CELLS - 1);
        const double FRACTION_X = GRID_X - COL;
        const double FRACTION_Y = GRID_Y - ROW;

        // Node values with edge clamping, so the spline reproduces the boundary field.
        const auto NODE = [&](int i, int j) -> Vec2 {
            const unsigned CLAMPED_COL = static_cast<unsigned>(std::clamp(i, 0, CELLS));
            const unsigned CLAMPED_ROW = static_cast<unsigned>(std::clamp(j, 0, CELLS));
            return m_displacements[gridIndex(CLAMPED_COL, CLAMPED_ROW)];
        };

        // Interpolate along x for the four rows around the sample, then along y.
        std::array<Vec2, 4> column{};
        for (int k = 0; k < 4; ++k) {
            const int ROW_INDEX = ROW - 1 + k;
            const Vec2 SAMPLE_BACK = NODE(COL - 1, ROW_INDEX);
            const Vec2 SAMPLE_LEFT = NODE(COL, ROW_INDEX);
            const Vec2 SAMPLE_RIGHT = NODE(COL + 1, ROW_INDEX);
            const Vec2 SAMPLE_NEXT = NODE(COL + 2, ROW_INDEX);
            column[k] = {.x = catmullRom(SAMPLE_BACK.x, SAMPLE_LEFT.x, SAMPLE_RIGHT.x, SAMPLE_NEXT.x, FRACTION_X),
                .y = catmullRom(SAMPLE_BACK.y, SAMPLE_LEFT.y, SAMPLE_RIGHT.y, SAMPLE_NEXT.y, FRACTION_X)};
        }
        return {.x = catmullRom(column[0].x, column[1].x, column[2].x, column[3].x, FRACTION_Y),
            .y = catmullRom(column[0].y, column[1].y, column[2].y, column[3].y, FRACTION_Y)};
    }

    bool LiquidMesh::constrain(Vec2 size) noexcept {
        for (std::size_t i = 0; i < m_displacements.size(); ++i) {
            if (!isFinite(m_displacements[i]) || !isFinite(m_velocities[i])) {
                reset();
                return false;
            }
        }

        const double LIMIT = displacementLimit(size);
        for (std::size_t i = 0; i < m_displacements.size(); ++i) {
            Vec2 &displacement = m_displacements[i];
            Vec2 &velocity = m_velocities[i];
            const double MAGNITUDE = std::hypot(displacement.x, displacement.y);
            if (MAGNITUDE > 0.0) {
                softSaturate(displacement, velocity, MAGNITUDE, LIMIT);
            }
        }

        for (unsigned halvings = 0;; ++halvings) {
            if (validTriangles(size)) {
                return true;
            }
            if (halvings == kMaxHalvings) {
                break;
            }
            for (std::size_t i = 0; i < m_displacements.size(); ++i) {
                m_displacements[i] = {
                    .x = m_displacements[i].x * 0.5, .y = m_displacements[i].y * 0.5};
                m_velocities[i] = {.x = m_velocities[i].x * 0.5, .y = m_velocities[i].y * 0.5};
            }
        }

        reset();
        return false;
    }

    bool LiquidMesh::validTriangles(Vec2 size) const noexcept {
        for (unsigned cy = 0; cy < kCells; ++cy) {
            for (unsigned cx = 0; cx < kCells; ++cx) {
                for (unsigned which = 0; which < 2; ++which) {
                    const auto CORNERS = triangleCorners(cx, cy, which);
                    std::array<Vec2, 3> rest{};
                    std::array<Vec2, 3> deformed{};
                    for (std::size_t k = 0; k < 3; ++k) {
                        rest[k] = restPosition(CORNERS[k], size);
                        const Vec2 DISPLACEMENT =
                            m_displacements[gridIndex(CORNERS[k].x, CORNERS[k].y)];
                        deformed[k] = {
                            .x = rest[k].x + DISPLACEMENT.x, .y = rest[k].y + DISPLACEMENT.y};
                    }

                    // Rest orientation is the same sign for all triangles, so the ratio is positive
                    // while orientation is kept and negative or zero after a foldover or collapse.
                    const double REST_AREA = signedArea(rest[0], rest[1], rest[2]);
                    if (!std::isfinite(REST_AREA) || REST_AREA == 0.0) {
                        return false;
                    }
                    const double AREA = signedArea(deformed[0], deformed[1], deformed[2]);
                    if (!(AREA / REST_AREA >= kMinAreaRatio)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

}  // namespace hyprimate
