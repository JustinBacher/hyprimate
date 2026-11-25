#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>

#include "shared/Core.hpp"
#include "window/swarm/Swarm.hpp"
#include "window/swarm/SwarmDetail.hpp"

namespace hyprimate {
    using swarm_detail::CORNER_TIME;
    using swarm_detail::MERGE_MIN;
    using swarm_detail::REST_RADIUS;
    using swarm_detail::SPEED_CAP;
    using swarm_detail::SPLIT_TIME;
    using swarm_detail::STEP;
    using swarm_detail::length;
    using swarm_detail::lerp;
    using swarm_detail::operator+;
    using swarm_detail::operator-;
    using swarm_detail::operator*;
    using swarm_detail::release_agent;
    namespace {

        constexpr double ACCUMULATOR_TOLERANCE = 1e-9;
        constexpr int MAX_STEPS_PER_ADVANCE = 30;

        // The route. An agent walks in the direction of its target turned by its approach arc and
        // its meander, so it comes in on a wandering curve; both fade out near home, where the turn
        // would otherwise become an orbit.
        constexpr double ARC_ANGLE = 0.9;  // rad either way, at full variation
        constexpr double WEAVE_RATE_MIN = 5.0, WEAVE_RATE_MAX = 11.0;  // rad/s

        // The gait. An agent hesitates for part of every gait cycle, so a column of them bunches
        // and strings out.
        constexpr double GAIT_RATE_MIN = 5.0, GAIT_RATE_MAX = 10.0;  // rad/s

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
        constexpr double LEAD =
            0.85;  // of the longest walk at cruise: when the first agents are due
        constexpr double EXIT_DEPTH = 3.0;  // fits beyond the edge an agent runs to
        constexpr double CLOSE_DEADLINE =
            6.0;  // s after the stagger at which a close ends regardless
        constexpr std::array<Edge, 4> EDGES{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};

        double unit(double value) noexcept {
            return std::clamp(value, 0.0, 1.0);
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
            const double ROWS =
                std::clamp(std::sqrt(COUNT * m_body.h / m_body.w), 1.0, static_cast<double>(COUNT));
            m_rows = static_cast<int>(std::lround(ROWS));
        }
        const int ROWS = m_rows;
        const double AREA = m_body.w * m_body.h;
        m_spacing = std::isfinite(AREA) ? std::sqrt(AREA / COUNT)
                                        : std::sqrt(m_body.w / COUNT) * std::sqrt(m_body.h);

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

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Preserve agent scheduling and RNG
    // draw order.
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
                const double WALK = length((CLOSING ? scheduled_agent.exit : scheduled_agent.home) -
                                           scheduled_agent.position) /
                                    (CRUISE * scheduled_agent.pace);
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
        if (!is_usable(m_screen) || !std::isfinite(SCREEN_RIGHT) || !std::isfinite(SCREEN_BOTTOM)) {
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
        m_window_speed = std::max(SPEED, m_window_speed * std::exp(-ELAPSED / WINDOW_SPEED_HOLD));
        m_shift = {};

        m_accumulator += DELTA_TIME;
        int steps = 0;
        while (steps < MAX_STEPS_PER_ADVANCE && m_accumulator >= STEP - ACCUMULATOR_TOLERANCE) {
            if (TIMED) {
                m_still += STEP;
            }
            step(WANDERING);
            ++steps;
            if (TIMED) {
                const double BEFORE = smoothstep(0.5, 1.0, (m_time - STEP) / m_simulation_seconds);
                const double NOW = smoothstep(0.5, 1.0, m_time / m_simulation_seconds);
                if (NOW > BEFORE && NOW < 1.0) {
                    guide_transition((NOW - BEFORE) / (1.0 - BEFORE));
                }
            }
            m_accumulator = std::max(0.0, m_accumulator - STEP);
        }
        if (steps == MAX_STEPS_PER_ADVANCE && m_accumulator >= STEP - ACCUMULATOR_TOLERANCE) {
            if (TIMED) {
                // Skipped time advances delay gates only; due and steering still reflect actual
                // steps. Newly released agents join absolute guidance below.
                const double SCHEDULED = (m_elapsed_ms / m_requested_ms) * m_simulation_seconds;
                const double SKIPPED_TIME = std::max(0.0, SCHEDULED - m_time);
                for (SwarmAgent &agent : m_agents) {
                    if (agent.gone || agent.delay <= 0.0) {
                        continue;
                    }
                    if (agent.delay > SKIPPED_TIME) {
                        agent.delay -= SKIPPED_TIME;
                    } else {
                        agent.delay = 0.0;
                        release_agent(agent);
                    }
                }
                const double BEFORE = smoothstep(0.5, 1.0, m_time / m_simulation_seconds);
                const double NOW = std::min(
                    smoothstep(0.5, 1.0, m_elapsed_ms / m_requested_ms), std::nextafter(1.0, 0.0));
                if (NOW > BEFORE && BEFORE < 1.0) {
                    guide_transition(
                        std::min((NOW - BEFORE) / (1.0 - BEFORE), std::nextafter(1.0, 0.0)));
                }
                m_time = SCHEDULED;
                m_accumulator = 0.0;
            } else {
                // Untimed transitions have no absolute schedule to guide toward; keep only the
                // fractional remainder so discarded complete steps cannot become a backlog.
                const double REMAINDER = std::fmod(m_accumulator, STEP);
                m_accumulator = REMAINDER >= STEP - ACCUMULATOR_TOLERANCE ? 0.0 : REMAINDER;
            }
        }
        if (TIMED) {
            if (opening()) {
                // Do not hand off a square window early if every agent happened to dock.
                m_rounding =
                    std::max(m_rounding, 1.0 - smoothstep(0.5, 1.0, m_elapsed_ms / m_requested_ms));
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


}  // namespace hyprimate
