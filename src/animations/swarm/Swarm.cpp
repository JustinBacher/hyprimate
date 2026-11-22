#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numbers>
#include <span>

#include "animations/shared/Core.hpp"
#include "animations/swarm/Swarm.hpp"

namespace hyprimate {
    namespace {

        constexpr double STEP = 1.0 / 120.0;
        constexpr double ACCUMULATOR_TOLERANCE = 1e-9;

        // Steering. Accelerations are multiples of an agent's cruise speed per second.
        constexpr double FORCE_MIN = 4.0, FORCE_MAX = 14.0;  // by responsiveness
        constexpr double STEER_TIME = 0.07;  // s to close a velocity error, before the force limit
        constexpr double SLOW_MARGIN = 1.5;  // braking distance margin for arriving
        constexpr double SPEED_CAP = 1.45;  // top speed over cruise
        constexpr double TURN_RATE = 14.0;  // rad/s the heading can swing
        constexpr double HEADING_SPEED = 0.04;  // of cruise: slower than this keeps the old heading

        // The route. An agent walks in the direction of its target turned by its approach arc and
        // its meander, so it comes in on a wandering curve; both fade out near home, where the turn
        // would otherwise become an orbit.
        constexpr double ARC_ANGLE = 0.9;  // rad either way, at full variation
        constexpr double MEANDER_ANGLE = 0.9;  // rad either way, at full wander
        constexpr double ROUTE_LIMIT =
            1.15;  // rad: it is never turned further than this off its target
        constexpr double WEAVE_RATE_MIN = 5.0, WEAVE_RATE_MAX = 11.0;  // rad/s
        constexpr double ROUTE_NEAR = 0.5,
                         ROUTE_FAR = 2.0;  // spacings from home over which route and crowd fade in

        // The gait. An agent hesitates for part of every gait cycle, so a column of them bunches
        // and strings out.
        constexpr double GAIT_DEPTH = 0.85;  // share of cruise lost in a hesitation, at full wander
        constexpr double GAIT_FROM = 0.45,
                         GAIT_FULL = 0.95;  // sine of the gait phase over which it slows
        constexpr double GAIT_RATE_MIN = 5.0, GAIT_RATE_MAX = 10.0;  // rad/s

        // The crowd. Radii in tile spacings; force shares of the steering limit.
        constexpr double NEIGHBOUR_RADIUS = 3.0;
        constexpr double SEPARATION_RADIUS = 0.8;  // soft push inside this
        constexpr double SEPARATION_FORCE = 1.2;
        constexpr double AVOID_RADIUS = 2.4;  // how far ahead an agent looks for others in its way
        constexpr double AVOID_FORCE = 1.8;
        constexpr double AVOID_SPEED = 0.35;  // of cruise: closing this fast draws a full sidestep
        constexpr double CONTACT_FULL = 0.5;  // separation at which bodies are fully solid
        constexpr double TRANSIT_NEAR = 1.0,
                         TRANSIT_FAR = 5.0;  // spacings from home over which a body becomes solid
        constexpr double ALIGN_RATE = 3.0;  // 1/s velocity matching at full cohesion
        constexpr double COHESION_FORCE = 0.35;

        // Settling: a critically damped spring takes over near home, and everywhere once patience
        // runs out.
        constexpr double SPRING_NEAR = 0.25, SPRING_FAR = 1.25;  // spacings
        constexpr double SPRING_MIN = 9.0, SPRING_MAX = 26.0;  // rad/s by settling
        constexpr double SPRING_FORCE = 3.0;  // spring force limit over the steering limit
        constexpr double PATIENCE_MAX = 2.4,
                         PATIENCE_MIN =
                             0.5;  // s of a still window, after the stagger, before hurrying
        constexpr double PATIENCE_RAMP = 0.8;
        constexpr double PATIENCE_DEADLINE =
            4.0;  // s after patience at which every agent is placed
        constexpr double DOCK_DISTANCE = 1.5;  // px
        constexpr double DOCK_SPEED = 60.0;  // px/s
        constexpr double DOCK_WINDOW_SPEED =
            0.5;  // of grip: the window must be slower than this to dock on it
        constexpr double REST_RADIUS =
            0.7;  // spacings from home inside which an agent swings to its rest direction

        // Formation lock.
        constexpr double MERGE_MAX = 0.5,
                         MERGE_MIN = 0.18;  // s to merge into the window, by settling
        constexpr double SPLIT_TIME = 0.14;  // s to shrink from tile to creature
        constexpr double TURN_TIME = 0.16;  // s to swing between rest direction and heading
        constexpr double HOLD_LOCK = 0.35;  // a leaving agent waits until its lock is below this
        constexpr double CORNER_TIME = 0.18;  // s for the global surface rounding to change

        // Per-agent spread.
        constexpr double GRIP_MIN = 60.0, GRIP_MAX = 900.0;  // px/s by responsiveness
        constexpr double PACE_SPREAD = 0.7;  // total, at full variation
        constexpr double GRIP_SPREAD = 1.0;
        constexpr double WINDOW_SPEED_HOLD = 0.12;  // s the window speed estimate lingers

        // Departures, as shares of the stagger.
        constexpr double REACTION_SHARE =
            0.5;  // the longest an agent shaken loose by movement stays put
        constexpr double STAGGER_MAX = 5.0;  // s

        // Transitions.
        constexpr double ENTRY_DEPTH = 1.5;  // fits beyond the edge an agent starts
        constexpr double ENTRY_SPEED = 0.5;  // of cruise
        constexpr double
            DAWDLE = 0.7,
            HURRY = 1.3;  // of cruise: how far an opening agent bends its speed to be on time
        constexpr double LEAD =
            0.85;  // of the longest walk at cruise: when the first agents are due
        constexpr double EXIT_DEPTH = 3.0;  // fits beyond the edge an agent runs to
        constexpr double GONE_MARGIN = 1.2;  // fits beyond the edge at which it has left
        constexpr double CLOSE_DEADLINE =
            6.0;  // s after the stagger at which a close ends regardless

        constexpr int HASH_SIDE =
            32;  // buckets per axis; positions wrap and the distance check rejects aliases
        constexpr int HASH_CELLS = HASH_SIDE * HASH_SIDE;
        constexpr std::uint32_t NO_CELL = std::numeric_limits<std::uint32_t>::max();

        constexpr std::array<Edge, 4> EDGES{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};

        Vec2 operator+(Vec2 lhs, Vec2 rhs) noexcept {
            return {.x = lhs.x + rhs.x, .y = lhs.y + rhs.y};
        }
        Vec2 operator-(Vec2 lhs, Vec2 rhs) noexcept {
            return {.x = lhs.x - rhs.x, .y = lhs.y - rhs.y};
        }
        Vec2 operator*(Vec2 vector, double scale) noexcept {
            return {.x = vector.x * scale, .y = vector.y * scale};
        }
        double dot(Vec2 lhs, Vec2 rhs) noexcept {
            return (lhs.x * rhs.x) + (lhs.y * rhs.y);
        }
        double length(Vec2 vector) noexcept {
            return std::hypot(vector.x, vector.y);
        }
        double lerp(double start, double end, double fraction) noexcept {
            return start + ((end - start) * fraction);
        }
        double unit(double value) noexcept {
            return std::clamp(value, 0.0, 1.0);
        }

        Vec2 clamp_length(Vec2 vector, double limit) noexcept {
            const double LENGTH = length(vector);
            return LENGTH > limit && LENGTH > 0.0 ? vector * (limit / LENGTH) : vector;
        }

        // `vector` turned clockwise on screen (y down) by `angle`.
        Vec2 turned(Vec2 vector, double angle) noexcept {
            const double COSINE = std::cos(angle);
            const double SINE = std::sin(angle);
            return {
                .x = (vector.x * COSINE) - (vector.y * SINE),
                .y = (vector.x * SINE) + (vector.y * COSINE),
            };
        }

        double wrap_angle(double angle) noexcept {
            return std::remainder(angle, 2.0 * std::numbers::pi);
        }

        double approach(double value, double target, double step) noexcept {
            return value < target ? std::min(target, value + step) : std::max(target, value - step);
        }

        bool finite(const SwarmParams &params) noexcept {
            return std::isfinite(params.speed) && std::isfinite(params.stagger) &&
                   std::isfinite(params.cohesion) && std::isfinite(params.separation) &&
                   std::isfinite(params.responsiveness) && std::isfinite(params.settling) &&
                   std::isfinite(params.wander) && std::isfinite(params.variation);
        }

        SwarmParams sane(SwarmParams params) noexcept {
            params.count = std::clamp(params.count, 1, SWARM_MAX_AGENTS);
            params.speed = std::max(params.speed, 1.0);
            params.stagger = std::clamp(params.stagger, 0.0, STAGGER_MAX);
            params.cohesion = unit(params.cohesion);
            params.separation = unit(params.separation);
            params.responsiveness = unit(params.responsiveness);
            params.settling = unit(params.settling);
            params.wander = unit(params.wander);
            params.variation = unit(params.variation);
            return params;
        }

        bool horizontal(Edge edge) noexcept {
            return edge == Edge::Top || edge == Edge::Bottom;
        }

        // The point `depth` beyond the edge at coordinate `along` on it.
        Vec2 beyond(const Box &screen, Edge edge, double along, double depth) noexcept {
            switch (edge) {
                case Edge::Left:
                    return {.x = screen.x - depth, .y = along};
                case Edge::Right:
                    return {.x = screen.x + screen.w + depth, .y = along};
                case Edge::Top:
                    return {.x = along, .y = screen.y - depth};
                case Edge::Bottom:
                    return {.x = along, .y = screen.y + screen.h + depth};
            }
            return {};
        }

        // Preserve the usual arithmetic; only reorder products/sums that overflow.
        double partition(double extent, int part, int whole) noexcept {
            const double PRODUCT = extent * part;
            return std::isfinite(PRODUCT) ? PRODUCT / whole
                                         : extent * (static_cast<double>(part) / whole);
        }

        double midpoint(double first, double last) noexcept {
            const double SUM = first + last;
            return std::isfinite(SUM) ? 0.5 * SUM : (0.5 * first) + (0.5 * last);
        }

        std::uint32_t bucket(Vec2 position, double cell) noexcept {
            if (!(cell > 0.0) || !std::isfinite(cell)) {
                return NO_CELL;
            }
            const double X = std::floor(position.x / cell);
            const double Y = std::floor(position.y / cell);
            // int64_t's upper bound rounds to 2^63 in double: use an exclusive bound.
            constexpr double LOW = static_cast<double>(std::numeric_limits<std::int64_t>::min());
            constexpr double HIGH = -LOW;
            if (!(X >= LOW && X < HIGH && Y >= LOW && Y < HIGH)) {
                return NO_CELL;
            }
            const auto CELL_X = static_cast<std::uint64_t>(static_cast<std::int64_t>(X)) &
                                static_cast<std::uint64_t>(HASH_SIDE - 1);
            const auto CELL_Y = static_cast<std::uint64_t>(static_cast<std::int64_t>(Y)) &
                                static_cast<std::uint64_t>(HASH_SIDE - 1);
            return static_cast<std::uint32_t>((CELL_Y * HASH_SIDE) + CELL_X);
        }

    }  // namespace

    void Swarm::layout(const SwarmPlacement &placement) {
        const Box &capture = placement.capture;
        m_origin = {.x = capture.x, .y = capture.y};
        m_size = {.x = capture.w, .y = capture.h};
        m_screen = {
            .x = placement.screen.x - capture.x,
            .y = placement.screen.y - capture.y,
            .w = placement.screen.w,
            .h = placement.screen.h,
        };

        // The body, clipped to the capture; a body that is unusable or outside it falls back to the
        // whole capture.
        const double BODY_LEFT = std::clamp(placement.body.x - capture.x, 0.0, capture.w);
        const double BODY_TOP = std::clamp(placement.body.y - capture.y, 0.0, capture.h);
        const double BODY_RIGHT =
            std::clamp(placement.body.x + placement.body.w - capture.x, 0.0, capture.w);
        const double BODY_BOTTOM =
            std::clamp(placement.body.y + placement.body.h - capture.y, 0.0, capture.h);
        m_body = is_usable(placement.body) && BODY_RIGHT - BODY_LEFT >= 1.0 &&
                         BODY_BOTTOM - BODY_TOP >= 1.0
                     ? Box{.x = BODY_LEFT,
                           .y = BODY_TOP,
                           .w = BODY_RIGHT - BODY_LEFT,
                           .h = BODY_BOTTOM - BODY_TOP,}
                     : Box{.x = 0.0, .y = 0.0, .w = capture.w, .h = capture.h};

        const auto COUNT = static_cast<int>(m_agents.size());
        if (m_rows == 0) {
            // Clamp in floating point, before lround/int conversion (also on 32-bit long).
            const double ROWS = std::clamp(
                std::sqrt(COUNT * m_body.h / m_body.w), 1.0, static_cast<double>(COUNT));
            m_rows = static_cast<int>(std::lround(ROWS));
        }
        const int ROWS = m_rows;
        m_spacing = std::sqrt(m_body.w * m_body.h / COUNT);

        // Rows share the agents as evenly as integers allow, so every row's tiles are near square.
        const auto COLUMNS_OF = [&](int row) {
            return ((row + 1) * COUNT / ROWS) - (row * COUNT / ROWS);
        };
        auto agent_it = m_agents.begin();
        for (int tile_row = 0; tile_row < ROWS; ++tile_row) {
            const int COLUMNS = COLUMNS_OF(tile_row);
            const double TOP = m_body.y + partition(m_body.h, tile_row, ROWS);
            const double BOTTOM = m_body.y + partition(m_body.h, tile_row + 1, ROWS);
            for (int col = 0; col < COLUMNS; ++col) {
                const double LEFT = m_body.x + partition(m_body.w, col, COLUMNS);
                const double RIGHT = m_body.x + partition(m_body.w, col + 1, COLUMNS);
                SwarmAgent &agent = *agent_it++;
                // Outermost tiles extend over the decoration margin to the capture's edge.
                const double TX0 = col == 0 ? 0.0 : LEFT;
                const double TX1 = col == COLUMNS - 1 ? m_size.x : RIGHT;
                const double TY0 = tile_row == 0 ? 0.0 : TOP;
                const double TY1 = tile_row == ROWS - 1 ? m_size.y : BOTTOM;
                agent.tile = {.x = TX0, .y = TY0, .w = TX1 - TX0, .h = TY1 - TY0};
                agent.home = {.x = midpoint(LEFT, RIGHT), .y = midpoint(TOP, BOTTOM)};
                agent.fit = 0.5 * std::min(RIGHT - LEFT, BOTTOM - TOP);
                agent.reach = std::hypot(std::max(agent.home.x - TX0, TX1 - agent.home.x),
                    std::max(agent.home.y - TY0, TY1 - agent.home.y));
                if (agent.docked) {
                    agent.position = agent.home;
                }
            }
        }
    }

    void Swarm::build(
        const SwarmParams &params, std::uint64_t seed, const SwarmPlacement &placement) {
        m_params = sane(params);
        m_rng = seed ^ 0x5DEECE66D1CE4E5BULL;
        m_agents.assign(static_cast<std::size_t>(m_params.count), SwarmAgent{});
        m_rows = 0;
        for (SwarmAgent &agent : m_agents) {
            agent.docked = true;
            agent.lock = 1.0;
            agent.visible = true;
            agent.pace_draw = unit_draw(m_rng) - 0.5;
            agent.pace = 1.0 + (m_params.variation * PACE_SPREAD * agent.pace_draw);
            agent.grip_draw = unit_draw(m_rng) - 0.5;
            agent.grip = lerp(GRIP_MIN, GRIP_MAX, m_params.responsiveness) *
                         (1.0 + (m_params.variation * GRIP_SPREAD * agent.grip_draw));
            agent.rest = std::numbers::pi * ((2.0 * unit_draw(m_rng)) - 1.0);
            agent.reaction_draw = unit_draw(m_rng);
            agent.reaction = REACTION_SHARE * m_params.stagger * agent.reaction_draw;
            agent.arc_draw = (2.0 * unit_draw(m_rng)) - 1.0;
            agent.arc = ARC_ANGLE * m_params.variation * agent.arc_draw;
            agent.weave = 2.0 * std::numbers::pi * unit_draw(m_rng);
            agent.weave_rate = lerp(WEAVE_RATE_MIN, WEAVE_RATE_MAX, unit_draw(m_rng));
            agent.gait = 2.0 * std::numbers::pi * unit_draw(m_rng);
            agent.gait_rate = lerp(GAIT_RATE_MIN, GAIT_RATE_MAX, unit_draw(m_rng));
        }
        layout(placement);
        m_shift = {};
        m_window_speed = 0.0;
        m_still = 0.0;
        m_time = 0.0;
        m_accumulator = 0.0;
        m_requested_ms = 0.0;
        m_rounding = 0.0;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Preserve agent scheduling and RNG draw order.
    void Swarm::begin(Transition kind,
        const SwarmParams &params,
        std::uint64_t seed,
        const SwarmPlacement &placement,
        EdgeSet sides,
        double duration_ms) {
        if (!is_usable(placement.capture) || !is_usable(placement.screen) || sides.empty() ||
            !finite(params) ||
            (duration_ms != -1.0 && (!std::isfinite(duration_ms) || duration_ms <= 0.0))) {
            return;
        }

        const bool CLOSING = kind == Transition::Close;
        if (!CLOSING || !active() || m_agents.empty()) {
            build(params, seed, placement);
        } else {
            // A close during an opening or a movement keeps the agents where they are.
            m_params = sane(params);
            m_params.count = static_cast<int>(m_agents.size());
            m_screen = {
                .x = placement.screen.x - m_origin.x,
                .y = placement.screen.y - m_origin.y,
                .w = placement.screen.w,
                .h = placement.screen.h,
            };
            m_shift = {};
            m_time = 0.0;
        }
        if (m_requested_ms > 0.0 || duration_ms > 0.0) {
            m_accumulator = 0.0;
        }
        m_requested_ms = duration_ms > 0.0 ? duration_ms : 0.0;
        m_elapsed_ms = 0.0;
        m_phase = CLOSING ? Phase::Closing : Phase::Opening;
        if (!CLOSING) {
            m_rounding = 1.0;
        }

        const double CRUISE = m_params.speed;
        std::array<Edge, 4> allowed{};
        std::size_t edges = 0;
        auto allowed_it = std::span(allowed).begin();
        for (const Edge CANDIDATE_EDGE : EDGES) {
            if (sides.has(CANDIDATE_EDGE)) {
                *allowed_it = CANDIDATE_EDGE;
                ++allowed_it;
                ++edges;
            }
        }

        double longest = 0.0;
        for (SwarmAgent &entering_agent : m_agents) {
            // A random point beyond a random one of the edges: where an agent comes from, or goes
            // to, says nothing about where its tile is.
            const Edge EDGE = allowed[std::min(edges - 1,
                static_cast<std::size_t>(unit_draw(m_rng) * static_cast<double>(edges)))];
            const bool HORIZONTAL = horizontal(EDGE);
            const double ALONG = (HORIZONTAL ? m_screen.x : m_screen.y) +
                                 ((HORIZONTAL ? m_screen.w : m_screen.h) * unit_draw(m_rng));

            if (CLOSING) {
                if (!entering_agent.visible) {
                    // Never entered: nothing of it is on screen to carry away.
                    entering_agent.gone = true;
                    continue;
                }
                entering_agent.leaving = true;
                entering_agent.due = 0.0;
                entering_agent.exit = beyond(m_screen,
                    EDGE,
                    ALONG,
                    (EXIT_DEPTH * entering_agent.fit) + entering_agent.reach);
                // Agents let go in random order over the stagger; those already loose leave at
                // once.
                entering_agent.delay =
                    entering_agent.docked ? m_params.stagger * unit_draw(m_rng) : 0.0;
                continue;
            }

            entering_agent.position =
                beyond(m_screen, EDGE, ALONG, ENTRY_DEPTH * entering_agent.fit);
            const Vec2 TO_HOME = entering_agent.home - entering_agent.position;
            const double DISTANCE_HOME = length(TO_HOME);
            entering_agent.velocity =
                DISTANCE_HOME > 0.0
                    ? TO_HOME * (ENTRY_SPEED * CRUISE * entering_agent.pace / DISTANCE_HOME)
                    : Vec2{};
            entering_agent.heading = std::atan2(TO_HOME.y, TO_HOME.x);
            entering_agent.turn = 1.0;
            entering_agent.lock = 0.0;
            entering_agent.docked = false;
            entering_agent.visible = false;
            // How long its walk takes, straight and at its cruise; turned into a departure below.
            entering_agent.due = DISTANCE_HOME / (CRUISE * entering_agent.pace);
            longest = std::max(longest, entering_agent.due);
        }

        // An opening agent is due home at a random moment of the stagger, counted from when the one
        // with the longest walk can be there, and sets off as late as its own walk allows. Tiles
        // therefore fill in no order, whether they are near an edge or far from it.
        double first = std::numeric_limits<double>::infinity();
        for (SwarmAgent &waiting_agent : m_agents) {
            if (waiting_agent.gone) {
                continue;
            }
            if (!CLOSING) {
                const double WALK = waiting_agent.due;
                waiting_agent.due = (LEAD * longest) + (m_params.stagger * unit_draw(m_rng));
                waiting_agent.delay = std::max(0.0, waiting_agent.due - WALK);
            }
            first = std::min(first, waiting_agent.delay);
        }
        // The first to go goes now, so the animation never opens on a wait.
        double route_seconds = 0.0;
        for (SwarmAgent &scheduled_agent : m_agents) {
            if (scheduled_agent.gone) {
                continue;
            }
            scheduled_agent.delay -= first;
            if (!CLOSING) {
                scheduled_agent.due -= first;
            }
            if (m_requested_ms > 0.0) {
                const double WALK = length((CLOSING ? scheduled_agent.exit : scheduled_agent.home)
                    - scheduled_agent.position) / (CRUISE * scheduled_agent.pace);
                const double FINISH = scheduled_agent.delay + (WALK / SPEED_CAP) +
                    (CLOSING ? SPLIT_TIME : MERGE_MIN + CORNER_TIME);
                route_seconds = std::max(route_seconds, FINISH);
            }
        }
        if (m_requested_ms > 0.0) {
            // Use a short route estimate for the physics clock. Agents still react to crowds and
            // moving homes, so it is not an endpoint prediction; a timed finish closes the gap.
            m_simulation_seconds =
                std::clamp(route_seconds, STEP, m_params.stagger + CLOSE_DEADLINE);
            if (CLOSING && route_seconds == 0.0) {
                m_phase = Phase::Closed;  // an interrupted open whose agents never entered
            }
        }
    }

    void Swarm::begin_movement(
        const SwarmParams &params, std::uint64_t seed, const SwarmPlacement &placement) {
        if (m_phase != Phase::Idle || !is_usable(placement.capture) ||
            !is_usable(placement.screen) || !finite(params)) {
            return;
        }
        build(params, seed, placement);
        m_phase = Phase::Movement;
    }

    void Swarm::update_movement(const SwarmPlacement &placement) {
        if ((m_phase != Phase::Opening && m_phase != Phase::Movement) ||
            !is_usable(placement.capture) || !is_usable(placement.screen)) {
            return;
        }
        m_shift = m_shift + (Vec2{.x = placement.capture.x, .y = placement.capture.y} - m_origin);
        layout(placement);
    }

    void Swarm::set_movement_params(const SwarmParams &params) noexcept {
        if (m_phase != Phase::Movement || !finite(params)) {
            return;
        }
        SwarmParams next = sane(params);
        next.count = m_params.count;
        if (next == m_params) {
            return;
        }
        m_params = next;
        for (auto &agent : m_agents) {
            agent.pace = 1.0 + (m_params.variation * PACE_SPREAD * agent.pace_draw);
            agent.grip = lerp(GRIP_MIN, GRIP_MAX, m_params.responsiveness) *
                         (1.0 + (m_params.variation * GRIP_SPREAD * agent.grip_draw));
            agent.reaction = REACTION_SHARE * m_params.stagger * agent.reaction_draw;
            agent.arc = ARC_ANGLE * m_params.variation * agent.arc_draw;
        }
    }

    void Swarm::reset() noexcept {
        m_phase = Phase::Idle;
        m_agents.clear();
        m_shift = {};
        m_window_speed = 0.0;
        m_rounding = 0.0;
        m_accumulator = 0.0;
        m_requested_ms = 0.0;
    }

    Box Swarm::bounds() const noexcept {
        const double SCREEN_RIGHT = m_screen.x + m_screen.w;
        const double SCREEN_BOTTOM = m_screen.y + m_screen.h;
        if (!is_usable(m_screen) || !std::isfinite(SCREEN_RIGHT) ||
            !std::isfinite(SCREEN_BOTTOM)) {
            return {};
        }
        double min_x = std::numeric_limits<double>::infinity();
        double min_y = min_x;
        double max_x = -min_x;
        double max_y = -min_x;
        for (const SwarmAgent &agent : m_agents) {
            if (!agent.visible) {
                continue;
            }
            const double LEFT = agent.position.x - agent.reach;
            const double TOP = agent.position.y - agent.reach;
            const double RIGHT = agent.position.x + agent.reach;
            const double BOTTOM = agent.position.y + agent.reach;
            if (!std::isfinite(LEFT) || !std::isfinite(TOP) || !std::isfinite(RIGHT) ||
                !std::isfinite(BOTTOM)) {
                continue;
            }
            min_x = std::min(min_x, LEFT);
            min_y = std::min(min_y, TOP);
            max_x = std::max(max_x, RIGHT);
            max_y = std::max(max_y, BOTTOM);
        }
        min_x = std::max(min_x, m_screen.x);
        min_y = std::max(min_y, m_screen.y);
        max_x = std::min(max_x, SCREEN_RIGHT);
        max_y = std::min(max_y, SCREEN_BOTTOM);
        if (!(max_x > min_x) || !(max_y > min_y)) {
            return {};
        }
        const Box RESULT{.x = min_x, .y = min_y, .w = max_x - min_x, .h = max_y - min_y};
        return is_usable(RESULT) ? RESULT : Box{};
    }

    bool Swarm::advance(double elapsed_seconds, bool movement_held) {
        if (!active()) {
            return false;
        }
        const double ELAPSED = std::isfinite(elapsed_seconds) && elapsed_seconds > 0.0
                                   ? std::min(elapsed_seconds, STALL_SECONDS)
                                   : 0.0;
        const bool TIMED = m_requested_ms > 0.0;
        double delta_time = ELAPSED;
        if (TIMED) {
            const double BEFORE = m_elapsed_ms;
            m_elapsed_ms = std::min(m_requested_ms, m_elapsed_ms + (ELAPSED * 1000.0));
            if (m_elapsed_ms == m_requested_ms) {
                guide_transition(1.0);
                m_phase = opening() ? Phase::Idle : Phase::Closed;
                return false;
            }
            delta_time = ((m_elapsed_ms - BEFORE) / m_requested_ms) * m_simulation_seconds;
        }
        const double DELTA_TIME = delta_time;
        if (!(DELTA_TIME > 0.0)) {
            return true;
        }
        const bool WANDERING = moving() && movement_held && m_params.wander > 0.0;
        if (WANDERING) {
            for (SwarmAgent &wandering_agent : m_agents) {
                wandering_agent.docked = false;
                wandering_agent.pause = 0.0;
                wandering_agent.settle_wait = -1.0;
            }
        }

        const double SPEED = length(m_shift) / ELAPSED;
        if (SPEED > 0.0) {
            // Agents live in the window's frame. Loose agents remain in screen space when their
            // home moves, whether that motion comes from a drag or a workspace slide.
            for (SwarmAgent &agent : m_agents) {
                if (agent.gone || (agent.docked && SPEED <= agent.grip)) {
                    continue;
                }
                agent.position = agent.position - m_shift;
                if (agent.docked) {
                    // It was riding the window. It stays put for its reaction time, then sets off
                    // after the window.
                    agent.docked = false;
                    agent.velocity = {};
                    agent.heading = std::atan2(m_shift.y, m_shift.x);
                    agent.pause = agent.reaction;
                    agent.settle_wait = -1.0;
                }
            }
            m_still = 0.0;
        } else if (!WANDERING && !TIMED) {
            m_still += DELTA_TIME;
        }
        if (WANDERING) {
            m_still = 0.0;
        }
        m_window_speed = std::max(SPEED, m_window_speed *
            std::exp(-ELAPSED / WINDOW_SPEED_HOLD));
        m_shift = {};

        m_accumulator += DELTA_TIME;
        while (m_accumulator >= STEP - ACCUMULATOR_TOLERANCE) {
            if (TIMED) {
                m_still += STEP;
            }
            step(WANDERING);
            if (TIMED) {
                const double BEFORE = smoothstep(0.5, 1.0, (m_time - STEP) / m_simulation_seconds);
                const double NOW = smoothstep(0.5, 1.0, m_time / m_simulation_seconds);
                if (NOW > BEFORE && NOW < 1.0) {
                    guide_transition((NOW - BEFORE) / (1.0 - BEFORE));
                }
            }
            m_accumulator = std::max(0.0, m_accumulator - STEP);
        }
        if (TIMED) {
            if (opening()) {
                // Do not hand off a square window early if every agent happened to dock.
                m_rounding = std::max(m_rounding,
                    1.0 - smoothstep(0.5, 1.0, m_elapsed_ms / m_requested_ms));
            }
            return true;
        }

        if (m_phase == Phase::Closing) {
            if (m_time > m_params.stagger + CLOSE_DEADLINE ||
                std::ranges::all_of(m_agents, [](const SwarmAgent &agent) { return agent.gone; })) {
                m_phase = Phase::Closed;
            }
        } else if (!WANDERING && m_rounding <= 0.0 &&
                   std::ranges::all_of(m_agents, [](const SwarmAgent &agent) {
                       return agent.docked && agent.lock >= 1.0 && agent.turn <= 0.0;
                   })) {
            m_phase = Phase::Idle;
        }
        return active();
    }

    void Swarm::guide_transition(double fraction) {
        // Ease toward the endpoint over the second half, rather than snapping agents that
        // haven't arrived or left by the deadline. The ordinary steering remains live.
        const bool OPENING = opening();
        const bool FINISHED = fraction == 1.0;
        for (auto &agent : m_agents) {
            if (agent.gone) {
                continue;
            }
            if (OPENING && FINISHED) {
                agent.position = agent.home;
                agent.visible = true;
                agent.docked = true;
                agent.velocity = {};
                agent.turn = 0.0;
                agent.lock = 1.0;
            } else if (FINISHED) {
                agent.gone = true;
                agent.visible = false;
            } else if (agent.delay <= 0.0) {
                const Vec2 DESTINATION = OPENING ? agent.home : agent.exit;
                agent.position = {.x = std::lerp(agent.position.x, DESTINATION.x, fraction),
                    .y = std::lerp(agent.position.y, DESTINATION.y, fraction)};
                if (OPENING && length(agent.position - agent.home) < REST_RADIUS * m_spacing) {
                    agent.turn *= 1.0 - fraction;
                    agent.lock = std::lerp(agent.lock, 1.0, fraction);
                }
            }
        }
        if (OPENING) {
            m_rounding *= 1.0 - fraction;
        }
    }

    void Swarm::hash() {
        const double CELL = NEIGHBOUR_RADIUS * m_spacing;
        m_cell_start.assign(HASH_CELLS + 1, 0);
        m_cell_of.assign(m_agents.size(), NO_CELL);
        auto cell = m_cell_of.begin();
        for (const SwarmAgent &agent : m_agents) {
            auto &agent_cell = *cell++;
            if (agent.docked || agent.gone || !agent.visible) {
                continue;
            }
            agent_cell = bucket(agent.position, CELL);
            if (agent_cell != NO_CELL) {
                ++m_cell_start[agent_cell + 1];
            }
        }
        auto previous = m_cell_start.begin();
        auto current = std::next(previous);
        for (int bucket_index = 0; bucket_index < HASH_CELLS;
            ++bucket_index, ++previous, ++current) {
            *current += *previous;
        }
        m_cell_items.resize(m_cell_start.back());
        // Filling from each bucket's end leaves m_cell_start pointing at the bucket starts again.
        auto reverse_cell = m_cell_of.rbegin();
        for (std::size_t i = m_agents.size(); i-- > 0; ++reverse_cell) {
            if (*reverse_cell != NO_CELL) {
                m_cell_items[--m_cell_start[*reverse_cell + 1]] = static_cast<std::uint32_t>(i);
            }
        }
        // Each start slid down one bucket; shift them back.
        auto destination = m_cell_start.begin();
        auto source = std::next(destination);
        for (int cell_index = 0; cell_index < HASH_CELLS; ++cell_index, ++destination, ++source) {
            *destination = *source;
        }
        m_cell_start.back() = static_cast<std::uint32_t>(m_cell_items.size());
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Preserve hot neighbor traversal and FP accumulation order.
    Swarm::Crowd Swarm::crowd(std::size_t index, Vec2 forward, double force) const {
        const SwarmAgent &subject_agent = m_agents[index];
        const double CELL = NEIGHBOUR_RADIUS * m_spacing;
        // Most queries reuse hash()'s cell. Newly visible/undocked agents were not inserted,
        // but still query their neighbours just as before.
        const std::uint32_t CELL_INDEX = m_cell_of[index] != NO_CELL
                                            ? m_cell_of[index]
                                            : bucket(subject_agent.position, CELL);
        if (CELL_INDEX == NO_CELL) {
            return {};
        }
        const double NEAR = SEPARATION_RADIUS * m_spacing;
        const double LOOK = AVOID_RADIUS * m_spacing;
        const double BODY = K_SWARM_BODY_SHARE * subject_agent.fit;
        // How much an agent is still in transit: 0 at its home, where it neither blocks others nor
        // is blocked, so arrivals can settle while the rest of the crowd walks through.
        const auto TRANSIT = [&](const SwarmAgent &agent) {
            return agent.leaving ? 1.0
                                 : smoothstep(TRANSIT_NEAR * m_spacing,
                                       TRANSIT_FAR * m_spacing,
                                       length(agent.home - agent.position));
        };
        const double OWN = TRANSIT(subject_agent);
        const Vec2 RIGHT{.x = -forward.y, .y = forward.x};  // its right-hand side on screen
        const auto CELL_X = static_cast<int>(CELL_INDEX % HASH_SIDE);
        const auto CELL_Y = static_cast<int>(CELL_INDEX / HASH_SIDE);

        Crowd out;
        Vec2 push{};
        Vec2 dodge{};
        Vec2 velocity{};
        Vec2 centre{};
        int neighbours = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                // Add offsets only to bounded wrapped coordinates, never int64_t endpoints.
                const auto BUCKET_INDEX = static_cast<std::size_t>(
                    (((CELL_Y + HASH_SIDE + dy) & (HASH_SIDE - 1)) * HASH_SIDE) +
                    ((CELL_X + HASH_SIDE + dx) & (HASH_SIDE - 1)));
                const std::uint32_t BEGIN = m_cell_start[BUCKET_INDEX];
                const std::uint32_t END = m_cell_start[BUCKET_INDEX + 1];
                for (const std::uint32_t NEIGHBOUR_INDEX :
                    std::span(m_cell_items).subspan(BEGIN, END - BEGIN)) {
                    if (NEIGHBOUR_INDEX == index) {
                        continue;
                    }
                    const SwarmAgent &neighbour = m_agents[NEIGHBOUR_INDEX];
                    const Vec2 AWAY = subject_agent.position - neighbour.position;
                    const double DISTANCE = length(AWAY);
                    if (DISTANCE > CELL) {
                        continue;
                    }
                    // Coincident agents part along a direction fixed by their order.
                    const Vec2 NORMAL =
                        DISTANCE > 1e-6 ? AWAY * (1.0 / DISTANCE)
                                        : Vec2{.x = index < NEIGHBOUR_INDEX ? 1.0 : -1.0, .y = 0.0};
                    const double CLOSING = -dot(subject_agent.velocity - neighbour.velocity,
                        NORMAL);  // px/s the gap is shrinking by

                    // Bodies in transit do not pass through each other: each of the two backs out
                    // of half the overlap and gives up half the speed they were closing with.
                    const double OVERLAP = BODY + (K_SWARM_BODY_SHARE * neighbour.fit) - DISTANCE;
                    const double AHEAD = -dot(NORMAL, forward);
                    const bool IN_WAY = DISTANCE < LOOK && AHEAD > 0.0 && CLOSING > 0.0;
                    const double SOLID =
                        OVERLAP > 0.0 || IN_WAY ? std::min(OWN, TRANSIT(neighbour)) : 0.0;
                    if (OVERLAP > 0.0) {
                        out.shove = out.shove + NORMAL * (0.5 * OVERLAP * SOLID);
                        if (CLOSING > 0.0) {
                            out.check = out.check + NORMAL * (0.5 * CLOSING * SOLID);
                        }
                    }
                    if (DISTANCE < NEAR) {
                        const double SEPARATION_WEIGHT = 1.0 - (DISTANCE / NEAR);
                        push = push + NORMAL * (SEPARATION_WEIGHT * SEPARATION_WEIGHT);
                    }
                    // Someone ahead that it is gaining on: step to the side they are not on. Dead
                    // ahead, step left, so two meeting head-on pass each other.
                    if (IN_WAY) {
                        const double DODGE_WEIGHT =
                            SOLID * (1.0 - (DISTANCE / LOOK)) * AHEAD *
                            std::min(1.0, CLOSING / (AVOID_SPEED * m_params.speed));
                        dodge = dodge +
                                RIGHT * (dot(NORMAL, RIGHT) > 0.0 ? DODGE_WEIGHT : -DODGE_WEIGHT);
                    }
                    velocity = velocity + neighbour.velocity;
                    centre = centre + neighbour.position;
                    ++neighbours;
                }
            }
        }

        out.steer = (push * SEPARATION_FORCE + dodge * AVOID_FORCE) * (force * m_params.separation);
        if (neighbours > 0) {
            const double SHARE = 1.0 / neighbours;
            out.steer = out.steer + (velocity * SHARE - subject_agent.velocity) *
                                        (ALIGN_RATE * m_params.cohesion);
            out.steer = out.steer + (centre * SHARE - subject_agent.position) *
                                        (COHESION_FORCE * force * m_params.cohesion / CELL);
        }
        return out;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Preserve per-agent mutation, RNG and FP order.
    void Swarm::step(bool wandering) {
        m_time += STEP;
        hash();

        const bool CLOSING = m_phase == Phase::Closing;
        const double FORCE = lerp(FORCE_MIN, FORCE_MAX, m_params.responsiveness);
        const double OMEGA = lerp(SPRING_MIN, SPRING_MAX, m_params.settling);
        const double MERGE = lerp(MERGE_MAX, MERGE_MIN, m_params.settling);
        // Patience starts once the last agent has had its turn to set off.
        const double PATIENCE =
            m_params.stagger + lerp(PATIENCE_MAX, PATIENCE_MIN, m_params.settling);
        const double URGENCY =
            CLOSING || wandering ? 0.0 : smoothstep(PATIENCE, PATIENCE + PATIENCE_RAMP, m_still);
        const bool OVERDUE = !CLOSING && !wandering && m_still > PATIENCE + PATIENCE_DEADLINE;
        const double SOLID = std::min(1.0, m_params.separation / CONTACT_FULL) * (1.0 - URGENCY);

        std::size_t agent_counter = 0;
        for (SwarmAgent &moving_agent : m_agents) {
            const std::size_t AGENT_INDEX = agent_counter++;
            if (moving_agent.gone) {
                continue;
            }
            moving_agent.due -= STEP;
            if (moving_agent.delay > 0.0) {
                moving_agent.delay -= STEP;
                if (moving_agent.delay > 0.0) {
                    continue;
                }
                moving_agent.delay = 0.0;
            }
            // Past its delay an opening agent is on its way in, and a closing one lets go of the
            // window.
            moving_agent.visible = true;
            if (moving_agent.leaving && moving_agent.docked) {
                moving_agent.docked = false;
                moving_agent.heading = std::atan2(moving_agent.exit.y - moving_agent.position.y,
                    moving_agent.exit.x - moving_agent.position.x);
            }
            if (OVERDUE && !moving_agent.docked) {
                moving_agent.docked = true;
                moving_agent.visible = true;
                moving_agent.position = moving_agent.home;
                moving_agent.velocity = {};
            }
            if (moving_agent.docked) {
                // Keep the creature alive while its carried tile straightens and fills in.
                if (moving_agent.lock < 1.0) {
                    moving_agent.walk += 4.0 * moving_agent.fit * STEP;
                }
                if (moving_agent.turn > 0.0) {
                    moving_agent.turn = std::max(0.0, moving_agent.turn - (STEP / TURN_TIME));
                } else {
                    moving_agent.lock = std::min(1.0, moving_agent.lock + (STEP / MERGE));
                }
                continue;
            }

            moving_agent.lock = std::max(0.0, moving_agent.lock - (STEP / SPLIT_TIME));
            if (moving_agent.leaving && moving_agent.lock > HOLD_LOCK) {
                continue;
            }

            const double CRUISE = m_params.speed * moving_agent.pace;
            const double LIMIT = CRUISE * FORCE;
            const double PACE = length(moving_agent.velocity);
            const Vec2 FORWARD = PACE > HEADING_SPEED * CRUISE
                                     ? moving_agent.velocity * (1.0 / PACE)
                                     : Vec2{
                                           .x = std::cos(moving_agent.heading),
                                           .y = std::sin(moving_agent.heading),
                                       };
            const Crowd CROWD = this->crowd(AGENT_INDEX, FORWARD, LIMIT);
            moving_agent.position = moving_agent.position + CROWD.shove * SOLID;
            if (moving_agent.pause > 0.0) {
                // Shaken loose and not yet giving chase: the window leaves it behind, and
                // passers-by jostle it.
                moving_agent.pause -= STEP;
                continue;
            }
            moving_agent.velocity = moving_agent.velocity + CROWD.check * SOLID;

            moving_agent.weave += moving_agent.weave_rate * STEP;
            moving_agent.gait += moving_agent.gait_rate * STEP;
            // A moving home keeps the spring walking around the tile throughout a held drag.
            // Its radius scales with wander; the real home is restored immediately on release.
            const double RADIUS = wandering ? m_params.wander * m_spacing : 0.0;
            const Vec2 TARGET =
                moving_agent.leaving
                    ? moving_agent.exit
                    : moving_agent.home + Vec2{
                                              .x = RADIUS * std::cos(moving_agent.weave),
                                              .y = RADIUS * std::sin((0.73 * moving_agent.weave) +
                                                                     moving_agent.gait),
                                          };
            const Vec2 OFFSET = TARGET - moving_agent.position;
            const double DISTANCE = length(OFFSET);
            const Vec2 TOWARD = DISTANCE > 1e-9 ? OFFSET * (1.0 / DISTANCE) : Vec2{};

            // Route, gait and crowd act on the way and fade out near home and as patience runs out.
            const double FADE =
                moving_agent.leaving
                    ? 1.0
                    : smoothstep(ROUTE_NEAR * m_spacing, ROUTE_FAR * m_spacing, DISTANCE) *
                          (1.0 - URGENCY);
            const double SWAY = (std::sin(moving_agent.weave) +
                                    (0.5 * std::sin((2.3 * moving_agent.weave) + 1.7))) /
                                1.5;
            const double OFF =
                std::clamp((moving_agent.arc + (MEANDER_ANGLE * m_params.wander * SWAY)) * FADE,
                    -ROUTE_LIMIT,
                    ROUTE_LIMIT);
            const double HESITATION = smoothstep(GAIT_FROM, GAIT_FULL, std::sin(moving_agent.gait));

            // Walk the route at the gait's speed; arriving agents brake over their stopping
            // distance.
            double want = CRUISE * (1.0 - (GAIT_DEPTH * m_params.wander * HESITATION * FADE));
            if (m_phase ==
                Phase::Opening) {  // on a schedule: hurry or dawdle towards being home when due
                want *= moving_agent.due > STEP
                            ? std::clamp(DISTANCE / (moving_agent.due * CRUISE), DAWDLE, HURRY)
                            : HURRY;
            }
            if (!moving_agent.leaving) {
                want *= std::min(1.0,
                    DISTANCE /
                        ((SLOW_MARGIN * CRUISE * CRUISE / (2.0 * LIMIT)) + (0.5 * m_spacing)));
            }
            Vec2 accel = clamp_length(
                (turned(TOWARD, OFF) * want - moving_agent.velocity) * (1.0 / STEER_TIME), LIMIT);
            accel = accel + CROWD.steer * FADE;

            if (!moving_agent.leaving) {
                const double SETTLE = std::max(
                    1.0 - smoothstep(SPRING_NEAR * m_spacing, SPRING_FAR * m_spacing, DISTANCE),
                    URGENCY);
                if (SETTLE > 0.0) {
                    const Vec2 SPRING = clamp_length(
                        OFFSET * (OMEGA * OMEGA) - moving_agent.velocity * (2.0 * OMEGA),
                        SPRING_FORCE * LIMIT);
                    accel = accel * (1.0 - SETTLE) + SPRING * SETTLE;
                }
            }

            moving_agent.velocity =
                clamp_length(moving_agent.velocity + accel * STEP, SPEED_CAP * CRUISE);
            moving_agent.position = moving_agent.position + moving_agent.velocity * STEP;
            const double SPEED = length(moving_agent.velocity);
            moving_agent.travel += SPEED * STEP;
            // Keep the gait readable even when small agents travel many body lengths per display
            // frame.
            const bool SETTLING =
                !wandering && !moving_agent.leaving && DISTANCE < REST_RADIUS * m_spacing;
            moving_agent.walk +=
                (SETTLING ? 4.0 * moving_agent.fit : std::min(SPEED, 4.0 * moving_agent.fit)) *
                STEP;
            if (SPEED > HEADING_SPEED * CRUISE) {
                const double SWING =
                    wrap_angle(std::atan2(moving_agent.velocity.y, moving_agent.velocity.x) -
                               moving_agent.heading);
                moving_agent.heading = wrap_angle(
                    moving_agent.heading + std::clamp(SWING, -TURN_RATE * STEP, TURN_RATE * STEP));
            }

            if (moving_agent.leaving) {
                moving_agent.turn = approach(moving_agent.turn, 1.0, STEP / TURN_TIME);
                const double MARGIN = GONE_MARGIN * moving_agent.fit;
                if (moving_agent.position.x < m_screen.x - MARGIN ||
                    moving_agent.position.x > m_screen.x + m_screen.w + MARGIN ||
                    moving_agent.position.y < m_screen.y - MARGIN ||
                    moving_agent.position.y > m_screen.y + m_screen.h + MARGIN) {
                    moving_agent.gone = true;
                    moving_agent.visible = false;
                }
                continue;
            }

            const double LEFT = length(moving_agent.home - moving_agent.position);
            moving_agent.turn = approach(moving_agent.turn,
                !wandering && LEFT < REST_RADIUS * m_spacing ? 0.0 : 1.0,
                STEP / TURN_TIME);
            if (!wandering && LEFT < DOCK_DISTANCE && SPEED < DOCK_SPEED &&
                m_window_speed <= DOCK_WINDOW_SPEED * moving_agent.grip) {
                if (m_phase == Phase::Movement && moving_agent.settle_wait < 0.0) {
                    moving_agent.settle_wait = m_params.stagger * unit_draw(m_rng);
                }
                if (m_phase == Phase::Movement) {
                    moving_agent.settle_wait -= STEP;
                }
                if (m_phase != Phase::Movement || moving_agent.settle_wait <= 0.0) {
                    moving_agent.docked = true;
                    moving_agent.position = moving_agent.home;
                    moving_agent.velocity = {};
                }
            }
        }
        const bool WHOLE = std::ranges::all_of(m_agents, [](const SwarmAgent &agent) {
            return agent.docked && agent.lock >= 1.0 && agent.turn <= 0.0;
        });
        if (CLOSING) {
            // Preserve an interrupted opening's surface until the first piece leaves.
            if (!WHOLE) {
                m_rounding = approach(m_rounding, 1.0, STEP / CORNER_TIME);
            }
        } else if (WHOLE && !wandering) {
            m_rounding = approach(m_rounding, 0.0, STEP / CORNER_TIME);
        } else if (m_phase == Phase::Opening) {
            m_rounding = 1.0;
        } else {
            m_rounding = approach(m_rounding, 1.0, STEP / CORNER_TIME);
        }
    }

}  // namespace hyprimate
