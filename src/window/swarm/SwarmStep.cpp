#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numbers>
#include <ranges>
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

        // Steering. Accelerations are multiples of an agent's cruise speed per second.
        constexpr double FORCE_MIN = 4.0, FORCE_MAX = 14.0;  // by responsiveness
        constexpr double STEER_TIME = 0.07;  // s to close a velocity error, before the force limit
        constexpr double SLOW_MARGIN = 1.5;  // braking distance margin for arriving
        constexpr double TURN_RATE = 14.0;  // rad/s the heading can swing
        constexpr double HEADING_SPEED = 0.04;  // of cruise: slower than this keeps the old heading

        // The route. An agent walks in the direction of its target turned by its approach arc and
        // its meander, so it comes in on a wandering curve; both fade out near home, where the turn
        // would otherwise become an orbit.
        constexpr double MEANDER_ANGLE = 0.9;  // rad either way, at full wander
        constexpr double ROUTE_LIMIT = 1.15;  // rad: it is never turned further than this off its target
        constexpr double ROUTE_NEAR = 0.5,
                         ROUTE_FAR = 2.0;  // spacings from home over which route and crowd fade in
        constexpr double DAWDLE = 0.7,
                         HURRY = 1.3;  // of cruise: how far an opening agent bends its speed to be on time

        // The gait. An agent hesitates for part of every gait cycle, so a column of them bunches
        // and strings out.
        constexpr double GAIT_DEPTH = 0.85;  // share of cruise lost in a hesitation, at full wander
        constexpr double GAIT_FROM = 0.45,
                         GAIT_FULL = 0.95;  // sine of the gait phase over which it slows

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
                         PATIENCE_MIN = 0.5;  // s of a still window, after the stagger, before hurrying
        constexpr double PATIENCE_RAMP = 0.8;
        constexpr double PATIENCE_DEADLINE = 4.0;  // s after patience at which every agent is placed
        constexpr double DOCK_DISTANCE = 1.5;  // px
        constexpr double DOCK_SPEED = 60.0;  // px/s
        constexpr double DOCK_WINDOW_SPEED = 0.5;  // of grip: the window must be slower than this to dock on it

        // Formation lock.
        constexpr double MERGE_MAX = 0.5;
        constexpr double TURN_TIME = 0.16;  // s to swing between rest direction and heading
        constexpr double HOLD_LOCK = 0.35;  // a leaving agent waits until its lock is below this
        constexpr double GONE_MARGIN = 1.2;  // fits beyond the edge at which it has left

        constexpr int HASH_SIDE = 32;  // buckets per axis; positions wrap and the distance check rejects aliases
        constexpr int HASH_CELLS = HASH_SIDE * HASH_SIDE;
        constexpr std::uint32_t NO_CELL = std::numeric_limits<std::uint32_t>::max();

        double dot(Vec2 lhs, Vec2 rhs) noexcept {
            return (lhs.x * rhs.x) + (lhs.y * rhs.y);
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

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Preserve hot neighbor traversal
    // and FP accumulation order.
    Swarm::Crowd Swarm::crowd(std::size_t index, Vec2 forward, double force) const {
        const SwarmAgent &subject_agent = m_agents[index];
        const double CELL = NEIGHBOUR_RADIUS * m_spacing;
        // Most queries reuse hash()'s cell. Newly visible/undocked agents were not inserted,
        // but still query their neighbours just as before.
        const std::uint32_t CELL_INDEX =
            m_cell_of[index] != NO_CELL ? m_cell_of[index] : bucket(subject_agent.position, CELL);
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

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Preserve per-agent mutation, RNG
    // and FP order.
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
            release_agent(moving_agent);
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
