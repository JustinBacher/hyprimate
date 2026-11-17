#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

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
            return {a.x + b.x, a.y + b.y};
        }
        Vec2 operator-(Vec2 a, Vec2 b) noexcept {
            return {a.x - b.x, a.y - b.y};
        }
        Vec2 operator*(Vec2 a, double s) noexcept {
            return {a.x * s, a.y * s};
        }
        double dot(Vec2 a, Vec2 b) noexcept {
            return a.x * b.x + a.y * b.y;
        }
        double length(Vec2 a) noexcept {
            return std::hypot(a.x, a.y);
        }
        double lerp(double a, double b, double t) noexcept {
            return a + (b - a) * t;
        }
        double unit(double v) noexcept {
            return std::clamp(v, 0.0, 1.0);
        }

        Vec2 clampLength(Vec2 a, double limit) noexcept {
            const double l = length(a);
            return l > limit && l > 0.0 ? a * (limit / l) : a;
        }

        // `a` turned clockwise on screen (y down) by `angle`.
        Vec2 turned(Vec2 a, double angle) noexcept {
            const double c = std::cos(angle), s = std::sin(angle);
            return {a.x * c - a.y * s, a.x * s + a.y * c};
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
                    return {s.x - depth, along};
                case Edge::Right:
                    return {s.x + s.w + depth, along};
                case Edge::Top:
                    return {along, s.y - depth};
                case Edge::Bottom:
                    return {along, s.y + s.h + depth};
            }
            return {};
        }

        std::uint32_t bucket(Vec2 p, double cell) noexcept {
            const auto ix = static_cast<std::int64_t>(std::floor(p.x / cell)) & (kHashSide - 1);
            const auto iy = static_cast<std::int64_t>(std::floor(p.y / cell)) & (kHashSide - 1);
            return static_cast<std::uint32_t>(iy * kHashSide + ix);
        }

    }  // namespace

    void Swarm::layout(const SwarmPlacement &placement) {
        const Box &capture = placement.capture;
        m_origin = {capture.x, capture.y};
        m_size = {capture.w, capture.h};
        m_screen = {placement.screen.x - capture.x,
            placement.screen.y - capture.y,
            placement.screen.w,
            placement.screen.h};

        // The body, clipped to the capture; a body that is unusable or outside it falls back to the
        // whole capture.
        const double x0 = std::clamp(placement.body.x - capture.x, 0.0, capture.w);
        const double y0 = std::clamp(placement.body.y - capture.y, 0.0, capture.h);
        const double x1 =
            std::clamp(placement.body.x + placement.body.w - capture.x, 0.0, capture.w);
        const double y1 =
            std::clamp(placement.body.y + placement.body.h - capture.y, 0.0, capture.h);
        m_body = isUsable(placement.body) && x1 - x0 >= 1.0 && y1 - y0 >= 1.0
                     ? Box{x0, y0, x1 - x0, y1 - y0}
                     : Box{0.0, 0.0, capture.w, capture.h};

        const auto count = static_cast<int>(m_agents.size());
        if (m_rows == 0)
            m_rows = std::clamp(
                static_cast<int>(std::lround(std::sqrt(count * m_body.h / m_body.w))), 1, count);
        const int rows = m_rows;
        m_spacing = std::sqrt(m_body.w * m_body.h / count);

        // Rows share the agents as evenly as integers allow, so every row's tiles are near square.
        const auto columnsOf = [&](int r) { return (r + 1) * count / rows - r * count / rows; };
        int index = 0;
        for (int r = 0; r < rows; ++r) {
            const int columns = columnsOf(r);
            const double top = m_body.y + m_body.h * r / rows;
            const double bottom = m_body.y + m_body.h * (r + 1) / rows;
            for (int c = 0; c < columns; ++c) {
                const double left = m_body.x + m_body.w * c / columns;
                const double right = m_body.x + m_body.w * (c + 1) / columns;
                SwarmAgent &a = m_agents[static_cast<std::size_t>(index++)];
                // Outermost tiles extend over the decoration margin to the capture's edge.
                const double tx0 = c == 0 ? 0.0 : left, tx1 = c == columns - 1 ? m_size.x : right;
                const double ty0 = r == 0 ? 0.0 : top, ty1 = r == rows - 1 ? m_size.y : bottom;
                a.tile = {tx0, ty0, tx1 - tx0, ty1 - ty0};
                a.home = {0.5 * (left + right), 0.5 * (top + bottom)};
                a.fit = 0.5 * std::min(right - left, bottom - top);
                a.reach = std::hypot(std::max(a.home.x - tx0, tx1 - a.home.x),
                    std::max(a.home.y - ty0, ty1 - a.home.y));
                if (a.docked) a.position = a.home;
            }
        }
    }

    void Swarm::build(
        const SwarmParams &params, std::uint64_t seed, const SwarmPlacement &placement) {
        m_params = sane(params);
        m_rng = seed ^ 0x5DEECE66D1CE4E5BULL;
        m_agents.assign(static_cast<std::size_t>(m_params.count), SwarmAgent{});
        m_rows = 0;
        for (SwarmAgent &a : m_agents) {
            a.docked = true;
            a.lock = 1.0;
            a.visible = true;
            a.pace = 1.0 + m_params.variation * kPaceSpread * (unitDraw(m_rng) - 0.5);
            a.grip = lerp(kGripMin, kGripMax, m_params.responsiveness) *
                     (1.0 + m_params.variation * kGripSpread * (unitDraw(m_rng) - 0.5));
            a.rest = std::numbers::pi * (2.0 * unitDraw(m_rng) - 1.0);
            a.reaction = kReactionShare * m_params.stagger * unitDraw(m_rng);
            a.arc = kArcAngle * m_params.variation * (2.0 * unitDraw(m_rng) - 1.0);
            a.weave = 2.0 * std::numbers::pi * unitDraw(m_rng);
            a.weaveRate = lerp(kWeaveRateMin, kWeaveRateMax, unitDraw(m_rng));
            a.gait = 2.0 * std::numbers::pi * unitDraw(m_rng);
            a.gaitRate = lerp(kGaitRateMin, kGaitRateMax, unitDraw(m_rng));
        }
        layout(placement);
        m_shift = {};
        m_workspaceShift = {};
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
        if (!isUsable(placement.capture) || !isUsable(placement.screen) || sides.empty() ||
            !finite(params))
            return;

        const bool closing = kind == Transition::Close;
        if (!closing || !active() || m_agents.empty())
            build(params, seed, placement);
        else {
            // A close during an opening or a movement keeps the agents where they are.
            m_params = sane(params);
            m_params.count = static_cast<int>(m_agents.size());
            m_screen = {placement.screen.x - m_origin.x,
                placement.screen.y - m_origin.y,
                placement.screen.w,
                placement.screen.h};
            m_shift = {};
            m_workspaceShift = {};
            m_time = 0.0;
        }
        m_phase = closing ? Phase::Closing : Phase::Opening;
        if (!closing) m_rounding = 1.0;

        const double cruise = m_params.speed;
        std::array<Edge, 4> allowed{};
        std::size_t edges = 0;
        for (Edge e : kEdges) {
            if (sides.has(e)) allowed[edges++] = e;
        }

        double longest = 0.0;
        for (SwarmAgent &a : m_agents) {
            // A random point beyond a random one of the edges: where an agent comes from, or goes
            // to, says nothing about where its tile is.
            const Edge edge =
                allowed[std::min(edges - 1, static_cast<std::size_t>(unitDraw(m_rng) * edges))];
            const bool hz = horizontal(edge);
            const double along =
                (hz ? m_screen.x : m_screen.y) + (hz ? m_screen.w : m_screen.h) * unitDraw(m_rng);

            if (closing) {
                if (!a.visible) {
                    // Never entered: nothing of it is on screen to carry away.
                    a.gone = true;
                    continue;
                }
                a.leaving = true;
                a.due = 0.0;
                a.exit = beyond(m_screen, edge, along, kExitDepth * a.fit + a.reach);
                // Agents let go in random order over the stagger; those already loose leave at
                // once.
                a.delay = a.docked ? m_params.stagger * unitDraw(m_rng) : 0.0;
                continue;
            }

            a.position = beyond(m_screen, edge, along, kEntryDepth * a.fit);
            const Vec2 toHome = a.home - a.position;
            const double d = length(toHome);
            a.velocity = d > 0.0 ? toHome * (kEntrySpeed * cruise * a.pace / d) : Vec2{};
            a.heading = std::atan2(toHome.y, toHome.x);
            a.turn = 1.0;
            a.lock = 0.0;
            a.docked = false;
            a.visible = false;
            // How long its walk takes, straight and at its cruise; turned into a departure below.
            a.due = d / (cruise * a.pace);
            longest = std::max(longest, a.due);
        }

        // An opening agent is due home at a random moment of the stagger, counted from when the one
        // with the longest walk can be there, and sets off as late as its own walk allows. Tiles
        // therefore fill in no order, whether they are near an edge or far from it.
        double first = std::numeric_limits<double>::infinity();
        for (SwarmAgent &a : m_agents) {
            if (a.gone) continue;
            if (!closing) {
                const double walk = a.due;
                a.due = kLead * longest + m_params.stagger * unitDraw(m_rng);
                a.delay = std::max(0.0, a.due - walk);
            }
            first = std::min(first, a.delay);
        }
        // The first to go goes now, so the animation never opens on a wait.
        for (SwarmAgent &a : m_agents) {
            if (a.gone) continue;
            a.delay -= first;
            if (!closing) a.due -= first;
        }
    }

    void Swarm::beginMovement(
        const SwarmParams &params, std::uint64_t seed, const SwarmPlacement &placement) {
        if (m_phase != Phase::Idle || !isUsable(placement.capture) || !isUsable(placement.screen) ||
            !finite(params))
            return;
        build(params, seed, placement);
        m_phase = Phase::Movement;
    }

    void Swarm::updateMovement(const SwarmPlacement &placement, Vec2 workspaceDelta) {
        if ((m_phase != Phase::Opening && m_phase != Phase::Movement) ||
            !isUsable(placement.capture) || !isUsable(placement.screen))
            return;
        m_shift = m_shift + (Vec2{placement.capture.x, placement.capture.y} - m_origin);
        m_workspaceShift = m_workspaceShift + workspaceDelta;
        layout(placement);
    }

    void Swarm::reset() noexcept {
        m_phase = Phase::Idle;
        m_agents.clear();
        m_shift = {};
        m_workspaceShift = {};
        m_windowSpeed = 0.0;
        m_rounding = 0.0;
        m_accumulator = 0.0;
    }

    Box Swarm::bounds() const noexcept {
        double x0 = std::numeric_limits<double>::infinity(), y0 = x0;
        double x1 = -x0, y1 = -x0;
        for (const SwarmAgent &a : m_agents) {
            if (!a.visible) continue;
            x0 = std::min(x0, a.position.x - a.reach);
            y0 = std::min(y0, a.position.y - a.reach);
            x1 = std::max(x1, a.position.x + a.reach);
            y1 = std::max(y1, a.position.y + a.reach);
        }
        x0 = std::max(x0, m_screen.x);
        y0 = std::max(y0, m_screen.y);
        x1 = std::min(x1, m_screen.x + m_screen.w);
        y1 = std::min(y1, m_screen.y + m_screen.h);
        if (!(x1 > x0 && y1 > y0)) return {};
        return {x0, y0, x1 - x0, y1 - y0};
    }

    bool Swarm::advance(double elapsedSeconds, bool movementHeld) {
        if (!active()) return false;
        const double dt = std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0
                              ? std::min(elapsedSeconds, kStallSeconds)
                              : 0.0;
        if (!(dt > 0.0)) return true;
        const bool workspaceMoving = m_workspaceShift.x != 0.0 || m_workspaceShift.y != 0.0;
        const bool wandering =
            moving() && (movementHeld || workspaceMoving) && m_params.wander > 0.0;
        if (wandering) {
            for (SwarmAgent &a : m_agents) {
                a.docked = false;
                a.pause = 0.0;
                a.settleWait = -1.0;
            }
        }

        const Vec2 relativeShift = m_shift - m_workspaceShift;
        const double speed = std::max(length(m_shift), length(relativeShift)) / dt;
        if (speed > 0.0) {
            // Loose agents trail client motion, but ride the workspace's coordinate frame. Keeping
            // them screen-anchored would leave outgoing tails to be culled and arrivals offscreen.
            for (SwarmAgent &a : m_agents) {
                if (a.gone || (a.docked && speed <= a.grip)) continue;
                a.position = a.position - relativeShift;
                if (a.docked) {
                    // It was riding the window. It stays put for its reaction time, then sets off
                    // after the window.
                    a.docked = false;
                    a.velocity = {};
                    a.heading = std::atan2(m_shift.y, m_shift.x);
                    a.pause = a.reaction;
                    a.settleWait = -1.0;
                }
            }
            m_still = 0.0;
        } else if (!wandering)
            m_still += dt;
        if (wandering) m_still = 0.0;
        m_windowSpeed = std::max(speed, m_windowSpeed * std::exp(-dt / kWindowSpeedHold));
        m_shift = {};
        m_workspaceShift = {};

        m_accumulator += dt;
        while (m_accumulator >= kStep - kAccumulatorTolerance) {
            step(wandering);
            m_accumulator = std::max(0.0, m_accumulator - kStep);
        }

        if (m_phase == Phase::Closing) {
            if (m_time > m_params.stagger + kCloseDeadline ||
                std::ranges::all_of(m_agents, [](const SwarmAgent &a) { return a.gone; }))
                m_phase = Phase::Closed;
        } else if (!wandering && m_rounding <= 0.0 &&
                   std::ranges::all_of(m_agents, [](const SwarmAgent &a) {
                       return a.docked && a.lock >= 1.0 && a.turn <= 0.0;
                   }))
            m_phase = Phase::Idle;
        return active();
    }

    void Swarm::hash() {
        const double cell = kNeighbourRadius * m_spacing;
        m_cellStart.assign(kHashCells + 1, 0);
        m_cellOf.assign(m_agents.size(), kNoCell);
        for (std::size_t i = 0; i < m_agents.size(); ++i) {
            const SwarmAgent &a = m_agents[i];
            if (a.docked || a.gone || !a.visible) continue;
            m_cellOf[i] = bucket(a.position, cell);
            ++m_cellStart[m_cellOf[i] + 1];
        }
        for (int c = 0; c < kHashCells; ++c) m_cellStart[c + 1] += m_cellStart[c];
        m_cellItems.resize(m_cellStart[kHashCells]);
        // Filling from each bucket's end leaves m_cellStart pointing at the bucket starts again.
        for (std::size_t i = m_agents.size(); i-- > 0;) {
            if (m_cellOf[i] != kNoCell)
                m_cellItems[--m_cellStart[m_cellOf[i] + 1]] = static_cast<std::uint32_t>(i);
        }
        // Each start slid down one bucket; shift them back.
        for (int c = 0; c < kHashCells; ++c) m_cellStart[c] = m_cellStart[c + 1];
        m_cellStart[kHashCells] = static_cast<std::uint32_t>(m_cellItems.size());
    }

    Swarm::Crowd Swarm::crowd(std::size_t index, Vec2 forward, double force) const {
        const SwarmAgent &a = m_agents[index];
        const double cell = kNeighbourRadius * m_spacing;
        const double near = kSeparationRadius * m_spacing;
        const double look = kAvoidRadius * m_spacing;
        const double body = kSwarmBodyShare * a.fit;
        // How much an agent is still in transit: 0 at its home, where it neither blocks others nor
        // is blocked, so arrivals can settle while the rest of the crowd walks through.
        const auto transit = [&](const SwarmAgent &agent) {
            return agent.leaving ? 1.0
                                 : smoothstep(kTransitNear * m_spacing,
                                       kTransitFar * m_spacing,
                                       length(agent.home - agent.position));
        };
        const double own = transit(a);
        const Vec2 right{-forward.y, forward.x};  // its right-hand side on screen
        const auto cx = static_cast<std::int64_t>(std::floor(a.position.x / cell));
        const auto cy = static_cast<std::int64_t>(std::floor(a.position.y / cell));

        Crowd out;
        Vec2 push{}, dodge{}, velocity{}, centre{};
        int neighbours = 0;
        for (std::int64_t dy = -1; dy <= 1; ++dy) {
            for (std::int64_t dx = -1; dx <= 1; ++dx) {
                const auto c = static_cast<std::size_t>(
                    ((cy + dy) & (kHashSide - 1)) * kHashSide + ((cx + dx) & (kHashSide - 1)));
                const std::uint32_t begin = m_cellStart[c], end = m_cellStart[c + 1];
                for (std::uint32_t k = begin; k < end; ++k) {
                    const std::size_t j = m_cellItems[k];
                    if (j == index) continue;
                    const SwarmAgent &b = m_agents[j];
                    const Vec2 away = a.position - b.position;
                    const double d = length(away);
                    if (d > cell) continue;
                    // Coincident agents part along a direction fixed by their order.
                    const Vec2 normal =
                        d > 1e-6 ? away * (1.0 / d) : Vec2{index < j ? 1.0 : -1.0, 0.0};
                    const double closing =
                        -dot(a.velocity - b.velocity, normal);  // px/s the gap is shrinking by

                    // Bodies in transit do not pass through each other: each of the two backs out
                    // of half the overlap and gives up half the speed they were closing with.
                    const double overlap = body + kSwarmBodyShare * b.fit - d;
                    const double ahead = -dot(normal, forward);
                    const bool inWay = d < look && ahead > 0.0 && closing > 0.0;
                    const double solid = overlap > 0.0 || inWay ? std::min(own, transit(b)) : 0.0;
                    if (overlap > 0.0) {
                        out.shove = out.shove + normal * (0.5 * overlap * solid);
                        if (closing > 0.0) out.check = out.check + normal * (0.5 * closing * solid);
                    }
                    if (d < near) {
                        const double w = 1.0 - d / near;
                        push = push + normal * (w * w);
                    }
                    // Someone ahead that it is gaining on: step to the side they are not on. Dead
                    // ahead, step left, so two meeting head-on pass each other.
                    if (inWay) {
                        const double w = solid * (1.0 - d / look) * ahead *
                                         std::min(1.0, closing / (kAvoidSpeed * m_params.speed));
                        dodge = dodge + right * (dot(normal, right) > 0.0 ? w : -w);
                    }
                    velocity = velocity + b.velocity;
                    centre = centre + b.position;
                    ++neighbours;
                }
            }
        }

        out.steer = (push * kSeparationForce + dodge * kAvoidForce) * (force * m_params.separation);
        if (neighbours > 0) {
            const double share = 1.0 / neighbours;
            out.steer =
                out.steer + (velocity * share - a.velocity) * (kAlignRate * m_params.cohesion);
            out.steer = out.steer + (centre * share - a.position) *
                                        (kCohesionForce * force * m_params.cohesion / cell);
        }
        return out;
    }

    void Swarm::step(bool wandering) {
        m_time += kStep;
        hash();

        const bool closing = m_phase == Phase::Closing;
        const double force = lerp(kForceMin, kForceMax, m_params.responsiveness);
        const double omega = lerp(kSpringMin, kSpringMax, m_params.settling);
        const double merge = lerp(kMergeMax, kMergeMin, m_params.settling);
        // Patience starts once the last agent has had its turn to set off.
        const double patience =
            m_params.stagger + lerp(kPatienceMax, kPatienceMin, m_params.settling);
        const double urgency =
            closing || wandering ? 0.0 : smoothstep(patience, patience + kPatienceRamp, m_still);
        const bool overdue = !closing && !wandering && m_still > patience + kPatienceDeadline;
        const double solid = std::min(1.0, m_params.separation / kContactFull) * (1.0 - urgency);

        for (std::size_t i = 0; i < m_agents.size(); ++i) {
            SwarmAgent &a = m_agents[i];
            if (a.gone) continue;
            a.due -= kStep;
            if (a.delay > 0.0) {
                a.delay -= kStep;
                if (a.delay > 0.0) continue;
                a.delay = 0.0;
            }
            // Past its delay an opening agent is on its way in, and a closing one lets go of the
            // window.
            a.visible = true;
            if (a.leaving && a.docked) {
                a.docked = false;
                a.heading = std::atan2(a.exit.y - a.position.y, a.exit.x - a.position.x);
            }
            if (overdue && !a.docked) {
                a.docked = true;
                a.visible = true;
                a.position = a.home;
                a.velocity = {};
            }
            if (a.docked) {
                // Keep the creature alive while its carried tile straightens and fills in.
                if (a.lock < 1.0) a.walk += 4.0 * a.fit * kStep;
                if (a.turn > 0.0)
                    a.turn = std::max(0.0, a.turn - kStep / kTurnTime);
                else
                    a.lock = std::min(1.0, a.lock + kStep / merge);
                continue;
            }

            a.lock = std::max(0.0, a.lock - kStep / kSplitTime);
            if (a.leaving && a.lock > kHoldLock) continue;

            const double cruise = m_params.speed * a.pace;
            const double limit = cruise * force;
            const double pace = length(a.velocity);
            const Vec2 forward = pace > kHeadingSpeed * cruise
                                     ? a.velocity * (1.0 / pace)
                                     : Vec2{std::cos(a.heading), std::sin(a.heading)};
            const Crowd crowd = this->crowd(i, forward, limit);
            a.position = a.position + crowd.shove * solid;
            if (a.pause > 0.0) {
                // Shaken loose and not yet giving chase: the window leaves it behind, and
                // passers-by jostle it.
                a.pause -= kStep;
                continue;
            }
            a.velocity = a.velocity + crowd.check * solid;

            a.weave += a.weaveRate * kStep;
            a.gait += a.gaitRate * kStep;
            // A moving home keeps the spring walking around the tile throughout a held drag.
            // Its radius scales with wander; the real home is restored immediately on release.
            const double radius = wandering ? m_params.wander * m_spacing : 0.0;
            const Vec2 target = a.leaving ? a.exit
                                         : a.home + Vec2{radius * std::cos(a.weave),
                                                        radius * std::sin(0.73 * a.weave + a.gait)};
            const Vec2 offset = target - a.position;
            const double distance = length(offset);
            const Vec2 toward = distance > 1e-9 ? offset * (1.0 / distance) : Vec2{};

            // Route, gait and crowd act on the way and fade out near home and as patience runs out.
            const double fade =
                a.leaving ? 1.0
                          : smoothstep(kRouteNear * m_spacing, kRouteFar * m_spacing, distance) *
                                (1.0 - urgency);
            const double sway = (std::sin(a.weave) + 0.5 * std::sin(2.3 * a.weave + 1.7)) / 1.5;
            const double off = std::clamp(
                (a.arc + kMeanderAngle * m_params.wander * sway) * fade, -kRouteLimit, kRouteLimit);
            const double hesitation = smoothstep(kGaitFrom, kGaitFull, std::sin(a.gait));

            // Walk the route at the gait's speed; arriving agents brake over their stopping
            // distance.
            double want = cruise * (1.0 - kGaitDepth * m_params.wander * hesitation * fade);
            if (m_phase ==
                Phase::Opening)  // on a schedule: hurry or dawdle towards being home when due
                want *= a.due > kStep ? std::clamp(distance / (a.due * cruise), kDawdle, kHurry)
                                      : kHurry;
            if (!a.leaving)
                want *= std::min(1.0,
                    distance / (kSlowMargin * cruise * cruise / (2.0 * limit) + 0.5 * m_spacing));
            Vec2 accel =
                clampLength((turned(toward, off) * want - a.velocity) * (1.0 / kSteerTime), limit);
            accel = accel + crowd.steer * fade;

            if (!a.leaving) {
                const double settle = std::max(
                    1.0 - smoothstep(kSpringNear * m_spacing, kSpringFar * m_spacing, distance),
                    urgency);
                if (settle > 0.0) {
                    const Vec2 spring =
                        clampLength(offset * (omega * omega) - a.velocity * (2.0 * omega),
                            kSpringForce * limit);
                    accel = accel * (1.0 - settle) + spring * settle;
                }
            }

            a.velocity = clampLength(a.velocity + accel * kStep, kSpeedCap * cruise);
            a.position = a.position + a.velocity * kStep;
            const double speed = length(a.velocity);
            a.travel += speed * kStep;
            // Keep the gait readable even when small agents travel many body lengths per display
            // frame.
            const bool settling = !wandering && !a.leaving && distance < kRestRadius * m_spacing;
            a.walk += (settling ? 4.0 * a.fit : std::min(speed, 4.0 * a.fit)) * kStep;
            if (speed > kHeadingSpeed * cruise) {
                const double swing = wrapAngle(std::atan2(a.velocity.y, a.velocity.x) - a.heading);
                a.heading =
                    wrapAngle(a.heading + std::clamp(swing, -kTurnRate * kStep, kTurnRate * kStep));
            }

            if (a.leaving) {
                a.turn = approach(a.turn, 1.0, kStep / kTurnTime);
                const double margin = kGoneMargin * a.fit;
                if (a.position.x < m_screen.x - margin ||
                    a.position.x > m_screen.x + m_screen.w + margin ||
                    a.position.y < m_screen.y - margin ||
                    a.position.y > m_screen.y + m_screen.h + margin) {
                    a.gone = true;
                    a.visible = false;
                }
                continue;
            }

            const double left = length(a.home - a.position);
            a.turn = approach(a.turn,
                !wandering && left < kRestRadius * m_spacing ? 0.0 : 1.0,
                kStep / kTurnTime);
            if (!wandering && left < kDockDistance && speed < kDockSpeed &&
                m_windowSpeed <= kDockWindowSpeed * a.grip) {
                if (m_phase == Phase::Movement && a.settleWait < 0.0)
                    a.settleWait = m_params.stagger * unitDraw(m_rng);
                if (m_phase != Phase::Movement || (a.settleWait -= kStep) <= 0.0) {
                    a.docked = true;
                    a.position = a.home;
                    a.velocity = {};
                }
            }
        }
        const bool whole = std::ranges::all_of(m_agents,
            [](const SwarmAgent &a) { return a.docked && a.lock >= 1.0 && a.turn <= 0.0; });
        if (closing) {
            // Preserve an interrupted opening's surface until the first piece leaves.
            if (!whole) m_rounding = approach(m_rounding, 1.0, kStep / kCornerTime);
        } else if (whole && !wandering) {
            m_rounding = approach(m_rounding, 0.0, kStep / kCornerTime);
        } else if (m_phase == Phase::Opening) {
            m_rounding = 1.0;
        } else {
            m_rounding = approach(m_rounding, 1.0, kStep / kCornerTime);
        }
    }

}  // namespace hyprimate
