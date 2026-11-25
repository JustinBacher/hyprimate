#include <algorithm>
#include <cmath>
#include <numbers>

#include "window/smoke/Smoke.hpp"
#include "window/smoke/SmokeDetail.hpp"

namespace hyprimate {
    using namespace smoke_detail;

    namespace {
        constexpr double PI = std::numbers::pi;

        double dot(Vec2 a, Vec2 b) noexcept {
            return (a.x * b.x) + (a.y * b.y);
        }
        double centred(double draw) noexcept {
            return (2.0 * draw) - 1.0;
        }
    }  // namespace

    void Smoke::begin(Transition direction, double duration, Vec2 size, SmokeRoute route) noexcept {
        m_frame.size = safe_size(size);
        if (direction != Transition::Close || m_frame.transit >= 1.0) {
            m_frame.close_source_body.reset();
        } else if (!m_frame.close_source_body) {
            m_frame.close_source_body = smoke_body(m_frame);
        }
        m_frame.drag_follow = 0;
        m_frame.trail_length = 0;
        m_frame.trail_thickness = 1;
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
            m_frame.wind =
                SPIN * 0.55 * smoothstep(0.0, 0.15, P) * (1.0 - smoothstep(0.62, 0.99, P));
        } else if (m_opening) {
            // Streams start along the screen edge, gather into a head that travels in along a
            // slight bow, then the head fills out into the window from its arrival side.
            const Vec2 ENTRY = add(mul(D, m_route.reach), mul(SIDE, ALONG));
            m_frame.edge_point = ENTRY;
            m_frame.edge_dir = D;
            m_frame.gather = smoothstep(0.0, 0.05, P) * (1.0 - smoothstep(0.15, 0.42, P));
            const double TRAVEL = smoothstep(0.1, 0.6, P);
            const double BOW =
                centred(m_draw[1]) * 0.18 * std::min(m_route.reach, 900.0) * VARIATION;
            head = add(blend(ENTRY, {}, TRAVEL), mul(SIDE, std::sin(PI * TRAVEL) * BOW));
            const double FILL = smoothstep(0.46, 0.94, P);
            half = blend(
                blend(mul(MASS.half, 0.5), MASS.half, smoothstep(0.05, 0.5, P)), WINDOW, FILL);
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
                mul(MASS.half, 0.12),
                smoothstep(0.55, 1.0, P));
            radius = std::min(half.x, half.y);
            head = add(INSIDE, mul(WANDER, 0.04 * SHORTEST));
            smoke = blend(m_start_smoke, 1.0, smoothstep(0.0, 0.6, P));
            presence = blend(m_start_presence, 0.0, smoothstep(0.55, 1.0, P));
            m_frame.attract = smoothstep(0.0, 0.2, P) * (1.0 - (0.5 * smoothstep(0.8, 1.0, P)));
            m_frame.wind =
                SPIN * 0.9 * smoothstep(0.1, 0.4, P) * (1.0 - (0.6 * smoothstep(0.7, 1.0, P)));
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
            const double LOOP = std::max(
                20.0, std::min(SPAN, DEPTH) * (0.45 + (0.12 * centred(m_draw[2]) * VARIATION)));
            const double BACK = -0.3 * LOOP * smoothstep(0.0, 0.2, P);
            const double ANGLE = (-0.5 * PI) - (PI * smoothstep(0.18, 0.5, P));
            const double DEPART = std::clamp((P - 0.44) / 0.56, 0.0, 1.0);
            const double EXIT = m_route.reach + (1.2 * std::max(MASS.half.x, MASS.half.y)) + 40.0;
            const double DISTANCE = (EXIT - dot(ORIGIN, D) - BACK) * std::pow(DEPART, 1.7);
            const double SERPENT =
                LOOP * 0.35 * std::sin((DEPART * 2.0 * PI * 1.2) + (6.28 * m_draw[3] * VARIATION)) *
                (1.0 - DEPART) * smoothstep(0.0, 0.15, DEPART) * (0.4 + (0.6 * VARIATION));
            head = add(ORIGIN,
                add(mul(D, BACK + (LOOP * std::cos(ANGLE)) + DISTANCE),
                    mul(turn, LOOP + (LOOP * std::sin(ANGLE)) + SERPENT)));
            half = blend(
                blend(Vec2{.x = TINY, .y = TINY}, mul(MASS.half, 0.6), smoothstep(0.0, 0.22, P)),
                MASS.half,
                smoothstep(0.44, 0.7, P));
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
        m_frame.head_velocity =
            blend(m_frame.head_velocity, VELOCITY, 1.0 - std::exp(-STEP / 0.03));
        m_frame.head_lag =
            blend(m_frame.head_lag, m_frame.head_velocity, 1.0 - std::exp(-STEP / 0.2));
        m_frame.head = head;
        m_frame.head_half = half;
        m_frame.head_radius = radius;
        m_frame.transit = transit;
        m_frame.smoke = smoke;
        m_frame.presence = presence;
        m_frame.cohesion = blend(m_frame.cohesion, cohesion, 1.0 - std::exp(-STEP / 0.12));
        m_frame.offset = add(m_frame.offset, mul(head, follow * transit));
    }
}  // namespace hyprimate
