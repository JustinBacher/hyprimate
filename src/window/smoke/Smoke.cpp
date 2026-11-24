#include <algorithm>
#include <cmath>
#include <numbers>

#include "window/smoke/Smoke.hpp"

namespace hyprimate {
    namespace {
        constexpr double STEP = SUBSTEP_SECONDS;
        constexpr double MAX_SPEED = 3000.0;
        constexpr double PI = std::numbers::pi;
        // Time for wake smoke to finish dissipating once nothing feeds the fluid, as a share of
        // its lifetime: the age-driven thinning has cleared it by then.
        constexpr double RESIDUAL_LIFETIMES = 1.15;
        constexpr double CLOSE_RESIDUAL_SECONDS = 0.6;

        Vec2 add(Vec2 a, Vec2 b) noexcept {
            return {.x = a.x + b.x, .y = a.y + b.y};
        }
        Vec2 sub(Vec2 a, Vec2 b) noexcept {
            return {.x = a.x - b.x, .y = a.y - b.y};
        }
        Vec2 mul(Vec2 a, double b) noexcept {
            return {.x = a.x * b, .y = a.y * b};
        }
        double length(Vec2 a) noexcept {
            return std::hypot(a.x, a.y);
        }
        double dot(Vec2 a, Vec2 b) noexcept {
            return (a.x * b.x) + (a.y * b.y);
        }
        Vec2 limit(Vec2 a, double maximum) noexcept {
            const double MAGNITUDE = length(a);
            return MAGNITUDE > maximum ? mul(a, maximum / MAGNITUDE) : a;
        }
        bool finite(Vec2 a) noexcept {
            return std::isfinite(a.x) && std::isfinite(a.y);
        }
        Vec2 safe_size(Vec2 size) noexcept {
            return {.x = std::isfinite(size.x) ? std::clamp(size.x, 1.0, 32768.0) : 1.0,
                .y = std::isfinite(size.y) ? std::clamp(size.y, 1.0, 32768.0) : 1.0};
        }
        double safe_param(double value, double fallback, double lo, double hi) noexcept {
            return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
        }
        double blend(double a, double b, double t) noexcept {
            return a + ((b - a) * t);
        }
        Vec2 blend(Vec2 a, Vec2 b, double t) noexcept {
            return {.x = blend(a.x, b.x, t), .y = blend(a.y, b.y, t)};
        }
        double centred(double draw) noexcept {
            return (2.0 * draw) - 1.0;
        }

        // Fully contracted body: roundness evens out the proportions at constant area and
        // rounds the corners; size scales both axes. Neither changes the other.
        struct Contracted {
            Vec2 half;
            double corner = 0;
        };
        Contracted contracted(Vec2 window, double mass_size, double roundness) noexcept {
            const double SCALE = std::clamp(mass_size, 0.05, 1.0);
            const double ROUND = std::clamp(roundness, 0.0, 1.0);
            const Vec2 SCALED{.x = window.x * SCALE, .y = window.y * SCALE};
            const double EVEN = std::sqrt(SCALED.x * SCALED.y);
            const Vec2 HALF{.x = blend(SCALED.x, EVEN, ROUND), .y = blend(SCALED.y, EVEN, ROUND)};
            return {.half = HALF, .corner = blend(0.12, 1.0, ROUND) * std::min(HALF.x, HALF.y)};
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

    void Smoke::begin(Transition direction, double duration, Vec2 size, SmokeRoute route) noexcept {
        m_frame.size = safe_size(size);
        if (direction != Transition::Close || m_frame.transit >= 1.0) {
            m_frame.close_source_body.reset();
        } else if (!m_frame.close_source_body) {
            m_frame.close_source_body = smoke_body(m_frame);
        }
        m_frame.drag_follow = 0;
        m_frame.trail_length = 0;
        m_frame.trail_girth = 1;
        m_duration = std::isfinite(duration) && duration > 0.0
                         ? duration
                         : (direction == Transition::Open ? 1.4 : 1.6);
        m_elapsed = 0;
        if (direction == Transition::Open && (!active() || m_closed)) {
            m_frame.smoke = 1;
            m_frame.presence = 0;
            m_frame.transit = 0;
            m_frame.order = -0.8;
        }
        m_start_smoke = m_frame.smoke;
        m_start_presence = m_frame.presence;
        // An interrupted transition hands its head over to the new one.
        m_from_transit = m_frame.transit;
        m_from_head = m_frame.head;
        m_from_half = m_frame.head_half;
        m_from_radius = m_frame.head_radius;
        const double DIRECTION = length(route.direction);
        m_route = route;
        if (!route.edge || !(DIRECTION > 0.5) || !std::isfinite(route.reach)) {
            m_route = {};
        } else {
            m_route.direction = mul(route.direction, 1.0 / DIRECTION);
            m_route.reach = std::max(route.reach, 0.0);
        }
        for (auto &draw : m_draw) {
            draw = unit_draw(m_rng);
        }
        m_opening = direction == Transition::Open;
        m_closing = direction == Transition::Close;
        m_closed = false;
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

    void Smoke::plan_transit(double progress) noexcept {
        const double P = std::clamp(progress, 0.0, 1.0);
        const double VARIATION = m_params.variation;
        const Vec2 WINDOW{.x = m_frame.size.x * 0.5, .y = m_frame.size.y * 0.5};
        const double SHORTEST = std::min(WINDOW.x, WINDOW.y);
        const auto MASS = contracted(WINDOW, m_params.mass, m_params.roundness);
        const double TINY = std::max(3.0, 0.02 * SHORTEST);
        // A random point well inside the window; the centre without variation.
        const Vec2 INSIDE{
            .x = centred(m_draw[0]) * std::max(WINDOW.x - (0.6 * MASS.half.x), 0.0) * VARIATION,
            .y = centred(m_draw[1]) * std::max(WINDOW.y - (0.6 * MASS.half.y), 0.0) * VARIATION};
        // Slow, seeded drift of a head that is otherwise holding its place.
        const Vec2 WANDER{.x = std::sin((m_elapsed * 1.1) + (6.28 * m_draw[2] * VARIATION)),
            .y = std::cos((m_elapsed * 0.8) + (6.28 * m_draw[3] * VARIATION))};
        const double SPIN = m_draw[4] < 0.5 * VARIATION ? -1.0 : 1.0;

        Vec2 head;
        Vec2 half;
        double radius = 0;
        double transit = 1;
        double smoke = 1;
        double presence = 1;
        double cohesion = 0;
        double follow = 0;  // how far the fluid domain travels with the head
        m_frame.attract = 0;
        m_frame.wind = 0;
        m_frame.gather = 0;
        m_frame.dispersal = 0;
        m_frame.order = 0;
        m_frame.edge_point = {};
        m_frame.edge_dir = {};
        const Vec2 D = m_route.direction;
        const Vec2 SIDE{.x = -D.y, .y = D.x};
        const double SPAN = (std::abs(SIDE.x) * WINDOW.x) + (std::abs(SIDE.y) * WINDOW.y);
        const double DEPTH = (std::abs(D.x) * WINDOW.x) + (std::abs(D.y) * WINDOW.y);
        const double ALONG = centred(m_draw[0]) * 0.75 * SPAN * VARIATION;
        const auto UV = [&](Vec2 point) {
            return Vec2{.x = std::clamp((point.x / m_frame.size.x) + 0.5, 0.0, 1.0),
                .y = std::clamp((point.y / m_frame.size.y) + 0.5, 0.0, 1.0)};
        };

        if (m_opening && !m_route.edge) {
            // Gather a visible head before its density starts resolving into the window.
            const double GROW = smoothstep(0.0, 0.6, P);
            const double FILL = smoothstep(0.30, 0.94, P);
            half = blend(blend(Vec2{.x = TINY, .y = TINY}, MASS.half, GROW), WINDOW, FILL);
            radius = std::min(half.x, half.y) * (1.0 - smoothstep(0.5, 0.95, P));
            head = add(blend(INSIDE, {}, FILL), mul(WANDER, 0.05 * SHORTEST * (1.0 - FILL)));
            transit = 1.0 - smoothstep(0.9, 1.0, P);
            smoke = blend(m_start_smoke, 0.0, std::clamp((P - 0.35) / 0.65, 0.0, 1.0));
            presence = blend(m_start_presence, 1.0, smoothstep(0.0, 0.40, P));
            cohesion = 1;
            m_frame.order = -0.8;
            m_frame.origin_uv = UV(INSIDE);
            m_frame.wind = SPIN * 0.55 * smoothstep(0.0, 0.15, P) * (1.0 - smoothstep(0.62, 0.99, P));
        } else if (m_opening) {
            // Streams start along the screen edge, gather into a head that travels in along a
            // slight bow, then the head fills out into the window from its arrival side.
            const Vec2 ENTRY = add(mul(D, m_route.reach), mul(SIDE, ALONG));
            m_frame.edge_point = ENTRY;
            m_frame.edge_dir = D;
            m_frame.gather = smoothstep(0.0, 0.05, P) * (1.0 - smoothstep(0.15, 0.42, P));
            const double TRAVEL = smoothstep(0.1, 0.6, P);
            const double BOW = centred(m_draw[1]) * 0.18 * std::min(m_route.reach, 900.0) * VARIATION;
            head = add(blend(ENTRY, {}, TRAVEL), mul(SIDE, std::sin(PI * TRAVEL) * BOW));
            const double FILL = smoothstep(0.46, 0.94, P);
            half = blend(blend(mul(MASS.half, 0.5), MASS.half, smoothstep(0.05, 0.5, P)), WINDOW, FILL);
            radius = std::min(half.x, half.y) * (1.0 - smoothstep(0.6, 0.95, P));
            transit = 1.0 - smoothstep(0.9, 1.0, P);
            smoke = blend(m_start_smoke, 0.0, std::clamp((P - 0.42) / 0.58, 0.0, 1.0));
            presence = blend(m_start_presence, 1.0, smoothstep(0.0, 0.32, P));
            cohesion = 1;
            follow = 1.0 - smoothstep(0.45, 0.85, P);
            m_frame.order = -0.8;
            m_frame.origin_uv = UV(add(mul(D, 0.9 * DEPTH), mul(SIDE, ALONG)));
            m_frame.wind = (BOW < 0 ? -SPIN : SPIN) * 0.5 * smoothstep(0.2, 0.45, P) *
                (1.0 - smoothstep(0.72, 0.99, P));
        } else if (!m_route.edge) {
            // The window breaks down from its rim into a swirling mass at a random point, which
            // contracts and thins away.
            half = blend(blend(mul(MASS.half, 0.3), MASS.half, smoothstep(0.0, 0.4, P)),
                mul(MASS.half, 0.12), smoothstep(0.55, 1.0, P));
            radius = std::min(half.x, half.y);
            head = add(INSIDE, mul(WANDER, 0.04 * SHORTEST));
            smoke = blend(m_start_smoke, 1.0, smoothstep(0.0, 0.6, P));
            presence = blend(m_start_presence, 0.0, smoothstep(0.55, 1.0, P));
            m_frame.attract = smoothstep(0.0, 0.2, P) * (1.0 - (0.5 * smoothstep(0.8, 1.0, P)));
            m_frame.wind = SPIN * 0.9 * smoothstep(0.1, 0.4, P) * (1.0 - (0.6 * smoothstep(0.7, 1.0, P)));
            m_frame.dispersal = 0.8 * smoothstep(0.65, 1.0, P);
            m_frame.origin_uv = UV(INSIDE);
        } else {
            // A head gathers on the side opposite the exit, facing away from it. It rears back
            // round a loop to face the exit, pauses, then leaves through the edge with the rest
            // of the window converting behind it and pulled along after it.
            Vec2 turn = std::abs(D.x) > 0.5 ? Vec2{.x = 0, .y = -1}
                                             : (ALONG > 1e-6 ? mul(SIDE, -1.0) : SIDE);
            if (m_draw[5] < 0.5 * VARIATION) {
                turn = mul(turn, -1.0);
            }
            const Vec2 ORIGIN = add(mul(D, -0.82 * DEPTH), mul(SIDE, ALONG));
            const double LOOP =
                std::max(20.0, std::min(SPAN, DEPTH) * (0.45 + (0.12 * centred(m_draw[2]) * VARIATION)));
            const double BACK = -0.3 * LOOP * smoothstep(0.0, 0.2, P);
            const double ANGLE = (-0.5 * PI) - (PI * smoothstep(0.18, 0.5, P));
            const double DEPART = std::clamp((P - 0.44) / 0.56, 0.0, 1.0);
            const double EXIT = m_route.reach + (1.2 * std::max(MASS.half.x, MASS.half.y)) + 40.0;
            const double DISTANCE = (EXIT - dot(ORIGIN, D) - BACK) * std::pow(DEPART, 1.7);
            const double SERPENT = LOOP * 0.35 * std::sin((DEPART * 2.0 * PI * 1.2) + (6.28 * m_draw[3] * VARIATION)) *
                (1.0 - DEPART) * smoothstep(0.0, 0.15, DEPART) * (0.4 + (0.6 * VARIATION));
            head = add(ORIGIN,
                add(mul(D, BACK + (LOOP * std::cos(ANGLE)) + DISTANCE),
                    mul(turn, LOOP + (LOOP * std::sin(ANGLE)) + SERPENT)));
            half = blend(blend(Vec2{.x = TINY, .y = TINY}, mul(MASS.half, 0.6), smoothstep(0.0, 0.22, P)),
                MASS.half, smoothstep(0.44, 0.7, P));
            radius = std::min(half.x, half.y);
            smoke = blend(m_start_smoke, 1.0, smoothstep(0.03, 0.78, P));
            presence = blend(m_start_presence, 0.0, smoothstep(0.8, 1.0, P));
            follow = smoothstep(0.4, 0.75, P);
            m_frame.attract = smoothstep(0.06, 0.25, P) * (1.0 - smoothstep(0.9, 1.0, P));
            // The air turns with the head's loop: the sense of rotation from D toward turn.
            const double SENSE = ((D.x * turn.y) - (D.y * turn.x)) > 0 ? -1.0 : 1.0;
            m_frame.wind = SENSE * (0.9 + (0.3 * centred(m_draw[4]) * VARIATION)) *
                smoothstep(0.1, 0.3, P) * (1.0 - smoothstep(0.6, 1.0, P));
            m_frame.dispersal = 0.7 * smoothstep(0.85, 1.0, P);
            m_frame.order = 1;
            m_frame.origin_uv = UV(ORIGIN);
        }

        // A transition interrupting another continues from where its head was.
        const double HANDOVER = m_from_transit > 0.01 ? smoothstep(0.0, 0.2, P) : 1.0;
        head = blend(m_from_head, head, HANDOVER);
        half = blend(m_from_half, half, HANDOVER);
        radius = blend(m_from_radius, radius, HANDOVER);
        transit = blend(m_from_transit, transit, smoothstep(0.0, 0.08, P));
        if (m_frame.close_source_body && transit >= 1.0) {
            m_frame.close_source_body.reset();
        }
        const Vec2 VELOCITY = m_frame.transit > 0.0 || m_from_transit > 0.01
                                  ? limit(mul(sub(head, m_frame.head), 1.0 / STEP), 2.0 * MAX_SPEED)
                                  : Vec2{};
        m_frame.head_velocity = blend(m_frame.head_velocity, VELOCITY, 1.0 - std::exp(-STEP / 0.03));
        m_frame.head_lag = blend(m_frame.head_lag, m_frame.head_velocity, 1.0 - std::exp(-STEP / 0.2));
        m_frame.head = head;
        m_frame.head_half = half;
        m_frame.head_radius = radius;
        m_frame.transit = transit;
        m_frame.smoke = smoke;
        m_frame.presence = presence;
        m_frame.cohesion = blend(m_frame.cohesion, cohesion, 1.0 - std::exp(-STEP / 0.12));
        m_frame.offset = add(m_frame.offset, mul(head, follow * transit));
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
        // Transition smoke keeps its existing response to the shared trail amount.
        m_frame.life = TRANSITION ? 0.55 + 0.6 * m_params.trail
                                  : safe_param(params.trail_lifetime, 0.94, 0.1, 4.0);
        m_frame.trail_length = TRANSITION ? 0 : safe_param(params.trail_length, 0.0, 0.0, 2400.0);
        m_frame.trail_girth = TRANSITION ? 1 : safe_param(params.trail_girth, 1.0, 0.25, 2.5);
        m_frame.mass_size = m_params.mass;
        m_frame.mass_roundness = m_params.roundness;
        m_frame.color = m_params.color;
        m_frame.density = m_params.density;
        m_frame.variation = m_params.variation;
        // Noise fields move away from a shared pattern only as far as variation allows.
        m_frame.seed = m_seed * m_params.variation;
        const bool FOLLOW_DRAG = params.follow_pointer && pointer_drag && !TRANSITION && !m_closed;
        if (!params.follow_pointer) {
            m_frame.drag_follow = 0;
        }
        if (FOLLOW_DRAG) {
            m_frame.drag_follow = 1;
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
                m_frame.drag_follow = std::max(0.0, m_frame.drag_follow - STEP / 0.12);
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
