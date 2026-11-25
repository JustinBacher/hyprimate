#include <algorithm>
#include <cmath>

#include "window/smoke/Smoke.hpp"
#include "window/smoke/SmokeDetail.hpp"

namespace hyprimate {
    using namespace smoke_detail;
    namespace {
        // Time for wake smoke to finish dissipating once nothing feeds the fluid, as a share of
        // its lifetime: the age-driven thinning has cleared it by then.
        constexpr double RESIDUAL_LIFETIMES = 1.15;
        constexpr double CLOSE_RESIDUAL_SECONDS = 0.6;

        bool finite(Vec2 a) noexcept {
            return std::isfinite(a.x) && std::isfinite(a.y);
        }
        double safe_param(double value, double fallback, double lo, double hi) noexcept {
            return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
        }
    }  // namespace

    void Smoke::seed(std::uint64_t value) noexcept {
        m_rng = value;
        m_seed = unit_draw(m_rng);
        m_frame.seed = m_seed * m_params.variation;
    }

    void Smoke::reset() noexcept {
        *this = Smoke{};
    }

    void Smoke::drive(Vec2 translation, bool layout_animated, Vec2 size_change) noexcept {
        if (!finite(translation) || !finite(size_change) || m_closed) {
            return;
        }
        m_pending = add(m_pending, translation);
        m_resize_pending += 0.5 * length(size_change);
        if (layout_animated) {
            m_layout_pending = add(m_layout_pending, translation);
        }
    }

    void Smoke::stop_motion() noexcept {
        m_pending = {};
        m_layout_pending = {};
        m_resize_pending = 0;
        m_frame.velocity = {};
        m_motion_speed = 0;
    }

    void Smoke::advance(double elapsed, Vec2 size, SmokeParams params, bool pointer_drag) noexcept {
        m_frame.size = safe_size(size);
        m_params.strength = safe_param(params.strength, 0.85, 0.0, 1.5);
        m_params.turbulence = safe_param(params.turbulence, 0.65, 0.0, 1.0);
        m_params.trail = safe_param(params.trail, 0.65, 0.0, 1.0);
        m_params.reform = safe_param(params.reform, 0.48, 0.02, 10.0);
        m_params.settle_speed = safe_param(params.settle_speed, 1.0, 0.1, 2.0);
        m_params.mass = safe_param(params.mass, 0.45, 0.05, 1.0);
        m_params.roundness = safe_param(params.roundness, 0.7, 0.0, 1.0);
        m_params.color = params.color;
        m_params.density = safe_param(params.density, 1.0, 0.0, 4.0);
        m_params.variation = safe_param(params.variation, 0.5, 0.0, 1.0);
        m_frame.turbulence = m_params.turbulence;
        m_frame.trail = m_params.trail;
        const bool TRANSITION = m_opening || m_closing;
        const double LEGACY_LIFE = 0.55 + 0.6 * m_params.trail;
        m_frame.life = !TRANSITION && params.trail_lifetime > 0
                           ? safe_param(params.trail_lifetime, LEGACY_LIFE, 0.1, 4.0)
                           : LEGACY_LIFE;
        m_frame.trail_length = TRANSITION ? 0 : safe_param(params.trail_length, 0.0, 0.0, 2400.0);
        m_frame.trail_thickness =
            TRANSITION ? 1 : safe_param(params.trail_thickness, 1.0, 0.25, 2.5);
        m_frame.mass_size = m_params.mass;
        m_frame.mass_roundness = m_params.roundness;
        m_frame.color = m_params.color;
        m_frame.density = m_params.density;
        m_frame.variation = m_params.variation;
        // Noise fields move away from a shared pattern only as far as variation allows.
        m_frame.seed = m_seed * m_params.variation;
        const double DRAG_FOLLOW = safe_param(params.drag_follow, 0.0, 0.0, 1.0);
        const bool FOLLOW_DRAG = DRAG_FOLLOW > 0 && pointer_drag && !TRANSITION && !m_closed;
        m_frame.drag_follow = FOLLOW_DRAG ? DRAG_FOLLOW : std::min(m_frame.drag_follow, DRAG_FOLLOW);
        if (FOLLOW_DRAG && DRAG_FOLLOW == 1.0) {
            m_frame.carry = {};
            m_carry_velocity = {};
            m_layout_pending = {};
        }
        if (!std::isfinite(elapsed) || elapsed <= 0 || m_closed) {
            return;
        }
        const double MAX_CARRY = std::max(m_frame.size.x, m_frame.size.y) * 0.6;
        m_frame.carry = limit(sub(m_frame.carry, m_layout_pending), MAX_CARRY);
        m_layout_pending = {};
        const double REFORM = m_params.reform / m_params.settle_speed;
        m_accumulator += std::min(elapsed, STALL_SECONDS);
        while (m_accumulator + 1e-10 >= STEP) {
            if (!FOLLOW_DRAG) {
                m_frame.drag_follow = std::max(0.0, m_frame.drag_follow - DRAG_FOLLOW * (STEP / 0.12));
            }
            const Vec2 DELTA = mul(m_pending, STEP / m_accumulator);
            const double RESIZE_DELTA = m_resize_pending * STEP / m_accumulator;
            m_resize_pending = std::max(0.0, m_resize_pending - RESIZE_DELTA);
            m_pending = sub(m_pending, DELTA);
            m_accumulator = std::max(0.0, m_accumulator - STEP);
            const Vec2 TARGET = limit(mul(DELTA, 1.0 / STEP), MAX_SPEED);
            const double VELOCITY_BLEND = 1.0 - std::exp(-STEP / 0.09);
            m_frame.velocity =
                add(m_frame.velocity, mul(sub(TARGET, m_frame.velocity), VELOCITY_BLEND));
            const double SPEED = length(m_frame.velocity);
            const double DEFORMATION_SPEED = RESIZE_DELTA > 0
                                                 ? std::hypot(SPEED, std::min(RESIZE_DELTA / STEP, MAX_SPEED))
                                                 : SPEED;
            m_motion_speed = blend(m_motion_speed, DEFORMATION_SPEED, 1.0 - std::exp(-STEP / 0.12));
            const double MOTION_TARGET =
                smoothstep(20.0, 260.0, m_motion_speed * m_params.strength);
            const double MOTION_TAU = MOTION_TARGET > m_motion_smoke ? 0.16 : REFORM;
            m_motion_smoke =
                blend(m_motion_smoke, MOTION_TARGET, 1.0 - std::exp(-STEP / MOTION_TAU));
            if (MOTION_TARGET == 0 && m_motion_smoke < 0.001) {
                m_motion_smoke = 0;
            }
            if (length(m_frame.velocity) < 0.01 && length(TARGET) < 0.01) {
                m_frame.velocity = {};
            }
            // The body responds to velocity changes later than the window does.
            m_frame.lag =
                add(m_frame.lag, mul(sub(m_frame.velocity, m_frame.lag), 1.0 - std::exp(-STEP / 0.28)));
            if (length(m_frame.lag) < 0.01 && length(m_frame.velocity) == 0) {
                m_frame.lag = {};
            }
            // The window contracts into its moving body ahead of unravelling, and expands
            // back slightly ahead of condensing so content reforms at full size.
            const SmokeBody BEFORE = smoke_body(m_frame);
            const double MASS_TAU = MOTION_TARGET > m_frame.mass ? 0.12 : 0.75 * REFORM;
            m_frame.mass = blend(m_frame.mass, MOTION_TARGET, 1.0 - std::exp(-STEP / MASS_TAU));
            if (MOTION_TARGET == 0 && m_frame.mass < 0.001) {
                m_frame.mass = 0;
            }

            // A critically damped return avoids an abrupt jump when a layout animation stops.
            constexpr double OMEGA = 9.0;
            const Vec2 TEMP = mul(add(m_carry_velocity, mul(m_frame.carry, OMEGA)), STEP);
            const double DAMPING = std::exp(-OMEGA * STEP);
            m_frame.carry = mul(add(m_frame.carry, TEMP), DAMPING);
            m_carry_velocity = mul(sub(m_carry_velocity, mul(TEMP, OMEGA)), DAMPING);
            if (length(m_frame.carry) < 0.001 && length(m_carry_velocity) < 0.001) {
                m_frame.carry = {};
                m_carry_velocity = {};
            }

            m_frame.position = add(m_frame.position, DELTA);
            // The wake lies along the path the window has just travelled. Centre the fluid
            // domain between the window and the recent path, so a sustained move leaves room
            // behind it while back-and-forth dragging keeps the domain over the whole wake
            // instead of swinging its edge across it.
            const double MARGIN = 0.5 * (smoke_domain(m_frame.size).x - m_frame.size.x);
            m_trail = blend(m_trail, m_frame.position, 1.0 - std::exp(-STEP / (0.35 * m_frame.life)));
            m_frame.offset = limit(sub(m_trail, m_frame.position), 0.6 * MARGIN);
            m_frame.time += STEP;
            m_frame.settle = 0;
            if (m_opening || m_closing) {
                m_elapsed = std::min(m_elapsed + STEP, m_duration);
                const double PROGRESS = m_elapsed / m_duration;
                plan_transit(PROGRESS);
                if (m_opening && PROGRESS >= 1.0) {
                    m_opening = false;
                }
            } else {
                m_frame.smoke = m_motion_smoke;
                m_frame.presence = 1.0;
                m_frame.transit = 0;
                m_frame.attract = 0;
                m_frame.wind = 0;
                m_frame.gather = 0;
                m_frame.dispersal = 0;
                // The opening wake stays outside the formed surface until it dwindles
                // or movement takes ownership of the same field.
                if (m_motion_smoke > 0 || m_frame.mass > 0 || m_residual <= 0) {
                    m_frame.order = 0;
                }
                m_frame.edge_dir = {};
                // Only opening gathers loose smoke from afar. A settling window keeps its body
                // together itself and lets what it sheds drift away and dissipate.
                m_frame.cohesion = blend(m_frame.cohesion, 0.0, 1.0 - std::exp(-STEP / 0.12));
                // Smoke still unreformed once the motion has eased off sheds as wisps.
                m_frame.settle = m_motion_smoke * (1.0 - MOTION_TARGET);
            }
            if (!m_closing) {
                m_frame.smoke = std::max(m_frame.smoke, m_motion_smoke);
            }
            const SmokeBody AFTER = smoke_body(m_frame);
            const double EXTENT = length(AFTER.half);
            // Only the movement body squeezes the material; a head grows on its own.
            m_frame.contraction = EXTENT > 0 && m_frame.transit <= 0
                                      ? (length(BEFORE.half) - EXTENT) / (STEP * EXTENT)
                                      : 0;

            const double RESIDUAL = m_closing ? CLOSE_RESIDUAL_SECONDS : RESIDUAL_LIFETIMES * m_frame.life;
            const bool FEEDING =
                m_opening || (m_frame.smoke > 0.002 && m_frame.presence > 0.002);
            m_residual = FEEDING ? RESIDUAL : std::max(0.0, m_residual - STEP);
            // Ageing thins the wake on its own; the shared fade only removes the last traces.
            m_frame.fade = FEEDING ? 1.0 : smoothstep(0.0, 0.45, m_residual / RESIDUAL);
            if (m_closing && m_elapsed >= m_duration && m_residual <= 0) {
                m_closing = false;
                m_closed = true;
                m_frame.presence = 0;
                m_frame.smoke = 1;
                m_frame.fade = 0;
                break;
            }
        }
    }

    bool Smoke::active() const noexcept {
        return !m_closed &&
            (m_opening || m_closing || m_frame.smoke > 0 || m_frame.mass > 0 || m_residual > 0 ||
                length(m_frame.carry) > 0 || length(m_frame.velocity) > 0 ||
                length(m_frame.lag) > 0 || length(m_pending) > 0 || m_resize_pending > 0);
    }
    bool Smoke::opening() const noexcept {
        return m_opening;
    }
    bool Smoke::closing() const noexcept {
        return m_closing;
    }
    bool Smoke::closed() const noexcept {
        return m_closed;
    }
    SmokeFrame Smoke::frame(Vec2 size) const noexcept {
        auto result = m_frame;
        result.size = safe_size(size);
        // Attenuate visible layout carry, not the underlying spring state.
        result.carry = mul(result.carry, 1.0 - result.drag_follow);
        // The window is drawn where it is now, including translation the fixed substeps have
        // not consumed yet. Anchoring the fluid there keeps world-fixed smoke still on screen;
        // anchoring it to the last substep would shake all of it along the drag by up to a
        // substep's travel whenever frames fall between substeps.
        result.position = add(m_frame.position, m_pending);
        return result;
    }
    Box Smoke::bounds(Vec2 size) const noexcept {
        size = safe_size(size);
        const Vec2 DOMAIN = smoke_domain(size, m_frame.trail_length);
        return {.x = ((size.x - DOMAIN.x) * 0.5) + m_frame.offset.x,
            .y = ((size.y - DOMAIN.y) * 0.5) + m_frame.offset.y,
            .w = DOMAIN.x,
            .h = DOMAIN.y};
    }

    SmokeBody smoke_body(const SmokeFrame &frame) noexcept {
        const Vec2 SIZE = safe_size(frame.size);
        const Vec2 WINDOW{.x = SIZE.x * 0.5, .y = SIZE.y * 0.5};
        const double MASS = smoothstep(0.0, 1.0, std::clamp(frame.mass, 0.0, 1.0));
        const auto TARGET = contracted(WINDOW, frame.mass_size, frame.mass_roundness);
        SmokeBody body;
        body.half = blend(WINDOW, TARGET.half, MASS);
        body.radius = TARGET.corner * MASS;
        // Free motion lags during acceleration and runs on while braking. A grabbed
        // body follows the window directly; its detached smoke still has inertia.
        const double LIMIT = 0.35 * std::min(WINDOW.x, WINDOW.y);
        body.offset =
            mul(limit(mul(sub(frame.lag, frame.velocity), 0.1), LIMIT), MASS * (1.0 - frame.drag_follow));
        const double SPEED = length(frame.lag);
        if (SPEED > 1e-6) {
            body.stretch = mul(frame.lag, 0.5 * smoothstep(60.0, 1600.0, SPEED) * MASS / SPEED);
        }
        const double TRANSIT = std::clamp(frame.transit, 0.0, 1.0);
        if (TRANSIT > 0) {
            const Vec2 HALF{.x = std::max(frame.head_half.x, 1.0), .y = std::max(frame.head_half.y, 1.0)};
            body.half = blend(body.half, HALF, TRANSIT);
            body.radius = blend(body.radius, std::min(frame.head_radius, std::min(HALF.x, HALF.y)), TRANSIT);
            body.offset = blend(body.offset, frame.head, TRANSIT);
            const double HEAD_SPEED = length(frame.head_lag);
            const Vec2 STRETCH = HEAD_SPEED > 1e-6
                                     ? mul(frame.head_lag, 0.45 * smoothstep(60.0, 1400.0, HEAD_SPEED) / HEAD_SPEED)
                                     : Vec2{};
            body.stretch = blend(body.stretch, STRETCH, TRANSIT);
        }
        return body;
    }
}  // namespace hyprimate
