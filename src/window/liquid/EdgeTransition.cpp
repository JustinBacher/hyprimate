#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <span>

#include "shared/Core.hpp"
#include "window/liquid/EdgeTransition.hpp"
#include "window/shared/EdgeFrame.hpp"

namespace hyprimate {
    namespace {

        constexpr double ACCUMULATOR_TOLERANCE = 1e-9;
        constexpr double REFERENCE_STRENGTH = 0.65;
        // Bead.
        constexpr double RADIUS_PER_SIDE = 0.14, RADIUS_MIN = 20.0, RADIUS_MAX = 80.0;
        constexpr double RADIUS_FIT = 0.95;  // r <= this fraction of the capture half-extents
        constexpr double HOVER_PER_PINCH = 1.25;  // nh = r + HOVER_PER_PINCH * Lp
        // Hop: the bead centre rises from behind the edge to its hover point.
        constexpr double HOP_OMEGA =
            22.0;  // rad/s at elasticity 0.5, scaled by 2^(elasticity - 0.5)
        constexpr double HOP_ZETA_STIFF = 0.8, HOP_ZETA_LOOSE = 0.45;  // momentum 0 / 1
        // Grow: the spreads take the bead to the window's extents (tangential first, then normal).
        constexpr double GROW_OMEGA = 15.0;  // rad/s at elasticity 0.5
        constexpr double GROW_ZETA_STIFF = 0.85, GROW_ZETA_LOOSE = 0.5;  // momentum 0 / 1
        constexpr double GROW_NORMAL_RATIO = 0.75;
        // Black edge liquid, scaled with the bead.
        constexpr double TUCK_PER_RADIUS = 0.35, TUCK_MIN = 5.0, TUCK_MAX = 20.0;
        constexpr double PINCH_PER_RADIUS = 1.1, PINCH_MIN = 16.0, PINCH_MAX = 72.0;
        constexpr double NECK_PER_RADIUS = 0.9, NECK_MIN = 12.0, NECK_MAX = 64.0;
        constexpr double CROSS_SKIRT_PER_TUCK = 2.5;
        // Recoil after the snap.
        constexpr double RECOIL_OMEGA = 28.0;
        constexpr double RECOIL_ZETA_STIFF = 0.6, RECOIL_ZETA_LOOSE = 0.2;
        constexpr double RECOIL_SKIRT_GROWTH = 0.6, RECOIL_SKIRT_SECONDS = 0.06,
                         RECOIL_PLATEAU_SECONDS = 0.03;
        // Snap fallbacks: hop at apex or at rest.
        constexpr double REST_HOP_PIXELS = 0.5, REST_HOP_PIXELS_PER_SECOND = 5.0;
        // Close.
        constexpr double CLOSE_SECONDS = 0.46;
        constexpr double SHRINK_END = 0.45;  // normal shrink ends here (fraction of the close)
        constexpr double SHRINK_TANGENT_START = 0.05, SHRINK_TANGENT_SPAN = 0.5;
        constexpr double DIVE_MARGIN =
            2.0;  // px beyond the bead's far side when the close completes
        constexpr double SPLASH_SECONDS =
            0.42;  // at elasticity 0.5; scaled by 2^(0.5 - elasticity)
        constexpr double SPLASH_PEAK_X = 0.22;  // impulse peak in normalised time
        constexpr double SPLASH_PER_RADIUS = 0.9;  // height at the peak, per radius
        constexpr double SPLASH_SKIRT = 0.55;  // skirt half-width per radius
        // Settling thresholds.
        constexpr double QUIET_PIXELS = 0.15, QUIET_PIXELS_PER_SECOND = 2.0;
        constexpr double QUIET_SPREAD = 1e-4,
                         QUIET_SPREAD_PER_SECOND =
                             1e-3;  // spreads are unitless; ~0.1 px on a 1000 px half-extent

        double lerp(double start, double end, double fraction) {
            return start + ((end - start) * fraction);
        }

        double ease_in_out_cubic(double progress) {
            progress = std::clamp(progress, 0.0, 1.0);
            return progress < 0.5 ? 4.0 * progress * progress * progress
                                  : 1.0 - (std::pow((-2.0 * progress) + 2.0, 3.0) / 2.0);
        }

        // Splash impulse in normalised time x: peaks at 1 when x = SPLASH_PEAK_X, is 0 outside (0,
        // 1) and ends at 0 at x = 1.
        double splash_impulse(double progress) {
            if (progress <= 0.0 || progress >= 1.0) {
                return 0.0;
            }
            const double PEAK_RATIO = progress / SPLASH_PEAK_X;
            return PEAK_RATIO * std::exp(1.0 - PEAK_RATIO) * (1.0 - smoothstep(0.7, 1.0, progress));
        }

        // Semi-implicit Euler toward `target`. Stable for omega * dt < 2; the largest omega used
        // (recoil, ~40 rad/s) gives omega * dt ~ 0.17 at 240 Hz substeps.
        template <typename S>
        void spring(S &state, double target, double omega, double zeta) {
            const double ACCEL =
                (-omega * omega * (state.value - target)) - (2.0 * zeta * omega * state.velocity);
            state.velocity += ACCEL * SUBSTEP_SECONDS;
            state.value += state.velocity * SUBSTEP_SECONDS;
        }

        bool valid_frame(const EdgeFrame &frame) {
            const std::array VALUES = {
                frame.edge_distance,
                frame.capture_far,
                frame.body_near,
                frame.body_far,
                frame.body_t0,
                frame.body_t1,
            };
            for (const double VALUE : VALUES) {
                if (!std::isfinite(VALUE)) {
                    return false;
                }
            }
            return frame.edge_distance > 0.0 && frame.capture_far > frame.edge_distance &&
                   frame.body_near >= 0.0 && frame.body_far > frame.body_near &&
                   frame.body_t1 > frame.body_t0;
        }

        // Radially homogeneous outline map in units of the capture half-extents. The rectangle
        // outline (max(|x|,|y|) = 1) is carried onto the superellipse of exponent n = 2 /
        // roundness; every ray from the centre maps onto itself, so the image cannot fold. `inv_n`
        // is 1/n. Callers pass the rectangle through unchanged (roundness below 2e-4).
        Vec2 outline(Vec2 point, double n, double inv_n) {
            const double ABS_X = std::abs(point.x);
            const double ABS_Y = std::abs(point.y);
            const double RHO = std::max(ABS_X, ABS_Y);
            if (!(RHO > 0.0)) {
                return point;
            }
            const double DIR_X = ABS_X / RHO;
            const double DIR_Y = ABS_Y / RHO;  // direction on the rectangle outline, max = 1
            const double OUTLINE_SCALE =
                std::pow(std::pow(DIR_X, n) + std::pow(DIR_Y, n), inv_n);  // in [1, 2^(1/n)]
            return {.x = point.x / OUTLINE_SCALE, .y = point.y / OUTLINE_SCALE};
        }

    }  // namespace

    void EdgeTransition::begin(Transition kind,
        const EdgeFrame &frame,
        const TransitionParams &params,
        double strength,
        bool close_splash) noexcept {
        const bool CONTINUING = kind == Transition::Close && m_phase == Phase::Opening;
        const EdgeTransition PREVIOUS = *this;
        reset();
        const EdgeFrame &chosen = CONTINUING ? PREVIOUS.m_frame : frame;
        if (!valid_frame(chosen) || !std::isfinite(params.elasticity) ||
            !std::isfinite(params.momentum) || !std::isfinite(strength) || strength <= 0.0 ||
            (params.duration_ms != -1.0 &&
                (!std::isfinite(params.duration_ms) || params.duration_ms <= 0.0))) {
            return;
        }

        m_frame = chosen;
        const double ELASTICITY = std::clamp(params.elasticity, 0.0, 1.0);
        const double MOMENTUM = std::clamp(params.momentum, 0.0, 1.0);
        const double STRENGTH_SCALE = std::clamp(strength / REFERENCE_STRENGTH, 0.25, 1.5);
        const double WIDTH = std::min(
            m_frame.body_t1 - m_frame.body_t0, std::numeric_limits<double>::max());
        const double DEPTH = m_frame.body_far - m_frame.body_near;
        m_half_n = m_frame.capture_far - m_frame.edge_distance;
        // The capture contains the body, so its tangential half-extent is at least width / 2; using
        // that bound keeps the bead inside both capture half-extents without needing the capture
        // size here.
        const double FIT = RADIUS_FIT * std::min(m_half_n, 0.5 * WIDTH);
        m_radius =
            std::min(std::clamp(RADIUS_PER_SIDE * std::min(WIDTH, DEPTH), RADIUS_MIN, RADIUS_MAX) *
                         STRENGTH_SCALE,
                FIT);
        m_tuck = std::clamp(TUCK_PER_RADIUS * m_radius, TUCK_MIN, TUCK_MAX);
        m_pinch = std::clamp(PINCH_PER_RADIUS * m_radius, PINCH_MIN, PINCH_MAX);
        m_neck_skirt = std::clamp(NECK_PER_RADIUS * m_radius, NECK_MIN, NECK_MAX);
        m_cross_skirt = CROSS_SKIRT_PER_TUCK * m_tuck;
        m_hover = m_radius + (HOVER_PER_PINCH * m_pinch);
        m_recoil_omega = RECOIL_OMEGA * std::exp2(ELASTICITY - 0.5);
        m_recoil_zeta = lerp(RECOIL_ZETA_STIFF, RECOIL_ZETA_LOOSE, MOMENTUM);
        m_grow_omega = GROW_OMEGA * std::exp2(ELASTICITY - 0.5);
        m_grow_zeta = lerp(GROW_ZETA_STIFF, GROW_ZETA_LOOSE, MOMENTUM);

        if (kind == Transition::Open) {
            m_hop_omega = HOP_OMEGA * std::exp2(ELASTICITY - 0.5);
            m_hop_zeta = lerp(HOP_ZETA_STIFF, HOP_ZETA_LOOSE, MOMENTUM);
            m_hop = {.value = -m_radius, .velocity = 0.0};  // bead fully behind the edge
            m_nc = -m_radius;
            m_phase = Phase::Opening;
        } else {
            m_duration = CLOSE_SECONDS * std::exp2(0.5 - ELASTICITY);
            m_close_momentum = MOMENTUM;
            m_close_splash = close_splash;
            m_splash_duration = SPLASH_SECONDS * std::exp2(0.5 - ELASTICITY);
            m_nc0 = CONTINUING ? PREVIOUS.m_nc : m_frame.edge_distance;
            m_s_t0 = CONTINUING ? PREVIOUS.m_spread_t.value : 1.0;
            m_s_n0 = CONTINUING ? PREVIOUS.m_spread_n.value : 1.0;
            m_spread_t = {.value = m_s_t0, .velocity = 0.0};
            m_spread_n = {.value = m_s_n0, .velocity = 0.0};
            m_nc = m_nc0;
            if (CONTINUING && PREVIOUS.m_recoiling) {
                m_recoiling = true;
                m_recoil = PREVIOUS.m_recoil;
                m_snap = PREVIOUS.m_snap;
                m_recoil_time = PREVIOUS.m_recoil_time;
            } else if (CONTINUING && PREVIOUS.m_profile.peak() > 0.0) {
                // Visible open liquid (neck or fillets) snaps into a recoil from its current
                // profile.
                start_recoil(std::get<0>(PREVIOUS.m_profile.lobes));
            }
            m_phase = Phase::Closing;
        }
        m_taken = m_nc;
        refresh_profile();
        if (params.duration_ms > 0.0) {
            // The solver's endpoint depends on settling, and may include an inherited recoil.
            // Count the fixed steps once, then play that exact trajectory on the requested clock.
            EdgeTransition probe = *this;
            do {
                probe.step_and_refresh();
                ++m_total_steps;
            } while (probe.active());
            m_requested_ms = params.duration_ms;
        }
    }

    void EdgeTransition::reset() noexcept {
        *this = EdgeTransition{};
    }

    void EdgeTransition::abort() noexcept {
        const bool CLOSING = m_phase == Phase::Closing;
        reset();
        if (CLOSING) {
            m_phase = Phase::Closed;
        }
    }

    bool EdgeTransition::active() const noexcept {
        return m_phase == Phase::Opening || m_phase == Phase::Closing;
    }
    bool EdgeTransition::closed() const noexcept {
        return m_phase == Phase::Closed;
    }

    Vec2 EdgeTransition::bead_centre() const noexcept {
        if (!active()) {
            return {.x = 0.0, .y = 0.0};
        }
        const bool HORIZONTAL = runs_horizontally(m_frame.edge);
        const Vec2 INWARD = inward_normal(m_frame.edge);
        const Vec2 ALONG = HORIZONTAL ? Vec2{.x = 1.0, .y = 0.0} : Vec2{.x = 0.0, .y = 1.0};
        const double TANGENT_CENTRE =
            std::midpoint(m_frame.body_t0, m_frame.body_t1) * (1.0 - m_spread_t.value);
        const double NORMAL_CENTRE = m_nc - m_frame.edge_distance;
        return {
            .x = (ALONG.x * TANGENT_CENTRE) + (INWARD.x * NORMAL_CENTRE),
            .y = (ALONG.y * TANGENT_CENTRE) + (INWARD.y * NORMAL_CENTRE),
        };
    }

    Vec2 EdgeTransition::bead_half(Vec2 rest_size) const noexcept {
        const bool HORIZONTAL = runs_horizontally(m_frame.edge);
        const double HALF_T = 0.5 * (HORIZONTAL ? rest_size.x : rest_size.y);
        const double HALF_N = 0.5 * (HORIZONTAL ? rest_size.y : rest_size.x);
        if (!active()) {
            return {.x = 0.5 * rest_size.x, .y = 0.5 * rest_size.y};
        }
        const double EXTENT_T = m_radius + ((HALF_T - m_radius) * m_spread_t.value);
        const double EXTENT_N = m_radius + ((HALF_N - m_radius) * m_spread_n.value);
        return HORIZONTAL ? Vec2{.x = EXTENT_T, .y = EXTENT_N} : Vec2{.x = EXTENT_N, .y = EXTENT_T};
    }

    EdgeTransition::RestMap EdgeTransition::rest_map(Vec2 size) const noexcept {
        RestMap mapping;
        if (!active() || !std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0.0 ||
            size.y <= 0.0) {
            return mapping;
        }
        mapping.identity = false;
        const bool HORIZONTAL = runs_horizontally(m_frame.edge);
        mapping.horizontal = HORIZONTAL;
        mapping.along = HORIZONTAL ? Vec2{.x = 1.0, .y = 0.0} : Vec2{.x = 0.0, .y = 1.0};
        mapping.inward = inward_normal(m_frame.edge);
        mapping.half_t = 0.5 * (HORIZONTAL ? size.x : size.y);
        mapping.half_n = 0.5 * (HORIZONTAL ? size.y : size.x);

        const double SPREAD_T = m_spread_t.value;
        const double SPREAD_N = m_spread_n.value;
        const double ROUNDNESS =
            std::pow(1.0 - std::clamp(std::min(SPREAD_T, SPREAD_N), 0.0, 1.0), 1.5);
        mapping.rectangle =
            !(ROUNDNESS >= 2e-4);  // exponent > 1e4 is indistinguishable from a rectangle
        if (!mapping.rectangle) {
            mapping.exponent = 2.0 / ROUNDNESS;
            mapping.inv_exponent = 1.0 / mapping.exponent;
        }

        const double CENTRE_T = std::midpoint(m_frame.body_t0, m_frame.body_t1);
        mapping.t_base = CENTRE_T * (1.0 - SPREAD_T);
        mapping.t_scale = m_radius + ((mapping.half_t - m_radius) * SPREAD_T);
        // The normal extent comes from `size`, like the tangential one: the frame's capture is
        // taken when the open begins, before decorations (shadow) report their extents, and a stale
        // extent leaves the window short of its rest box.
        mapping.n_base = m_nc - m_frame.edge_distance;
        mapping.n_scale = m_radius + ((mapping.half_n - m_radius) * SPREAD_N);
        return mapping;
    }

    Vec2 EdgeTransition::RestMap::map(Vec2 from_centre) const noexcept {
        if (identity || !std::isfinite(from_centre.x) || !std::isfinite(from_centre.y)) {
            return from_centre;
        }
        // Rest point in units of the capture half-extents.
        const double REST_T = (horizontal ? from_centre.x : from_centre.y) / half_t;
        const double REST_N = ((from_centre.x * inward.x) + (from_centre.y * inward.y)) / half_n;
        const Vec2 SHAPE = rectangle ? Vec2{.x = REST_T, .y = REST_N}
                                     : outline({.x = REST_T, .y = REST_N}, exponent, inv_exponent);
        const double T_OUT = t_base + (t_scale * SHAPE.x);
        const double N_OUT = n_base + (n_scale * SHAPE.y);
        return {
            .x = (along.x * T_OUT) + (inward.x * N_OUT),
            .y = (along.y * T_OUT) + (inward.y * N_OUT),
        };
    }

    Vec2 EdgeTransition::map(Vec2 from_centre, Vec2 size) const noexcept {
        return rest_map(size).map(from_centre);
    }

    Vec2 EdgeTransition::take_travel() noexcept {
        const double DELTA = m_phase != Phase::Idle
                                 ? m_nc - m_taken
                                 : 0.0;  // Closed keeps the final substep's travel
        m_taken = m_nc;
        const Vec2 INWARD = inward_normal(m_frame.edge);
        return {.x = INWARD.x * DELTA, .y = INWARD.y * DELTA};
    }

    double EdgeTransition::e_normal() const noexcept {
        return m_radius + ((m_half_n - m_radius) * m_spread_n.value);
    }

    double EdgeTransition::near_side() const noexcept {
        return m_nc - e_normal();
    }
    double EdgeTransition::far_side() const noexcept {
        return m_nc + e_normal();
    }

    bool EdgeTransition::advance(double elapsed_seconds) noexcept {
        if (!active()) {
            return false;
        }
        if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0 ||
            elapsed_seconds > STALL_SECONDS) {
            abort();
            return false;
        }
        if (m_requested_ms > 0.0) {
            m_elapsed_ms += elapsed_seconds * 1000.0;
            const int TARGET =
                m_elapsed_ms >= m_requested_ms
                    ? m_total_steps
                    : std::min(m_total_steps - 1,
                          static_cast<int>((m_elapsed_ms / m_requested_ms) * m_total_steps +
                                           ACCUMULATOR_TOLERANCE));
            // Finishing an opening resets the counters along with the rest of the transition.
            while (active() && m_elapsed_steps < TARGET) {
                ++m_elapsed_steps;
                step_and_refresh();
            }
            return active();
        }
        m_accumulator += elapsed_seconds;
        while (active() && m_accumulator >= SUBSTEP_SECONDS - ACCUMULATOR_TOLERANCE) {
            m_accumulator -= SUBSTEP_SECONDS;
            step_and_refresh();
        }
        return active();
    }

    void EdgeTransition::step_and_refresh() noexcept {
        step();
        refresh_profile();  // a snap starts from the profile one substep earlier
    }

    bool EdgeTransition::quiet() const noexcept {
        const bool HOP_QUIET = std::abs(m_hop.value - m_hover) < QUIET_PIXELS &&
                               std::abs(m_hop.velocity) < QUIET_PIXELS_PER_SECOND;
        const bool GROW_QUIET = std::abs(m_spread_t.value - 1.0) < QUIET_SPREAD &&
                                std::abs(m_spread_t.velocity) < QUIET_SPREAD_PER_SECOND &&
                                std::abs(m_spread_n.value - 1.0) < QUIET_SPREAD &&
                                std::abs(m_spread_n.velocity) < QUIET_SPREAD_PER_SECOND;
        const bool LIQUID_QUIET = m_recoiling && std::abs(m_recoil.value) < QUIET_PIXELS &&
                                  std::abs(m_recoil.velocity) < QUIET_PIXELS_PER_SECOND;
        return m_growing && HOP_QUIET && GROW_QUIET && LIQUID_QUIET;
    }

    void EdgeTransition::step() noexcept {
        if (m_phase == Phase::Opening) {
            const double BEFORE = m_hop.velocity;
            spring(m_hop, m_hover, m_hop_omega, m_hop_zeta);
            if (m_growing) {
                spring(m_spread_t, 1.0, m_grow_omega, m_grow_zeta);
                spring(m_spread_n, 1.0, m_grow_omega * GROW_NORMAL_RATIO, m_grow_zeta);
            }
            // The centre carries the normal spread's overshoot: it sits at the hop until the grow
            // starts.
            m_nc = m_hop.value + ((m_frame.edge_distance - m_hover) * m_spread_n.value);
            if (m_recoiling) {
                advance_recoil();
            } else if (near_side() >= 0.0) {
                const bool APEX = BEFORE > 0.0 && m_hop.velocity <= 0.0;
                const bool RESTING = std::abs(m_hop.value - m_hover) < REST_HOP_PIXELS &&
                                     std::abs(m_hop.velocity) < REST_HOP_PIXELS_PER_SECOND;
                if (near_side() >= m_pinch || APEX || RESTING) {
                    // m_profile still holds the attached profile from before this substep.
                    start_recoil(std::get<0>(m_profile.lobes));
                    m_growing = true;
                }
            }
            if (quiet()) {
                reset();
            }
            return;
        }

        if (!m_drained) {
            m_time += SUBSTEP_SECONDS;
            const double PROGRESS = std::min(m_time / m_duration, 1.0);
            // Shrink: the pill folds back into a circle, normal first, then tangential.
            m_spread_n.value = m_s_n0 * (1.0 - ease_in_out_cubic(PROGRESS / SHRINK_END));
            m_spread_t.value =
                m_s_t0 *
                (1.0 - ease_in_out_cubic((PROGRESS - SHRINK_TANGENT_START) / SHRINK_TANGENT_SPAN));
            // One continuous trajectory: shrinking must not stop the centre at a hover point.
            // Momentum advances the dive without introducing a second launch.
            const double SQUARE = PROGRESS * PROGRESS;
            const double CUBIC = SQUARE * PROGRESS;
            const double DIVE =
                m_close_momentum < 0.5
                    ? lerp(CUBIC, SQUARE, m_close_momentum * 2.0)
                    : lerp(SQUARE, (2.0 * SQUARE) - CUBIC, (m_close_momentum - 0.5) * 2.0);
            m_nc = lerp(m_nc0, -m_radius - DIVE_MARGIN, DIVE);
            if (m_recoiling) {
                advance_recoil();  // residual recoil from an interrupted opening keeps falling
            }
            if (m_time < m_duration - ACCUMULATOR_TOLERANCE) {
                return;
            }
            m_drained = true;
            m_splashing = m_close_splash;  // the splash starts with the next substep
        } else {
            if (m_recoiling) {
                advance_recoil();
            }
            if (m_splashing) {
                m_splash_time += SUBSTEP_SECONDS;
                m_splashing = m_splash_time < m_splash_duration - ACCUMULATOR_TOLERANCE;
            }
        }
        if (!m_splashing && recoil_settled()) {
            m_phase = Phase::Closed;
        }
    }

    EdgeTransition::Lobe EdgeTransition::live_lobe() const noexcept {
        const double NEAR_SIDE = near_side();
        if (NEAR_SIDE < 0.0) {
            // Crossing: fillets hug the bead where it passes through the edge. They ramp in as the
            // bead's far side breaks the surface.
            return {
                .height = m_tuck * smoothstep(0.0, 2.0 * m_tuck, far_side()),
                .plateau = m_radius,
                .skirt = m_cross_skirt,
            };
        }
        if (NEAR_SIDE <= m_pinch) {
            // Attached: the tip stays tucked under the near side while the contact patch necks
            // down.
            const double PINCH_BLEND = smoothstep(0.0, m_pinch, NEAR_SIDE);
            return {
                .height = NEAR_SIDE + m_tuck,
                .plateau = m_radius * (1.0 - PINCH_BLEND),
                .skirt = lerp(m_cross_skirt, m_neck_skirt, PINCH_BLEND),
            };
        }
        return {};
    }

    EdgeTransition::Lobe EdgeTransition::recoil_lobe() const noexcept {
        const double RECOIL_TIME = m_recoil_time;
        return {
            .height = std::max(0.0, m_recoil.value),
            .plateau = m_snap.plateau * std::exp(-RECOIL_TIME / RECOIL_PLATEAU_SECONDS),
            .skirt =
                m_snap.skirt * (1.0 + (RECOIL_SKIRT_GROWTH *
                                          (1.0 - std::exp(-RECOIL_TIME / RECOIL_SKIRT_SECONDS)))),
        };
    }

    EdgeTransition::Lobe EdgeTransition::splash_lobe() const noexcept {
        const double PROGRESS = m_splash_time / m_splash_duration;
        return {
            .height = SPLASH_PER_RADIUS * m_radius * splash_impulse(PROGRESS),
            .plateau = 0.0,
            .skirt = SPLASH_SKIRT * m_radius,
        };
    }

    void EdgeTransition::refresh_profile() noexcept {
        m_profile = {};
        if (!active()) {
            return;
        }
        // Open liquid (attached neck or crossing fillets) until the snap; the recoil after a snap
        // (or an interrupted open's residual during a close); the splash once a close has drained.
        if (m_phase == Phase::Opening && !m_recoiling) {
            m_profile.add(live_lobe());
        }
        if (m_recoiling) {
            m_profile.add(recoil_lobe());
        }
        if (m_phase == Phase::Closing && m_drained && m_splashing) {
            m_profile.add(splash_lobe());
        }
    }

    void EdgeTransition::start_recoil(const Lobe &snap) noexcept {
        m_snap = snap;
        m_recoil = {.value = snap.height, .velocity = 0.0};
        m_recoil_time = 0.0;
        m_recoiling = true;
    }

    bool EdgeTransition::recoil_settled() const noexcept {
        return !m_recoiling || (std::abs(m_recoil.value) < QUIET_PIXELS &&
                                   std::abs(m_recoil.velocity) < QUIET_PIXELS_PER_SECOND);
    }

    void EdgeTransition::advance_recoil() noexcept {
        m_recoil_time += SUBSTEP_SECONDS;
        spring(m_recoil, 0.0, m_recoil_omega, m_recoil_zeta);
    }

    double EdgeTransition::meniscus(double tangent) const noexcept {
        const double CENTRE = std::midpoint(m_frame.body_t0, m_frame.body_t1);
        double peak_height = 0.0;
        for (const Lobe &lobe : m_profile.lobes) {
            if (!(lobe.height > 0.0)) {
                continue;
            }
            const double SKIRT_DISTANCE = std::abs(tangent - CENTRE) - lobe.plateau;
            if (SKIRT_DISTANCE <= 0.0) {
                peak_height = std::max(peak_height, lobe.height);
                continue;
            }
            const double SKIRT_FRACTION = SKIRT_DISTANCE / lobe.skirt;
            if (SKIRT_FRACTION >= 1.0) {
                continue;
            }
            const double SKIRT_WEIGHT = 1.0 - (SKIRT_FRACTION * SKIRT_FRACTION);
            peak_height = std::max(peak_height,
                lobe.height * SKIRT_WEIGHT * SKIRT_WEIGHT);  // tangent to the edge at the skirt's
                                                             // foot, like the bar's concave flanks
        }
        return peak_height;
    }

    double EdgeTransition::meniscus_low() const noexcept {
        const double CENTRE = std::midpoint(m_frame.body_t0, m_frame.body_t1);
        double low = CENTRE;
        bool any = false;
        for (const Lobe &lobe : m_profile.lobes) {
            if (!(lobe.height > 0.0)) {
                continue;
            }
            const double LOW_EDGE = CENTRE - lobe.plateau - lobe.skirt;
            low = any ? std::min(low, LOW_EDGE) : LOW_EDGE;
            any = true;
        }
        return low;
    }

    double EdgeTransition::meniscus_high() const noexcept {
        const double CENTRE = std::midpoint(m_frame.body_t0, m_frame.body_t1);
        double high = CENTRE;
        bool any = false;
        for (const Lobe &lobe : m_profile.lobes) {
            if (!(lobe.height > 0.0)) {
                continue;
            }
            const double HIGH_EDGE = CENTRE + lobe.plateau + lobe.skirt;
            high = any ? std::max(high, HIGH_EDGE) : HIGH_EDGE;
            any = true;
        }
        return high;
    }

    double EdgeTransition::meniscus_peak() const noexcept {
        return m_profile.peak();
    }

}  // namespace hyprimate
