#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

#include "animations/shared/Core.hpp"
#include "animations/swarm/Swarm.hpp"

namespace hyprimate {
    namespace {

        constexpr double kStep = 1.0 / 120.0;
        constexpr double kAccumulatorTolerance = 1e-9;

        // Steering. Accelerations are multiples of an agent's cruise speed per second.
        constexpr double kForceMin = 4.0, kForceMax = 14.0;  // by responsiveness
        constexpr double kSteerTime = 0.07;  // s to close a velocity error, before the force limit
        constexpr double kSlowMargin = 1.5;  // braking distance margin for arriving
        constexpr double kSpeedCap = 1.45;  // top speed over cruise
        constexpr double kTurnRate = 14.0;  // rad/s the heading can swing
        constexpr double kHeadingSpeed = 0.04;  // of cruise: slower than this keeps the old heading

        // The route. An agent walks in the direction of its target turned by its approach arc and
        // its meander, so it comes in on a wandering curve; both fade out near home, where the turn
        // would otherwise become an orbit.
        constexpr double kArcAngle = 0.9;  // rad either way, at full variation
        constexpr double kMeanderAngle = 0.9;  // rad either way, at full wander
        constexpr double kRouteLimit =
            1.15;  // rad: it is never turned further than this off its target
        constexpr double kWeaveRateMin = 5.0, kWeaveRateMax = 11.0;  // rad/s
        constexpr double kRouteNear = 0.5,
                         kRouteFar = 2.0;  // spacings from home over which route and crowd fade in

        // The gait. An agent hesitates for part of every gait cycle, so a column of them bunches
        // and strings out.
        constexpr double kGaitDepth = 0.85;  // share of cruise lost in a hesitation, at full wander
        constexpr double kGaitFrom = 0.45,
                         kGaitFull = 0.95;  // sine of the gait phase over which it slows
        constexpr double kGaitRateMin = 5.0, kGaitRateMax = 10.0;  // rad/s

        // The crowd. Radii in tile spacings; force shares of the steering limit.
        constexpr double kNeighbourRadius = 3.0;
        constexpr double kSeparationRadius = 0.8;  // soft push inside this
        constexpr double kSeparationForce = 1.2;
        constexpr double kAvoidRadius = 2.4;  // how far ahead an agent looks for others in its way
        constexpr double kAvoidForce = 1.8;
        constexpr double kAvoidSpeed = 0.35;  // of cruise: closing this fast draws a full sidestep
        constexpr double kContactFull = 0.5;  // separation at which bodies are fully solid
        constexpr double kTransitNear = 1.0,
                         kTransitFar = 5.0;  // spacings from home over which a body becomes solid
        constexpr double kAlignRate = 3.0;  // 1/s velocity matching at full cohesion
        constexpr double kCohesionForce = 0.35;

        // Settling: a critically damped spring takes over near home, and everywhere once patience
        // runs out.
        constexpr double kSpringNear = 0.25, kSpringFar = 1.25;  // spacings
        constexpr double kSpringMin = 9.0, kSpringMax = 26.0;  // rad/s by settling
        constexpr double kSpringForce = 3.0;  // spring force limit over the steering limit
        constexpr double kPatienceMax = 2.4,
                         kPatienceMin =
                             0.5;  // s of a still window, after the stagger, before hurrying
        constexpr double kPatienceRamp = 0.8;
        constexpr double kPatienceDeadline =
            4.0;  // s after patience at which every agent is placed
        constexpr double kDockDistance = 1.5;  // px
        constexpr double kDockSpeed = 60.0;  // px/s
        constexpr double kDockWindowSpeed =
            0.5;  // of grip: the window must be slower than this to dock on it
        constexpr double kRestRadius =
            0.7;  // spacings from home inside which an agent swings to its rest direction

        // Formation lock.
        constexpr double kMergeMax = 0.5,
                         kMergeMin = 0.18;  // s to merge into the window, by settling
        constexpr double kSplitTime = 0.14;  // s to shrink from tile to creature
        constexpr double kTurnTime = 0.16;  // s to swing between rest direction and heading
        constexpr double kHoldLock = 0.35;  // a leaving agent waits until its lock is below this
        constexpr double kCornerTime = 0.18;  // s for the global surface rounding to change

        // Per-agent spread.
        constexpr double kGripMin = 60.0, kGripMax = 900.0;  // px/s by responsiveness
        constexpr double kPaceSpread = 0.7;  // total, at full variation
        constexpr double kGripSpread = 1.0;
        constexpr double kWindowSpeedHold = 0.12;  // s the window speed estimate lingers

        // Departures, as shares of the stagger.
        constexpr double kReactionShare =
            0.5;  // the longest an agent shaken loose by movement stays put
        constexpr double kStaggerMax = 5.0;  // s

        // Transitions.
        constexpr double kEntryDepth = 1.5;  // fits beyond the edge an agent starts
        constexpr double kEntrySpeed = 0.5;  // of cruise
        constexpr double
            kDawdle = 0.7,
            kHurry = 1.3;  // of cruise: how far an opening agent bends its speed to be on time
        constexpr double kLead =
            0.85;  // of the longest walk at cruise: when the first agents are due
        constexpr double kExitDepth = 3.0;  // fits beyond the edge an agent runs to
        constexpr double kGoneMargin = 1.2;  // fits beyond the edge at which it has left
        constexpr double kCloseDeadline =
            6.0;  // s after the stagger at which a close ends regardless

        constexpr int kHashSide =
            32;  // buckets per axis; positions wrap and the distance check rejects aliases
        constexpr int kHashCells = kHashSide * kHashSide;
        constexpr std::uint32_t kNoCell = std::numeric_limits<std::uint32_t>::max();

        constexpr std::array<Edge, 4> kEdges{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};

        Vec2 operator+(Vec2 a, Vec2 b) noexcept {
            return {.x = a.x + b.x, .y = a.y + b.y};
        }
        Vec2 operator-(Vec2 a, Vec2 b) noexcept {
            return {.x = a.x - b.x, .y = a.y - b.y};
        }
        Vec2 operator*(Vec2 a, double s) noexcept {
            return {.x = a.x * s, .y = a.y * s};
        }
        double dot(Vec2 a, Vec2 b) noexcept {
            return (a.x * b.x) + (a.y * b.y);
        }
        double length(Vec2 a) noexcept {
            return std::hypot(a.x, a.y);
        }
        double lerp(double a, double b, double t) noexcept {
            return a + ((b - a) * t);
        }
        double unit(double v) noexcept {
            return std::clamp(v, 0.0, 1.0);
        }

        Vec2 clampLength(Vec2 a, double limit) noexcept {
            const double LENGTH = length(a);
            return LENGTH > limit && LENGTH > 0.0 ? a * (limit / LENGTH) : a;
        }

        // `a` turned clockwise on screen (y down) by `angle`.
        Vec2 turned(Vec2 a, double angle) noexcept {
            const double COSINE = std::cos(angle);
            const double SINE = std::sin(angle);
            return {.x = (a.x * COSINE) - (a.y * SINE), .y = (a.x * SINE) + (a.y * COSINE)};
        }

        double wrapAngle(double a) noexcept {
            return std::remainder(a, 2.0 * std::numbers::pi);
        }

        double approach(double value, double target, double step) noexcept {
            return value < target ? std::min(target, value + step) : std::max(target, value - step);
        }

        bool finite(const SwarmParams &p) noexcept {
            return std::isfinite(p.speed) && std::isfinite(p.stagger) &&
                   std::isfinite(p.cohesion) && std::isfinite(p.separation) &&
                   std::isfinite(p.responsiveness) && std::isfinite(p.settling) &&
                   std::isfinite(p.wander) && std::isfinite(p.variation);
        }

        SwarmParams sane(SwarmParams p) noexcept {
            p.count = std::clamp(p.count, 1, kSwarmMaxAgents);
            p.speed = std::max(p.speed, 1.0);
            p.stagger = std::clamp(p.stagger, 0.0, kStaggerMax);
            p.cohesion = unit(p.cohesion);
            p.separation = unit(p.separation);
            p.responsiveness = unit(p.responsiveness);
            p.settling = unit(p.settling);
            p.wander = unit(p.wander);
            p.variation = unit(p.variation);
            return p;
        }

        bool horizontal(Edge e) noexcept {
            return e == Edge::Top || e == Edge::Bottom;
        }

        // The point `depth` beyond the edge at coordinate `along` on it.
        Vec2 beyond(const Box &s, Edge e, double along, double depth) noexcept {
            switch (e) {
                case Edge::Left:
                    return {.x = s.x - depth, .y = along};
                case Edge::Right:
                    return {.x = s.x + s.w + depth, .y = along};
                case Edge::Top:
                    return {.x = along, .y = s.y - depth};
                case Edge::Bottom:
                    return {.x = along, .y = s.y + s.h + depth};
            }
            return {};
        }

        std::uint32_t bucket(Vec2 p, double cell) noexcept {
            const auto CELL_X = static_cast<std::int64_t>(std::floor(p.x / cell)) & (kHashSide - 1);
            const auto CELL_Y = static_cast<std::int64_t>(std::floor(p.y / cell)) & (kHashSide - 1);
            return static_cast<std::uint32_t>((CELL_Y * kHashSide) + CELL_X);
        }

    }  // namespace

    void Swarm::layout(const SwarmPlacement &placement) {
        const Box &capture = placement.capture;
        m_origin = {.x = capture.x, .y = capture.y};
        m_size = {.x = capture.w, .y = capture.h};
        m_screen = {.x = placement.screen.x - capture.x,
            .y = placement.screen.y - capture.y,
            .w = placement.screen.w,
            .h = placement.screen.h};

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
                           .h = BODY_BOTTOM - BODY_TOP}
                     : Box{.x = 0.0, .y = 0.0, .w = capture.w, .h = capture.h};

        const auto COUNT = static_cast<int>(m_agents.size());
        if (m_rows == 0) {
            m_rows = std::clamp(
                static_cast<int>(std::lround(std::sqrt(COUNT * m_body.h / m_body.w))), 1, COUNT);
        }
        const int ROWS = m_rows;
        m_spacing = std::sqrt(m_body.w * m_body.h / COUNT);

        // Rows share the agents as evenly as integers allow, so every row's tiles are near square.
        const auto COLUMNS_OF = [&](int r) {
            return ((r + 1) * COUNT / ROWS) - (r * COUNT / ROWS);
        };
        int index = 0;
        for (int tileRow = 0; tileRow < ROWS; ++tileRow) {
            const int COLUMNS = COLUMNS_OF(tileRow);
            const double TOP = m_body.y + (m_body.h * tileRow / ROWS);
            const double BOTTOM = m_body.y + (m_body.h * (tileRow + 1) / ROWS);
            for (int col = 0; col < COLUMNS; ++col) {
                const double LEFT = m_body.x + (m_body.w * col / COLUMNS);
                const double RIGHT = m_body.x + (m_body.w * (col + 1) / COLUMNS);
                SwarmAgent &agent = m_agents[static_cast<std::size_t>(index++)];
                // Outermost tiles extend over the decoration margin to the capture's edge.
                const double TX0 = col == 0 ? 0.0 : LEFT;
                const double TX1 = col == COLUMNS - 1 ? m_size.x : RIGHT;
                const double TY0 = tileRow == 0 ? 0.0 : TOP;
                const double TY1 = tileRow == ROWS - 1 ? m_size.y : BOTTOM;
                agent.tile = {.x = TX0, .y = TY0, .w = TX1 - TX0, .h = TY1 - TY0};
                agent.home = {.x = 0.5 * (LEFT + RIGHT), .y = 0.5 * (TOP + BOTTOM)};
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
            agent.pace = 1.0 + (m_params.variation * kPaceSpread * (unit_draw(m_rng) - 0.5));
            agent.grip = lerp(kGripMin, kGripMax, m_params.responsiveness) *
                         (1.0 + (m_params.variation * kGripSpread * (unit_draw(m_rng) - 0.5)));
            agent.rest = std::numbers::pi * ((2.0 * unit_draw(m_rng)) - 1.0);
            agent.reaction = kReactionShare * m_params.stagger * unit_draw(m_rng);
            agent.arc = kArcAngle * m_params.variation * ((2.0 * unit_draw(m_rng)) - 1.0);
            agent.weave = 2.0 * std::numbers::pi * unit_draw(m_rng);
            agent.weaveRate = lerp(kWeaveRateMin, kWeaveRateMax, unit_draw(m_rng));
            agent.gait = 2.0 * std::numbers::pi * unit_draw(m_rng);
            agent.gaitRate = lerp(kGaitRateMin, kGaitRateMax, unit_draw(m_rng));
        }
        layout(placement);
        m_shift = {};
        m_windowSpeed = 0.0;
        m_still = 0.0;
        m_time = 0.0;
        m_accumulator = 0.0;
        m_rounding = 0.0;
    }

    void Swarm::begin(Transition kind,
        const SwarmParams &params,
        std::uint64_t seed,
        const SwarmPlacement &placement,
        EdgeSet sides) {
        if (!is_usable(placement.capture) || !is_usable(placement.screen) || sides.empty() ||
            !finite(params)) {
            return;
        }

        const bool CLOSING = kind == Transition::Close;
        if (!CLOSING || !active() || m_agents.empty()) {
            build(params, seed, placement);
        } else {
            // A close during an opening or a movement keeps the agents where they are.
            m_params = sane(params);
            m_params.count = static_cast<int>(m_agents.size());
            m_screen = {.x = placement.screen.x - m_origin.x,
                .y = placement.screen.y - m_origin.y,
                .w = placement.screen.w,
                .h = placement.screen.h};
            m_shift = {};
            m_time = 0.0;
        }
        m_phase = CLOSING ? Phase::Closing : Phase::Opening;
        if (!CLOSING) {
            m_rounding = 1.0;
        }

        const double CRUISE = m_params.speed;
        std::array<Edge, 4> allowed{};
        std::size_t edges = 0;
        for (const Edge CANDIDATE_EDGE : kEdges) {
            if (sides.has(CANDIDATE_EDGE)) {
                allowed[edges++] = CANDIDATE_EDGE;
            }
        }

        double longest = 0.0;
        for (SwarmAgent &enteringAgent : m_agents) {
            // A random point beyond a random one of the edges: where an agent comes from, or goes
            // to, says nothing about where its tile is.
            const Edge EDGE =
                allowed[std::min(edges - 1, static_cast<std::size_t>(unit_draw(m_rng) * edges))];
            const bool HORIZONTAL = horizontal(EDGE);
            const double ALONG = (HORIZONTAL ? m_screen.x : m_screen.y) +
                                 ((HORIZONTAL ? m_screen.w : m_screen.h) * unit_draw(m_rng));

            if (CLOSING) {
                if (!enteringAgent.visible) {
                    // Never entered: nothing of it is on screen to carry away.
                    enteringAgent.gone = true;
                    continue;
                }
                enteringAgent.leaving = true;
                enteringAgent.due = 0.0;
                enteringAgent.exit = beyond(
                    m_screen, EDGE, ALONG, (kExitDepth * enteringAgent.fit) + enteringAgent.reach);
                // Agents let go in random order over the stagger; those already loose leave at
                // once.
                enteringAgent.delay =
                    enteringAgent.docked ? m_params.stagger * unit_draw(m_rng) : 0.0;
                continue;
            }

            enteringAgent.position = beyond(m_screen, EDGE, ALONG, kEntryDepth * enteringAgent.fit);
            const Vec2 TO_HOME = enteringAgent.home - enteringAgent.position;
            const double DISTANCE_HOME = length(TO_HOME);
            enteringAgent.velocity =
                DISTANCE_HOME > 0.0
                    ? TO_HOME * (kEntrySpeed * CRUISE * enteringAgent.pace / DISTANCE_HOME)
                    : Vec2{};
            enteringAgent.heading = std::atan2(TO_HOME.y, TO_HOME.x);
            enteringAgent.turn = 1.0;
            enteringAgent.lock = 0.0;
            enteringAgent.docked = false;
            enteringAgent.visible = false;
            // How long its walk takes, straight and at its cruise; turned into a departure below.
            enteringAgent.due = DISTANCE_HOME / (CRUISE * enteringAgent.pace);
            longest = std::max(longest, enteringAgent.due);
        }

        // An opening agent is due home at a random moment of the stagger, counted from when the one
        // with the longest walk can be there, and sets off as late as its own walk allows. Tiles
        // therefore fill in no order, whether they are near an edge or far from it.
        double first = std::numeric_limits<double>::infinity();
        for (SwarmAgent &waitingAgent : m_agents) {
            if (waitingAgent.gone) {
                continue;
            }
            if (!CLOSING) {
                const double WALK = waitingAgent.due;
                waitingAgent.due = (kLead * longest) + (m_params.stagger * unit_draw(m_rng));
                waitingAgent.delay = std::max(0.0, waitingAgent.due - WALK);
            }
            first = std::min(first, waitingAgent.delay);
        }
        // The first to go goes now, so the animation never opens on a wait.
        for (SwarmAgent &scheduledAgent : m_agents) {
            if (scheduledAgent.gone) {
                continue;
            }
            scheduledAgent.delay -= first;
            if (!CLOSING) {
                scheduledAgent.due -= first;
            }
        }
    }

    void Swarm::beginMovement(
        const SwarmParams &params, std::uint64_t seed, const SwarmPlacement &placement) {
        if (m_phase != Phase::Idle || !is_usable(placement.capture) ||
            !is_usable(placement.screen) || !finite(params)) {
            return;
        }
        build(params, seed, placement);
        m_phase = Phase::Movement;
    }

    void Swarm::updateMovement(const SwarmPlacement &placement) {
        if ((m_phase != Phase::Opening && m_phase != Phase::Movement) ||
            !is_usable(placement.capture) || !is_usable(placement.screen)) {
            return;
        }
        m_shift = m_shift + (Vec2{.x = placement.capture.x, .y = placement.capture.y} - m_origin);
        layout(placement);
    }

    void Swarm::reset() noexcept {
        m_phase = Phase::Idle;
        m_agents.clear();
        m_shift = {};
        m_windowSpeed = 0.0;
        m_rounding = 0.0;
        m_accumulator = 0.0;
    }

    Box Swarm::bounds() const noexcept {
        double minX = std::numeric_limits<double>::infinity();
        double minY = minX;
        double maxX = -minX;
        double maxY = -minX;
        for (const SwarmAgent &agent : m_agents) {
            if (!agent.visible) {
                continue;
            }
            minX = std::min(minX, agent.position.x - agent.reach);
            minY = std::min(minY, agent.position.y - agent.reach);
            maxX = std::max(maxX, agent.position.x + agent.reach);
            maxY = std::max(maxY, agent.position.y + agent.reach);
        }
        minX = std::max(minX, m_screen.x);
        minY = std::max(minY, m_screen.y);
        maxX = std::min(maxX, m_screen.x + m_screen.w);
        maxY = std::min(maxY, m_screen.y + m_screen.h);
        if (!(maxX > minX && maxY > minY)) {
            return {};
        }
        return {.x = minX, .y = minY, .w = maxX - minX, .h = maxY - minY};
    }

    bool Swarm::advance(double elapsedSeconds, bool movementHeld) {
        if (!active()) {
            return false;
        }
        const double DELTA_TIME = std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0
                                      ? std::min(elapsedSeconds, STALL_SECONDS)
                                      : 0.0;
        if (!(DELTA_TIME > 0.0)) {
            return true;
        }
        const bool WANDERING = moving() && movementHeld && m_params.wander > 0.0;
        if (WANDERING) {
            for (SwarmAgent &wanderingAgent : m_agents) {
                wanderingAgent.docked = false;
                wanderingAgent.pause = 0.0;
                wanderingAgent.settleWait = -1.0;
            }
        }

        const double SPEED = length(m_shift) / DELTA_TIME;
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
                    agent.settleWait = -1.0;
                }
            }
            m_still = 0.0;
        } else if (!WANDERING) {
            m_still += DELTA_TIME;
        }
        if (WANDERING) {
            m_still = 0.0;
        }
        m_windowSpeed = std::max(SPEED, m_windowSpeed * std::exp(-DELTA_TIME / kWindowSpeedHold));
        m_shift = {};

        m_accumulator += DELTA_TIME;
        while (m_accumulator >= kStep - kAccumulatorTolerance) {
            step(WANDERING);
            m_accumulator = std::max(0.0, m_accumulator - kStep);
        }

        if (m_phase == Phase::Closing) {
            if (m_time > m_params.stagger + kCloseDeadline ||
                std::ranges::all_of(m_agents, [](const SwarmAgent &a) { return a.gone; })) {
                m_phase = Phase::Closed;
            }
        } else if (!WANDERING && m_rounding <= 0.0 &&
                   std::ranges::all_of(m_agents, [](const SwarmAgent &a) {
                       return a.docked && a.lock >= 1.0 && a.turn <= 0.0;
                   })) {
            m_phase = Phase::Idle;
        }
        return active();
    }

    void Swarm::hash() {
        const double CELL = kNeighbourRadius * m_spacing;
        m_cellStart.assign(kHashCells + 1, 0);
        m_cellOf.assign(m_agents.size(), kNoCell);
        for (std::size_t i = 0; i < m_agents.size(); ++i) {
            const SwarmAgent &agent = m_agents[i];
            if (agent.docked || agent.gone || !agent.visible) {
                continue;
            }
            m_cellOf[i] = bucket(agent.position, CELL);
            ++m_cellStart[m_cellOf[i] + 1];
        }
        for (int bucketIndex = 0; bucketIndex < kHashCells; ++bucketIndex) {
            m_cellStart[bucketIndex + 1] += m_cellStart[bucketIndex];
        }
        m_cellItems.resize(m_cellStart[kHashCells]);
        // Filling from each bucket's end leaves m_cellStart pointing at the bucket starts again.
        for (std::size_t i = m_agents.size(); i-- > 0;) {
            if (m_cellOf[i] != kNoCell) {
                m_cellItems[--m_cellStart[m_cellOf[i] + 1]] = static_cast<std::uint32_t>(i);
            }
        }
        // Each start slid down one bucket; shift them back.
        for (int cellIndex = 0; cellIndex < kHashCells; ++cellIndex) {
            m_cellStart[cellIndex] = m_cellStart[cellIndex + 1];
        }
        m_cellStart[kHashCells] = static_cast<std::uint32_t>(m_cellItems.size());
    }

    Swarm::Crowd Swarm::crowd(std::size_t index, Vec2 forward, double force) const {
        const SwarmAgent &subjectAgent = m_agents[index];
        const double CELL = kNeighbourRadius * m_spacing;
        const double NEAR = kSeparationRadius * m_spacing;
        const double LOOK = kAvoidRadius * m_spacing;
        const double BODY = kSwarmBodyShare * subjectAgent.fit;
        // How much an agent is still in transit: 0 at its home, where it neither blocks others nor
        // is blocked, so arrivals can settle while the rest of the crowd walks through.
        const auto TRANSIT = [&](const SwarmAgent &agent) {
            return agent.leaving ? 1.0
                                 : smoothstep(kTransitNear * m_spacing,
                                       kTransitFar * m_spacing,
                                       length(agent.home - agent.position));
        };
        const double OWN = TRANSIT(subjectAgent);
        const Vec2 RIGHT{.x = -forward.y, .y = forward.x};  // its right-hand side on screen
        const auto CELL_X = static_cast<std::int64_t>(std::floor(subjectAgent.position.x / CELL));
        const auto CELL_Y = static_cast<std::int64_t>(std::floor(subjectAgent.position.y / CELL));

        Crowd out;
        Vec2 push{};
        Vec2 dodge{};
        Vec2 velocity{};
        Vec2 centre{};
        int neighbours = 0;
        for (std::int64_t dy = -1; dy <= 1; ++dy) {
            for (std::int64_t dx = -1; dx <= 1; ++dx) {
                const auto BUCKET_INDEX =
                    static_cast<std::size_t>((((CELL_Y + dy) & (kHashSide - 1)) * kHashSide) +
                                             ((CELL_X + dx) & (kHashSide - 1)));
                const std::uint32_t BEGIN = m_cellStart[BUCKET_INDEX];
                const std::uint32_t END = m_cellStart[BUCKET_INDEX + 1];
                for (std::uint32_t k = BEGIN; k < END; ++k) {
                    const std::size_t NEIGHBOUR_INDEX = m_cellItems[k];
                    if (NEIGHBOUR_INDEX == index) {
                        continue;
                    }
                    const SwarmAgent &neighbour = m_agents[NEIGHBOUR_INDEX];
                    const Vec2 AWAY = subjectAgent.position - neighbour.position;
                    const double DISTANCE = length(AWAY);
                    if (DISTANCE > CELL) {
                        continue;
                    }
                    // Coincident agents part along a direction fixed by their order.
                    const Vec2 NORMAL =
                        DISTANCE > 1e-6 ? AWAY * (1.0 / DISTANCE)
                                        : Vec2{.x = index < NEIGHBOUR_INDEX ? 1.0 : -1.0, .y = 0.0};
                    const double CLOSING = -dot(subjectAgent.velocity - neighbour.velocity,
                        NORMAL);  // px/s the gap is shrinking by

                    // Bodies in transit do not pass through each other: each of the two backs out
                    // of half the overlap and gives up half the speed they were closing with.
                    const double OVERLAP = BODY + (kSwarmBodyShare * neighbour.fit) - DISTANCE;
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
                            std::min(1.0, CLOSING / (kAvoidSpeed * m_params.speed));
                        dodge = dodge +
                                RIGHT * (dot(NORMAL, RIGHT) > 0.0 ? DODGE_WEIGHT : -DODGE_WEIGHT);
                    }
                    velocity = velocity + neighbour.velocity;
                    centre = centre + neighbour.position;
                    ++neighbours;
                }
            }
        }

        out.steer = (push * kSeparationForce + dodge * kAvoidForce) * (force * m_params.separation);
        if (neighbours > 0) {
            const double SHARE = 1.0 / neighbours;
            out.steer = out.steer + (velocity * SHARE - subjectAgent.velocity) *
                                        (kAlignRate * m_params.cohesion);
            out.steer = out.steer + (centre * SHARE - subjectAgent.position) *
                                        (kCohesionForce * force * m_params.cohesion / CELL);
        }
        return out;
    }

    void Swarm::step(bool wandering) {
        m_time += kStep;
        hash();

        const bool CLOSING = m_phase == Phase::Closing;
        const double FORCE = lerp(kForceMin, kForceMax, m_params.responsiveness);
        const double OMEGA = lerp(kSpringMin, kSpringMax, m_params.settling);
        const double MERGE = lerp(kMergeMax, kMergeMin, m_params.settling);
        // Patience starts once the last agent has had its turn to set off.
        const double PATIENCE =
            m_params.stagger + lerp(kPatienceMax, kPatienceMin, m_params.settling);
        const double URGENCY =
            CLOSING || wandering ? 0.0 : smoothstep(PATIENCE, PATIENCE + kPatienceRamp, m_still);
        const bool OVERDUE = !CLOSING && !wandering && m_still > PATIENCE + kPatienceDeadline;
        const double SOLID = std::min(1.0, m_params.separation / kContactFull) * (1.0 - URGENCY);

        for (std::size_t i = 0; i < m_agents.size(); ++i) {
            SwarmAgent &movingAgent = m_agents[i];
            if (movingAgent.gone) {
                continue;
            }
            movingAgent.due -= kStep;
            if (movingAgent.delay > 0.0) {
                movingAgent.delay -= kStep;
                if (movingAgent.delay > 0.0) {
                    continue;
                }
                movingAgent.delay = 0.0;
            }
            // Past its delay an opening agent is on its way in, and a closing one lets go of the
            // window.
            movingAgent.visible = true;
            if (movingAgent.leaving && movingAgent.docked) {
                movingAgent.docked = false;
                movingAgent.heading = std::atan2(movingAgent.exit.y - movingAgent.position.y,
                    movingAgent.exit.x - movingAgent.position.x);
            }
            if (OVERDUE && !movingAgent.docked) {
                movingAgent.docked = true;
                movingAgent.visible = true;
                movingAgent.position = movingAgent.home;
                movingAgent.velocity = {};
            }
            if (movingAgent.docked) {
                // Keep the creature alive while its carried tile straightens and fills in.
                if (movingAgent.lock < 1.0) {
                    movingAgent.walk += 4.0 * movingAgent.fit * kStep;
                }
                if (movingAgent.turn > 0.0) {
                    movingAgent.turn = std::max(0.0, movingAgent.turn - (kStep / kTurnTime));
                } else {
                    movingAgent.lock = std::min(1.0, movingAgent.lock + (kStep / MERGE));
                }
                continue;
            }

            movingAgent.lock = std::max(0.0, movingAgent.lock - (kStep / kSplitTime));
            if (movingAgent.leaving && movingAgent.lock > kHoldLock) {
                continue;
            }

            const double CRUISE = m_params.speed * movingAgent.pace;
            const double LIMIT = CRUISE * FORCE;
            const double PACE = length(movingAgent.velocity);
            const Vec2 FORWARD =
                PACE > kHeadingSpeed * CRUISE
                    ? movingAgent.velocity * (1.0 / PACE)
                    : Vec2{.x = std::cos(movingAgent.heading), .y = std::sin(movingAgent.heading)};
            const Crowd CROWD = this->crowd(i, FORWARD, LIMIT);
            movingAgent.position = movingAgent.position + CROWD.shove * SOLID;
            if (movingAgent.pause > 0.0) {
                // Shaken loose and not yet giving chase: the window leaves it behind, and
                // passers-by jostle it.
                movingAgent.pause -= kStep;
                continue;
            }
            movingAgent.velocity = movingAgent.velocity + CROWD.check * SOLID;

            movingAgent.weave += movingAgent.weaveRate * kStep;
            movingAgent.gait += movingAgent.gaitRate * kStep;
            // A moving home keeps the spring walking around the tile throughout a held drag.
            // Its radius scales with wander; the real home is restored immediately on release.
            const double RADIUS = wandering ? m_params.wander * m_spacing : 0.0;
            const Vec2 TARGET =
                movingAgent.leaving
                    ? movingAgent.exit
                    : movingAgent.home + Vec2{.x = RADIUS * std::cos(movingAgent.weave),
                                             .y = RADIUS * std::sin((0.73 * movingAgent.weave) +
                                                                    movingAgent.gait)};
            const Vec2 OFFSET = TARGET - movingAgent.position;
            const double DISTANCE = length(OFFSET);
            const Vec2 TOWARD = DISTANCE > 1e-9 ? OFFSET * (1.0 / DISTANCE) : Vec2{};

            // Route, gait and crowd act on the way and fade out near home and as patience runs out.
            const double FADE =
                movingAgent.leaving
                    ? 1.0
                    : smoothstep(kRouteNear * m_spacing, kRouteFar * m_spacing, DISTANCE) *
                          (1.0 - URGENCY);
            const double SWAY =
                (std::sin(movingAgent.weave) + (0.5 * std::sin((2.3 * movingAgent.weave) + 1.7))) /
                1.5;
            const double OFF =
                std::clamp((movingAgent.arc + (kMeanderAngle * m_params.wander * SWAY)) * FADE,
                    -kRouteLimit,
                    kRouteLimit);
            const double HESITATION = smoothstep(kGaitFrom, kGaitFull, std::sin(movingAgent.gait));

            // Walk the route at the gait's speed; arriving agents brake over their stopping
            // distance.
            double want = CRUISE * (1.0 - (kGaitDepth * m_params.wander * HESITATION * FADE));
            if (m_phase ==
                Phase::Opening) {  // on a schedule: hurry or dawdle towards being home when due
                want *= movingAgent.due > kStep
                            ? std::clamp(DISTANCE / (movingAgent.due * CRUISE), kDawdle, kHurry)
                            : kHurry;
            }
            if (!movingAgent.leaving) {
                want *= std::min(1.0,
                    DISTANCE /
                        ((kSlowMargin * CRUISE * CRUISE / (2.0 * LIMIT)) + (0.5 * m_spacing)));
            }
            Vec2 accel = clampLength(
                (turned(TOWARD, OFF) * want - movingAgent.velocity) * (1.0 / kSteerTime), LIMIT);
            accel = accel + CROWD.steer * FADE;

            if (!movingAgent.leaving) {
                const double SETTLE = std::max(
                    1.0 - smoothstep(kSpringNear * m_spacing, kSpringFar * m_spacing, DISTANCE),
                    URGENCY);
                if (SETTLE > 0.0) {
                    const Vec2 SPRING =
                        clampLength(OFFSET * (OMEGA * OMEGA) - movingAgent.velocity * (2.0 * OMEGA),
                            kSpringForce * LIMIT);
                    accel = accel * (1.0 - SETTLE) + SPRING * SETTLE;
                }
            }

            movingAgent.velocity =
                clampLength(movingAgent.velocity + accel * kStep, kSpeedCap * CRUISE);
            movingAgent.position = movingAgent.position + movingAgent.velocity * kStep;
            const double SPEED = length(movingAgent.velocity);
            movingAgent.travel += SPEED * kStep;
            // Keep the gait readable even when small agents travel many body lengths per display
            // frame.
            const bool SETTLING =
                !wandering && !movingAgent.leaving && DISTANCE < kRestRadius * m_spacing;
            movingAgent.walk +=
                (SETTLING ? 4.0 * movingAgent.fit : std::min(SPEED, 4.0 * movingAgent.fit)) * kStep;
            if (SPEED > kHeadingSpeed * CRUISE) {
                const double SWING =
                    wrapAngle(std::atan2(movingAgent.velocity.y, movingAgent.velocity.x) -
                              movingAgent.heading);
                movingAgent.heading = wrapAngle(
                    movingAgent.heading + std::clamp(SWING, -kTurnRate * kStep, kTurnRate * kStep));
            }

            if (movingAgent.leaving) {
                movingAgent.turn = approach(movingAgent.turn, 1.0, kStep / kTurnTime);
                const double MARGIN = kGoneMargin * movingAgent.fit;
                if (movingAgent.position.x < m_screen.x - MARGIN ||
                    movingAgent.position.x > m_screen.x + m_screen.w + MARGIN ||
                    movingAgent.position.y < m_screen.y - MARGIN ||
                    movingAgent.position.y > m_screen.y + m_screen.h + MARGIN) {
                    movingAgent.gone = true;
                    movingAgent.visible = false;
                }
                continue;
            }

            const double LEFT = length(movingAgent.home - movingAgent.position);
            movingAgent.turn = approach(movingAgent.turn,
                !wandering && LEFT < kRestRadius * m_spacing ? 0.0 : 1.0,
                kStep / kTurnTime);
            if (!wandering && LEFT < kDockDistance && SPEED < kDockSpeed &&
                m_windowSpeed <= kDockWindowSpeed * movingAgent.grip) {
                if (m_phase == Phase::Movement && movingAgent.settleWait < 0.0) {
                    movingAgent.settleWait = m_params.stagger * unit_draw(m_rng);
                }
                if (m_phase != Phase::Movement || (movingAgent.settleWait -= kStep) <= 0.0) {
                    movingAgent.docked = true;
                    movingAgent.position = movingAgent.home;
                    movingAgent.velocity = {};
                }
            }
        }
        const bool WHOLE = std::ranges::all_of(m_agents,
            [](const SwarmAgent &a) { return a.docked && a.lock >= 1.0 && a.turn <= 0.0; });
        if (CLOSING) {
            // Preserve an interrupted opening's surface until the first piece leaves.
            if (!WHOLE) {
                m_rounding = approach(m_rounding, 1.0, kStep / kCornerTime);
            }
        } else if (WHOLE && !wandering) {
            m_rounding = approach(m_rounding, 0.0, kStep / kCornerTime);
        } else if (m_phase == Phase::Opening) {
            m_rounding = 1.0;
        } else {
            m_rounding = approach(m_rounding, 1.0, kStep / kCornerTime);
        }
    }

}  // namespace hyprimate
