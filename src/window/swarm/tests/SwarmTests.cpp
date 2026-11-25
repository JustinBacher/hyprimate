#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

#include "Check.hpp"
#include "shared/Core.hpp"
#include "window/shared/EdgeFrame.hpp"
#include "window/swarm/Swarm.hpp"
#include "window/swarm/SwarmSprite.hpp"

namespace {
    using namespace hyprimate;

    constexpr Box SCREEN{.x = 0, .y = 0, .w = 1920, .h = 1080};
    constexpr Box BODY{.x = 400, .y = 200, .w = 900, .h = 600};
    constexpr Box CAPTURE{
        .x = 380,
        .y = 180,
        .w = 940,
        .h = 640,
    };  // 20 px of shadow around the body
    constexpr double FRAME = 1.0 / 144.0;

    SwarmPlacement placement(Vec2 shift = {}) {
        return {
            .capture =
                {
                    .x = CAPTURE.x + shift.x,
                    .y = CAPTURE.y + shift.y,
                    .w = CAPTURE.w,
                    .h = CAPTURE.h,
                },
            .body = {.x = BODY.x + shift.x, .y = BODY.y + shift.y, .w = BODY.w, .h = BODY.h},
            .screen = SCREEN,
        };
    }
    EdgeSet sides(std::initializer_list<Edge> edges) {
        EdgeSet edge_set;
        for (const Edge EDGE : edges) {
            edge_set.add(EDGE);
        }
        return edge_set;
    }
    // Runs until the model stops or `seconds` pass; returns the time used.
    double run(Swarm &swarm, double seconds) {
        double elapsed = 0.0;
        while (elapsed < seconds && swarm.active()) {
            swarm.advance(FRAME);
            elapsed += FRAME;
        }
        return elapsed;
    }
    // Agent position in the placement's space, for a window that has not moved.
    Vec2 on_screen(const SwarmAgent &agent) {
        return {.x = CAPTURE.x + agent.position.x, .y = CAPTURE.y + agent.position.y};
    }
    bool in_formation(const Swarm &swarm) {
        return swarm.rounding() == 0.0 &&
               std::ranges::all_of(swarm.agents(), [](const SwarmAgent &agent) {
                   return agent.docked && agent.lock == 1.0 && agent.turn == 0.0 &&
                          agent.position.x == agent.home.x && agent.position.y == agent.home.y;
               });
    }
    // Pearson correlation of two equally long series.
    double correlation(const std::vector<double> &first, const std::vector<double> &second) {
        double mean_a = 0.0;
        double mean_b = 0.0;
        auto b_it = second.begin();
        for (const double VALUE : first) {
            mean_a += VALUE / static_cast<double>(first.size()),
                mean_b += *b_it++ / static_cast<double>(second.size());
        }
        double cross_sum = 0.0;
        double square_sum_a = 0.0;
        double square_sum_b = 0.0;
        b_it = second.begin();
        for (const double VALUE : first) {
            cross_sum += (VALUE - mean_a) * (*b_it - mean_b);
            square_sum_a += (VALUE - mean_a) * (VALUE - mean_a);
            square_sum_b += (*b_it - mean_b) * (*b_it - mean_b);
            ++b_it;
        }
        return cross_sum / std::sqrt(square_sum_a * square_sum_b);
    }
    // When each agent of an opening first docks, in seconds; the model is run to its end.
    std::vector<double> landings(Swarm &swarm) {
        std::vector<double> landing_times(swarm.agents().size(), -1.0);
        // NOLINTNEXTLINE(bugprone-float-loop-counter): Preserve FP recurrence; fixed-count
        // replacement adds a register save in GCC -O2.
        for (double elapsed = 0.0; swarm.active() && elapsed < 30.0; elapsed += FRAME) {
            swarm.advance(FRAME);
            auto agent = swarm.agents().begin();
            for (double &landing_time : landing_times) {
                if (landing_time < 0.0 && agent->docked) {
                    landing_time = elapsed;
                }
                ++agent;
            }
        }
        return landing_times;
    }

    void tiles_partition_the_capture() {
        for (const int COUNT : {1, 2, 7, 80, 400}) {
            Swarm partition_swarm;
            SwarmParams partition_params;
            partition_params.count = COUNT;
            partition_swarm.begin_movement(partition_params, 1, placement());
            CHECK(std::cmp_equal(partition_swarm.agents().size(), COUNT));
            double area = 0.0;
            double min_x = 1e9;
            double min_y = 1e9;
            double max_x = -1e9;
            double max_y = -1e9;
            for (const SwarmAgent &tile_agent : partition_swarm.agents()) {
                area += tile_agent.tile.w * tile_agent.tile.h;
                min_x = std::min(min_x, tile_agent.tile.x),
                min_y = std::min(min_y, tile_agent.tile.y);
                max_x = std::max(max_x, tile_agent.tile.x + tile_agent.tile.w),
                max_y = std::max(max_y, tile_agent.tile.y + tile_agent.tile.h);
                // The creature fits inside the body's share of its own tile.
                CHECK(tile_agent.home.x - tile_agent.fit >= tile_agent.tile.x - 1e-9 &&
                      tile_agent.home.x + tile_agent.fit <=
                          tile_agent.tile.x + tile_agent.tile.w + 1e-9);
                CHECK(tile_agent.home.y - tile_agent.fit >= tile_agent.tile.y - 1e-9 &&
                      tile_agent.home.y + tile_agent.fit <=
                          tile_agent.tile.y + tile_agent.tile.h + 1e-9);
                CHECK(tile_agent.home.x >= 20.0 && tile_agent.home.x <= 920.0 &&
                      tile_agent.home.y >= 20.0 && tile_agent.home.y <= 620.0);
            }
            // Same total area and same outline as the capture: the tiles cover it without overlap.
            CHECK(std::abs(area - (CAPTURE.w * CAPTURE.h)) < 1e-6);
            CHECK(min_x == 0.0 && min_y == 0.0 && max_x == CAPTURE.w && max_y == CAPTURE.h);
        }
        // Near-square pieces: 80 agents on a 3:2 body.
        Swarm swarm;
        SwarmParams params;
        params.count = 80;
        swarm.begin_movement(params, 1, placement());
        for (const SwarmAgent &agent : swarm.agents()) {
            CHECK(agent.fit > 30.0 && agent.fit < 60.0);
        }
    }

    void opening_enters_from_its_sides_and_settles_exactly() {
        Swarm swarm;
        swarm.begin(Transition::Open, {}, 7, placement(), sides({Edge::Top, Edge::Right}));
        CHECK(swarm.opening() && swarm.active());
        for (const SwarmAgent &agent : swarm.agents()) {
            const Vec2 POSITION = on_screen(agent);
            CHECK(!agent.visible && agent.lock == 0.0);
            CHECK(POSITION.y < SCREEN.y || POSITION.x > SCREEN.x + SCREEN.w);
        }
        // Nothing is on screen yet, so there is nothing to draw.
        CHECK(swarm.bounds().w == 0.0);

        run(swarm, 0.8);
        CHECK(swarm.active());
        CHECK(std::ranges::any_of(
            swarm.agents(), [](const SwarmAgent &agent) { return agent.visible; }));
        CHECK(swarm.bounds().w > 0.0);

        // Well inside the point at which stragglers are placed by force.
        const double USED = 0.8 + run(swarm, 8.0);
        CHECK(!swarm.active() && !swarm.closed());
        CHECK(USED < 4.5);
        CHECK(in_formation(swarm));
    }

    void equal_seeds_give_equal_motion() {
        const auto MID_FLIGHT = [](std::uint64_t seed) {
            Swarm swarm;
            swarm.begin(Transition::Open, {}, seed, placement(), sides({Edge::Left}));
            run(swarm, 0.4);
            std::vector<Vec2> out;
            for (const SwarmAgent &agent : swarm.agents()) {
                out.push_back(agent.position);
            }
            return out;
        };
        const auto FIRST_RUN = MID_FLIGHT(3);
        const auto SAME_SEED_RUN = MID_FLIGHT(3);
        const auto OTHER_SEED_RUN = MID_FLIGHT(4);
        bool same = true;
        bool differs = false;
        auto same_sample = SAME_SEED_RUN.begin();
        auto other_sample = OTHER_SEED_RUN.begin();
        for (const auto &sample : FIRST_RUN) {
            same = same && sample.x == same_sample->x && sample.y == same_sample->y;
            differs = differs || sample.x != other_sample->x || sample.y != other_sample->y;
            ++same_sample;
            ++other_sample;
        }
        CHECK(same);
        CHECK(differs);
    }

    void seeded_crowded_trajectory() {
        SwarmParams params;
        params.count = 120;
        params.stagger = 0.15;
        params.cohesion = 1.0;
        params.separation = 1.0;
        Swarm swarm;
        swarm.begin(Transition::Open, params, 0x53a9U, placement(), sides({Edge::Left}));
        std::uint64_t checksum = 14695981039346656037ULL;
        const auto MIX = [&checksum](std::uint64_t value) {
            checksum = (checksum ^ value) * 1099511628211ULL;
        };
        for (int frame = 0; frame < 300; ++frame) {
            swarm.advance(FRAME);
            for (const SwarmAgent &agent : swarm.agents()) {
                for (const double value : {agent.position.x, agent.position.y, agent.velocity.x,
                         agent.velocity.y, agent.heading, agent.lock, agent.turn, agent.travel,
                         agent.walk, agent.delay, agent.due, agent.weave, agent.gait}) {
                    MIX(std::bit_cast<std::uint64_t>(value));
                }
                MIX(static_cast<std::uint64_t>(agent.visible) |
                    (static_cast<std::uint64_t>(agent.docked) << 1) |
                    (static_cast<std::uint64_t>(agent.leaving) << 2) |
                    (static_cast<std::uint64_t>(agent.gone) << 3));
            }
        }
        CHECK(checksum == 10005184937473135830ULL);
    }

    void close_lets_go_in_no_order_and_leaves_through_its_side() {
        Swarm swarm;
        swarm.begin(Transition::Close, {}, 5, placement(), sides({Edge::Bottom}));
        CHECK(swarm.active() && !swarm.closed());
        CHECK(in_formation(swarm));

        // Half way through the stagger about half have let go, and which half has nothing to do
        // with where they stand: the window does not come apart from the side it leaves through.
        run(swarm, 0.3);
        std::vector<double> loose;
        std::vector<double> row;
        std::vector<double> column;
        for (const SwarmAgent &departing_agent : swarm.agents()) {
            loose.push_back(departing_agent.docked ? 0.0 : 1.0);
            row.push_back(departing_agent.home.y);
            column.push_back(departing_agent.home.x);
        }
        const auto GONE = std::ranges::count(loose, 1.0);
        CHECK(GONE > static_cast<long>(loose.size()) / 4 &&
              GONE < 3 * static_cast<long>(loose.size()) / 4);
        CHECK(std::abs(correlation(loose, row)) < 0.2);
        CHECK(std::abs(correlation(loose, column)) < 0.2);

        const double USED = 0.3 + run(swarm, 8.0);
        CHECK(swarm.closed() && !swarm.active());
        CHECK(USED < 4.0);
        // They leave through the whole length of the edge, not just the stretch under the window.
        double leftmost = 1e9;
        double rightmost = -1e9;
        for (const SwarmAgent &agent : swarm.agents()) {
            CHECK(agent.gone && !agent.visible);
            CHECK(on_screen(agent).y > SCREEN.y + SCREEN.h);
            leftmost = std::min(leftmost, on_screen(agent).x),
            rightmost = std::max(rightmost, on_screen(agent).x);
        }
        CHECK(leftmost < BODY.x - 200.0 && rightmost > BODY.x + BODY.w + 200.0);
        CHECK(swarm.bounds().w == 0.0);
    }

    void tiles_fill_in_no_order() {
        // From one edge, tiles near it are not filled before tiles far from it, and agents do not
        // walk in along their own rows: where one enters says nothing about where its tile is.
        Swarm swarm;
        swarm.begin(Transition::Open, {}, 7, placement(), sides({Edge::Left}));
        std::vector<double> column;
        std::vector<double> row;
        std::vector<double> entry;
        for (const SwarmAgent &entering_agent : swarm.agents()) {
            column.push_back(entering_agent.home.x);
            row.push_back(entering_agent.home.y);
            entry.push_back(entering_agent.position.y);
        }
        CHECK(std::abs(correlation(entry, row)) < 0.2);
        const double TOP = *std::ranges::min_element(entry);
        const double BOTTOM = *std::ranges::max_element(entry);
        CHECK(CAPTURE.y + TOP < SCREEN.y + (0.1 * SCREEN.h) &&
              CAPTURE.y + BOTTOM > SCREEN.y + (0.9 * SCREEN.h));
        const std::vector<double> LANDED = landings(swarm);
        CHECK(in_formation(swarm));
        CHECK(std::abs(correlation(LANDED, column)) < 0.25);
        CHECK(std::abs(correlation(LANDED, row)) < 0.25);

        // With several edges to choose from, each is used about equally.
        Swarm all;
        all.begin(Transition::Open,
            {},
            8,
            placement(),
            sides({Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}));
        int from_left = 0;
        int from_top = 0;
        for (const SwarmAgent &agent : all.agents()) {
            from_left += static_cast<int>(on_screen(agent).x < SCREEN.x);
            from_top += static_cast<int>(on_screen(agent).y < SCREEN.y);
        }
        const int COUNT = static_cast<int>(all.agents().size());
        CHECK(from_left > COUNT / 8 && from_left < 3 * COUNT / 8);
        CHECK(from_top > COUNT / 8 && from_top < 3 * COUNT / 8);
    }

    void surface_rounding_waits_for_formation() {
        for (const bool MOVEMENT : {false, true}) {
            Swarm swarm;
            if (MOVEMENT) {
                swarm.begin_movement({}, 17, placement());
                CHECK(swarm.rounding() == 0.0);
                for (int i = 0; i < 120; ++i) {
                    swarm.advance(1.0 / 120.0, true);
                }
            } else {
                swarm.begin(Transition::Open, {}, 17, placement(), sides({Edge::Left, Edge::Top}));
            }
            CHECK(swarm.rounding() == 1.0);
            bool saw_joining = false;
            bool saw_handoff = false;
            for (int frame = 0; frame < 1200 && swarm.active(); ++frame) {
                swarm.advance(1.0 / 120.0);
                const bool WHOLE = std::ranges::all_of(swarm.agents(), [](const SwarmAgent &agent) {
                    return agent.docked && agent.turn == 0.0 && agent.lock == 1.0;
                });
                if (!WHOLE) {
                    saw_joining = true;
                    CHECK(swarm.rounding() == 1.0);
                    CHECK(swarm.active());
                } else if (swarm.rounding() > 0.0) {
                    saw_handoff = true;
                    CHECK(swarm.active());
                }
            }
            CHECK(saw_joining && saw_handoff);
            CHECK(!swarm.active() && in_formation(swarm));
        }
    }

    void closing_rounds_the_surface_as_parts_leave() {
        Swarm swarm;
        swarm.begin(Transition::Close, {}, 5, placement(), sides({Edge::Bottom}));
        CHECK(swarm.rounding() == 0.0 && in_formation(swarm));
        swarm.advance(1.0 / 120.0);
        CHECK(swarm.rounding() > 0.0 && swarm.rounding() < 1.0);
        run(swarm, 0.25);
        CHECK(swarm.rounding() == 1.0);
        run(swarm, 8.0);
        CHECK(swarm.closed());
    }

    void close_takes_over_an_opening() {
        Swarm swarm;
        swarm.begin(Transition::Open, {}, 9, placement(), sides({Edge::Left}));
        run(swarm, 0.25);
        std::vector<Vec2> before;
        const double ROUNDING = swarm.rounding();
        for (const SwarmAgent &agent : swarm.agents()) {
            before.push_back(agent.position);
        }
        swarm.begin(Transition::Close, {}, 10, placement(), sides({Edge::Left}));
        CHECK(swarm.active() && !swarm.opening());
        CHECK(swarm.rounding() == ROUNDING);
        // Agents carry on from where they were instead of jumping into formation.
        auto agent_it = swarm.agents().begin();
        for (const Vec2 &position : before) {
            const auto &agent = *agent_it++;
            CHECK(agent.position.x == position.x && agent.position.y == position.y);
        }
        run(swarm, 8.0);
        CHECK(swarm.closed());
    }

    void close_inherits_an_in_flight_handoff() {
        Swarm swarm;
        swarm.begin(Transition::Open, {}, 9, placement(), sides({Edge::Left}));
        for (int frame = 0; frame < 1200 && swarm.rounding() == 1.0; ++frame) {
            swarm.advance(1.0 / 120.0);
        }
        const double ROUNDING = swarm.rounding();
        CHECK(swarm.active() && ROUNDING > 0.0 && ROUNDING < 1.0);
        swarm.begin(Transition::Close, {}, 10, placement(), sides({Edge::Left}));
        CHECK(swarm.rounding() == ROUNDING);
        swarm.advance(1.0 / 120.0);
        CHECK(swarm.rounding() > ROUNDING);
        run(swarm, 8.0);
        CHECK(swarm.closed());
    }

    void movement_without_motion_finishes_immediately() {
        Swarm swarm;
        swarm.begin_movement({}, 2, placement());
        CHECK(swarm.rounding() == 0.0);
        swarm.advance(1.0 / 120.0);
        CHECK(!swarm.active() && in_formation(swarm));
    }

    void slow_movement_keeps_the_window_whole() {
        Swarm swarm;
        swarm.begin_movement({}, 2, placement());
        CHECK(swarm.moving());
        // 30 px/s is under every agent's grip, so the window never comes apart.
        swarm.update_movement(placement({.x = 30.0 * FRAME, .y = 0.0}));
        swarm.advance(FRAME);
        CHECK(!swarm.active());
        CHECK(in_formation(swarm));
    }

    void workspace_slides_preserve_independent_flight() {
        for (const Vec2 TRAVEL : {
                 Vec2{.x = -SCREEN.w, .y = 0},
                 Vec2{.x = SCREEN.w, .y = 0},
                 Vec2{.x = 0, .y = -SCREEN.h},
                 Vec2{.x = 0, .y = SCREEN.h},
             }) {
            for (const bool INCOMING : {false, true}) {
                Swarm swarm;
                Vec2 offset = INCOMING ? Vec2{.x = -TRAVEL.x, .y = -TRAVEL.y} : Vec2{};
                swarm.begin_movement({}, 2, placement(offset));
                for (int frame = 0; frame < 40; ++frame) {
                    offset.x += TRAVEL.x / 40.0;
                    offset.y += TRAVEL.y / 40.0;
                    swarm.update_movement(placement(offset));
                    swarm.advance(FRAME);
                }
                // The native slide is over, but independently moving creatures are still flying.
                CHECK(swarm.active());
                if (!INCOMING) {
                    CHECK(swarm.bounds().w > 0.0 && swarm.bounds().h > 0.0);
                }
                std::vector<Vec2> before;
                for (const auto &agent : swarm.agents()) {
                    before.push_back(agent.position);
                }
                run(swarm, 0.1);
                double flight = 0.0;
                auto agent = swarm.agents().begin();
                for (const auto &position : before) {
                    flight +=
                        std::hypot(agent->position.x - position.x, agent->position.y - position.y);
                    ++agent;
                }
                CHECK(flight / before.size() > 1.0);
                run(swarm, 8.0);
                CHECK(in_formation(swarm));
                if (INCOMING) {
                    CHECK(swarm.bounds().w > 0.0 && swarm.bounds().h > 0.0);
                } else {
                    CHECK(swarm.bounds().w == 0.0 && swarm.bounds().h == 0.0);
                }
            }
        }
    }

    void workspace_reversal_preserves_screen_positions() {
        Swarm baseline;
        baseline.begin(Transition::Open, {}, 9, placement(), sides({Edge::Left}));
        run(baseline, 0.25);
        Swarm moved = baseline;
        Vec2 offset{};
        for (const Vec2 DELTA :
            {Vec2{.x = -300, .y = 40}, Vec2{.x = 500, .y = -80}, Vec2{.x = -200, .y = 40}}) {
            offset.x += DELTA.x;
            offset.y += DELTA.y;
            moved.update_movement(placement(offset));
            // Apply the coordinate change without consuming a physics substep.
            moved.advance(1e-6);
            CHECK(moved.opening());
            auto baseline_agent_it = baseline.agents().begin();
            for (const auto &moved_agent : moved.agents()) {
                const auto &baseline_agent = *baseline_agent_it++;
                CHECK(
                    std::abs(moved_agent.position.x + offset.x - baseline_agent.position.x) < 1e-8);
                CHECK(
                    std::abs(moved_agent.position.y + offset.y - baseline_agent.position.y) < 1e-8);
                CHECK(moved_agent.visible == baseline_agent.visible &&
                      moved_agent.lock == baseline_agent.lock);
            }
        }
    }

    void fast_movement_scatters_then_regroups() {
        Swarm swarm;
        swarm.begin_movement({}, 2, placement());
        Vec2 shift{};
        for (int i = 0; i < 40; ++i) {  // 3000 px/s to the left for ~0.28 s
            shift.x -= 3000.0 * FRAME;
            swarm.update_movement(placement(shift));
            swarm.advance(FRAME);
        }
        CHECK(swarm.active());
        double trailing = 0.0;
        double lock = 0.0;
        for (const SwarmAgent &agent : swarm.agents()) {
            // Loose agents trail behind a window that moved left: they sit to the right of their
            // homes.
            trailing += agent.position.x - agent.home.x;
            lock += agent.lock;
            CHECK(std::isfinite(agent.position.x) && std::isfinite(agent.position.y));
        }
        CHECK(trailing / swarm.agents().size() > 40.0);
        CHECK(lock / swarm.agents().size() < 0.2);

        const double USED = run(swarm, 8.0);
        CHECK(!swarm.active());
        CHECK(USED < 4.0);
        CHECK(in_formation(swarm));
    }

    void held_movement_wanders_until_release() {
        for (const double WANDER : {0.1, 0.5, 1.0}) {
            Swarm swarm;
            SwarmParams params;
            params.wander = WANDER;
            swarm.begin_movement(params, 23, placement());
            // No translations at all, including a pause longer than the forced-settle deadline.
            for (int interval = 0; interval < 20; ++interval) {
                const std::vector<SwarmAgent> BEFORE(swarm.agents().begin(), swarm.agents().end());
                for (int frame = 0; frame < 60; ++frame) {
                    swarm.advance(1.0 / 120.0, true);
                }
                CHECK(swarm.moving());
                auto agent_it = swarm.agents().begin();
                for (const SwarmAgent &previous : BEFORE) {
                    const auto &agent = *agent_it++;
                    CHECK(!agent.docked);
                    CHECK(agent.travel > previous.travel + 0.01);
                    CHECK(agent.walk > previous.walk);
                    CHECK(std::isfinite(agent.position.x) && std::isfinite(agent.position.y));
                }
            }
            run(swarm, 10.0);
            CHECK(!swarm.active());
            CHECK(in_formation(swarm));
        }
    }

    void held_movement_respects_wander() {
        const auto DISTANCE = [](double wander) {
            Swarm swarm;
            SwarmParams params;
            params.count = 1;
            params.wander = wander;
            swarm.begin_movement(params, 23, placement());
            for (int frame = 0; frame < 600; ++frame) {
                swarm.advance(1.0 / 120.0, true);
            }
            if (wander == 0.0) {
                CHECK(!swarm.active());
                CHECK(in_formation(swarm));
            }
            return swarm.agents().front().travel;
        };
        CHECK(DISTANCE(0.0) == 0.0);
        CHECK(DISTANCE(1.0) > DISTANCE(0.1) * 2.0);
    }

    void departures_are_staggered() {
        // How long the middle four fifths of an opening's agents take to land.
        const auto LANDING_SPAN = [](double stagger) {
            Swarm opening_swarm;
            SwarmParams opening_params;
            opening_params.stagger = stagger;
            opening_swarm.begin(
                Transition::Open, opening_params, 5, placement(), sides({Edge::Left}));
            opening_swarm.advance(2.0 * FRAME);
            // The first to go goes at once, so the animation never opens on a wait.
            CHECK(std::ranges::any_of(
                opening_swarm.agents(), [](const SwarmAgent &agent) { return agent.visible; }));
            std::vector<double> landing_times = landings(opening_swarm);
            std::ranges::sort(landing_times);
            return landing_times[landing_times.size() * 9 / 10] -
                   landing_times[landing_times.size() / 10];
        };
        const auto HOLDING = [](double stagger, double seconds) {
            Swarm closing_swarm;
            SwarmParams params;
            params.stagger = stagger;
            closing_swarm.begin(Transition::Close, params, 5, placement(), sides({Edge::Bottom}));
            run(closing_swarm, seconds);
            return std::ranges::count_if(
                closing_swarm.agents(), [](const SwarmAgent &agent) { return agent.docked; });
        };
        const long ALL = SwarmParams{}.count;
        const double FIRST = 2.0 * FRAME;  // long enough for one simulation step
        // An opening's agents are timed to land over the stagger, wherever they come from.
        CHECK(LANDING_SPAN(2.0) > LANDING_SPAN(0.0) + 1.0);
        // A closing window lets every agent go at once without a stagger; with one, the first goes
        // at once and the rest follow steadily until the stagger is up.
        CHECK(HOLDING(0.0, FIRST) == 0);
        CHECK(HOLDING(1.0, FIRST) > 9 * ALL / 10 && HOLDING(1.0, FIRST) < ALL);
        CHECK(HOLDING(1.0, 0.5) > ALL / 4 && HOLDING(1.0, 0.5) < 3 * ALL / 4);
        CHECK(HOLDING(1.0, 1.05) == 0);

        // Movement: agents shaken loose do not all give chase in the same instant.
        Swarm swarm;
        swarm.begin_movement({}, 2, placement());
        Vec2 shift{};
        for (int i = 0; i < 12; ++i) {  // 3000 px/s for ~0.08 s
            shift.x -= 3000.0 * FRAME;
            swarm.update_movement(placement(shift));
            swarm.advance(FRAME);
        }
        const auto WAITING = std::ranges::count_if(
            swarm.agents(), [](const SwarmAgent &agent) { return agent.travel == 0.0; });
        CHECK(WAITING > ALL / 4 && WAITING < ALL);
    }

    void routes_are_not_straight_lines() {
        // Mean distance walked over the straight-line distance, with nothing in the way.
        const auto DETOUR = [](double wander, double variation) {
            Swarm swarm;
            SwarmParams params;
            params.wander = wander;
            params.variation = variation;
            params.separation = 0.0;
            params.cohesion = 0.0;
            swarm.begin(Transition::Open, params, 31, placement(), sides({Edge::Left}));
            std::vector<double> straight;
            for (const SwarmAgent &opening_agent : swarm.agents()) {
                straight.push_back(std::hypot(opening_agent.position.x - opening_agent.home.x,
                    opening_agent.position.y - opening_agent.home.y));
            }
            run(swarm, 10.0);
            CHECK(in_formation(swarm));
            double total = 0.0;
            auto agent = swarm.agents().begin();
            for (const double DISTANCE : straight) {
                total += agent++->travel / DISTANCE;
            }
            return total / static_cast<double>(straight.size());
        };
        // With neither meander nor per-agent arcs an agent walks straight home; each of the two
        // bends its path.
        CHECK(std::abs(DETOUR(0.0, 0.0) - 1.0) < 0.01);
        CHECK(DETOUR(1.0, 0.0) > 1.06);
        CHECK(DETOUR(0.0, 1.0) > 1.06);
        CHECK(DETOUR(1.0, 0.0) > DETOUR(0.5, 0.0));
    }

    void bodies_in_transit_do_not_pass_through_each_other() {
        // Among agents still far from home, the share of close encounters in which two bodies
        // overlap by over a quarter.
        const auto OVERLAPPING = [](double separation) {
            Swarm swarm;
            SwarmParams params;
            params.separation = separation;
            swarm.begin(Transition::Open, params, 21, placement(), sides({Edge::Left}));
            const double FAR = 5.0 * std::sqrt(BODY.w * BODY.h / params.count);
            const auto IN_TRANSIT = [&](const SwarmAgent &agent) {
                return agent.visible && !agent.docked &&
                       std::hypot(
                           agent.position.x - agent.home.x, agent.position.y - agent.home.y) > FAR;
            };
            long encounters = 0;
            long overlaps = 0;
            while (swarm.active()) {
                swarm.advance(FRAME);
                const auto AGENTS = swarm.agents();
                for (auto first = AGENTS.begin(); first != AGENTS.end(); ++first) {
                    if (!IN_TRANSIT(*first)) {
                        continue;
                    }
                    for (auto second = std::next(first); second != AGENTS.end(); ++second) {
                        if (!IN_TRANSIT(*second)) {
                            continue;
                        }
                        const double DISTANCE = std::hypot(first->position.x - second->position.x,
                            first->position.y - second->position.y);
                        const double BODIES = K_SWARM_BODY_SHARE * (first->fit + second->fit);
                        encounters += DISTANCE < 2.0 * BODIES;
                        overlaps += DISTANCE < 0.75 * BODIES;
                    }
                }
            }
            CHECK(encounters > 1000);
            return static_cast<double>(overlaps) / static_cast<double>(std::max(1L, encounters));
        };
        // Free to cross, agents walk through each other all the time; with solid bodies it is rare.
        CHECK(OVERLAPPING(0.0) > 0.08);
        CHECK(OVERLAPPING(0.5) < 0.02);
        CHECK(OVERLAPPING(1.0) < 0.02);
    }

    void settling_hurries_the_landing() {
        const auto DURATION = [](double settling) {
            Swarm swarm;
            SwarmParams params;
            params.settling = settling;
            swarm.begin(Transition::Open, params, 13, placement(), sides({Edge::Bottom}));
            return run(swarm, 10.0);
        };
        CHECK(DURATION(1.0) < DURATION(0.0));
    }

    void resize_recuts_the_tiles() {
        Swarm swarm;
        swarm.begin(Transition::Open, {}, 6, placement(), sides({Edge::Left}));
        run(swarm, 0.2);
        SwarmPlacement bigger = placement();
        bigger.capture.w += 300.0;
        bigger.body.w += 300.0;
        swarm.update_movement(bigger);
        double area = 0.0;
        for (const SwarmAgent &agent : swarm.agents()) {
            area += agent.tile.w * agent.tile.h;
        }
        CHECK(std::abs(area - (bigger.capture.w * bigger.capture.h)) < 1e-6);
        run(swarm, 8.0);
        CHECK(!swarm.active() && in_formation(swarm));
    }

    void resize_keeps_agents_assigned_to_nearby_tiles() {
        Swarm swarm;
        SwarmParams params;
        params.count = 250;
        SwarmPlacement box = {
            .capture = {.x = 380, .y = 180, .w = 864, .h = 640},
            .body = {.x = 400, .y = 200, .w = 824, .h = 600},
            .screen = SCREEN,
        };
        swarm.begin_movement(params, 2, box);
        box.capture.x -= 100.0;
        box.body.x -= 100.0;
        swarm.update_movement(box);
        swarm.advance(1.0 / 60.0);
        std::vector<Vec2> homes;
        for (const auto &agent : swarm.agents()) {
            homes.push_back(agent.home);
        }
        box.capture.w -= 2.0;
        box.body.w -= 2.0;
        swarm.update_movement(box);
        auto agent_it = swarm.agents().begin();
        for (const Vec2 &previous_home : homes) {
            const auto &home = agent_it++->home;
            CHECK(std::hypot(home.x - previous_home.x, home.y - previous_home.y) < 3.0);
        }
        run(swarm, 8.0);
        CHECK(!swarm.active() && in_formation(swarm));
    }

    void movement_landings_are_randomly_staggered() {
        Swarm swarm;
        SwarmParams params;
        params.stagger = 1.5;
        swarm.begin_movement(params, 7, placement());
        swarm.update_movement(placement({.x = -200.0, .y = 0.0}));
        swarm.advance(1.0 / 60.0);
        std::vector<double> arrived(swarm.agents().size(), -1.0);
        int delayed = 0;
        // NOLINTNEXTLINE(bugprone-float-loop-counter): Preserve the FP recurrence and its exact
        // endpoint.
        for (double elapsed = 0.0; elapsed < 6.0 && swarm.active(); elapsed += FRAME) {
            swarm.advance(FRAME);
            auto agent_it = swarm.agents().begin();
            for (auto &arrival : arrived) {
                const auto &agent = *agent_it++;
                if (arrival < 0.0 && !agent.docked &&
                    std::hypot(agent.position.x - agent.home.x, agent.position.y - agent.home.y) <
                        1.5 &&
                    std::hypot(agent.velocity.x, agent.velocity.y) < 60.0) {
                    arrival = elapsed;
                }
                if (arrival >= 0.0 && agent.docked) {
                    if (elapsed - arrival > 0.25) {
                        ++delayed;
                    }
                    arrival = -2.0;
                }
            }
        }
        CHECK(delayed > 20);
        CHECK(!swarm.active() && in_formation(swarm));
    }

    void invalid_input_leaves_the_model_alone() {
        Swarm swarm;
        swarm.begin(Transition::Open, {}, 1, placement(), EdgeSet{});
        CHECK(!swarm.active());
        swarm.begin(Transition::Open,
            {},
            1,
            {.capture = {.x = 0, .y = 0, .w = 0, .h = 100}, .body = BODY, .screen = SCREEN},
            sides({Edge::Left}));
        CHECK(!swarm.active());
        SwarmParams bad;
        bad.speed = std::numeric_limits<double>::quiet_NaN();
        swarm.begin(Transition::Close, bad, 1, placement(), sides({Edge::Left}));
        CHECK(!swarm.active() && !swarm.closed());

        swarm.begin(Transition::Open, {}, 1, placement(), sides({Edge::Left}));
        run(swarm, 0.3);
        const Vec2 BEFORE = swarm.agents().front().position;
        CHECK(swarm.advance(std::numeric_limits<double>::quiet_NaN()));
        CHECK(swarm.advance(-1.0));
        CHECK(swarm.agents().front().position.x == BEFORE.x &&
              swarm.agents().front().position.y == BEFORE.y);
        // A stalled frame is clamped rather than integrated whole.
        swarm.advance(30.0);
        for (const SwarmAgent &agent : swarm.agents()) {
            CHECK(std::isfinite(agent.position.x) && std::isfinite(agent.position.y));
        }
        swarm.reset();
        CHECK(!swarm.active() && swarm.agents().empty());
    }

    void unrepresentable_cells_do_not_alias_neighbours() {
        // The quotient, not the input position, must fit the hash's integer coordinate.
        for (const double ORIGIN : {1e23, -1e23}) {
            SwarmPlacement far = placement();
            far.screen.x = ORIGIN;
            SwarmParams params;
            params.count = 2;
            params.stagger = 0.0;
            params.separation = 0.0;
            params.cohesion = 0.0;
            Swarm alone;
            alone.begin(Transition::Open, params, 31, far, sides({Edge::Left}));
            params.cohesion = 1.0;
            Swarm together;
            together.begin(Transition::Open, params, 31, far, sides({Edge::Left}));
            for (int step = 0; step < 4; ++step) {
                alone.advance(1.0 / 120.0);
                together.advance(1.0 / 120.0);
            }
            auto expected = alone.agents().begin();
            for (const SwarmAgent &agent : together.agents()) {
                CHECK(std::isfinite(agent.position.x) && std::isfinite(agent.position.y));
                CHECK(agent.position.x == expected->position.x &&
                      agent.position.y == expected->position.y);
                CHECK(agent.velocity.x == expected->velocity.x &&
                      agent.velocity.y == expected->velocity.y);
                ++expected;
            }
        }
        // Exactly -2^63 is representable, but its left neighbour cannot be added in int64_t.
        // Just below +2^63 is valid; +2^63 itself must be rejected before conversion.
        const double END = std::ldexp(192.0, 63);
        for (const double ORIGIN : {-END, std::nextafter(END, 0.0), END}) {
            SwarmPlacement edge{
                .capture = {.x = 0.0, .y = 0.0, .w = 64.0, .h = 64.0},
                .body = {.x = 0.0, .y = 0.0, .w = 64.0, .h = 64.0},
                .screen = {.x = ORIGIN, .y = 0.0, .w = 1.0, .h = 1.0},
            };
            SwarmParams params;
            params.count = 1;
            params.stagger = 0.0;
            Swarm swarm;
            swarm.begin(Transition::Open, params, 31, edge, sides({Edge::Left}));
            for (int step = 0; step < 4; ++step) {
                swarm.advance(1.0 / 120.0);
            }
            CHECK(std::isfinite(swarm.agents().front().position.x));
            CHECK(std::isfinite(swarm.agents().front().position.y));
        }
    }

    void extreme_geometry_keeps_rows_and_bounds_representable() {
        const double MAX = std::numeric_limits<double>::max();
        for (const double HEIGHT : {1e100, MAX}) {
            SwarmPlacement tall{
                .capture = {.x = 0.0, .y = 0.0, .w = 1.0, .h = HEIGHT},
                .body = {.x = 0.0, .y = 0.0, .w = 1.0, .h = HEIGHT},
                .screen = {.x = 0.0, .y = 0.0, .w = 1.0, .h = HEIGHT},
            };
            SwarmParams params;
            params.count = SWARM_MAX_AGENTS;
            Swarm swarm;
            swarm.begin_movement(params, 1, tall);
            CHECK(swarm.agents().size() == SWARM_MAX_AGENTS);
            CHECK(swarm.agents().front().tile.w == 1.0);
            CHECK(swarm.agents().front().tile.h == HEIGHT / SWARM_MAX_AGENTS);
            const Box BOUNDS = swarm.bounds();
            CHECK(std::isfinite(BOUNDS.x) && std::isfinite(BOUNDS.y) && std::isfinite(BOUNDS.w) &&
                  std::isfinite(BOUNDS.h));
        }
        // Finite inputs can produce infinite screen-relative positions, then NaN motion.
        SwarmPlacement overflow = placement();
        overflow.capture.x = -MAX;
        overflow.screen.x = MAX;
        Swarm swarm;
        SwarmParams params;
        params.stagger = 0.0;
        swarm.begin(Transition::Open, params, 1, overflow, sides({Edge::Left}));
        for (int step = 0; step < 4; ++step) {
            swarm.advance(1.0 / 120.0);
            const Box BOUNDS = swarm.bounds();
            CHECK(BOUNDS.w == 0.0 && BOUNDS.h == 0.0);
            CHECK(std::isfinite(BOUNDS.x) && std::isfinite(BOUNDS.y));
        }
        // Overflow in spacing (infinity) or underflow to zero must not produce a cell.
        for (const double SIZE : {MAX, std::numeric_limits<double>::denorm_min()}) {
            SwarmPlacement square{
                .capture = {.x = 0.0, .y = 0.0, .w = SIZE, .h = SIZE},
                .body = {.x = 0.0, .y = 0.0, .w = SIZE, .h = SIZE},
                .screen = {.x = 0.0, .y = 0.0, .w = SIZE, .h = SIZE},
            };
            swarm.reset();
            swarm.begin(Transition::Open, params, 1, square, sides({Edge::Left}));
            for (int step = 0; step < 4; ++step) {
                swarm.advance(1.0 / 120.0);
                const Box BOUNDS = swarm.bounds();
                CHECK(std::isfinite(BOUNDS.x) && std::isfinite(BOUNDS.y) &&
                      std::isfinite(BOUNDS.w) && std::isfinite(BOUNDS.h));
            }
        }
    }

    void sprite_measures_body_and_legs() {
        // A disc of radius 20 at (40, 60) with a thin leg reaching right to x = 110, in a 128 px
        // frame.
        constexpr int SIDE = 128;
        std::vector<std::uint8_t> alpha(static_cast<std::size_t>(SIDE) * SIDE, 0);
        auto pixel = alpha.begin();
        for (int row = 0; row < SIDE; ++row) {
            for (int col = 0; col < SIDE; ++col, ++pixel) {
                if (std::hypot(col + 0.5 - 40.0, row + 0.5 - 60.0) <= 20.0 ||
                    (row >= 59 && row <= 60 && col >= 40 && col < 110)) {
                    *pixel = 255;
                }
            }
        }
        const SpriteFrame FRAME_INFO = measure_sprite(alpha, SIDE);
        CHECK(std::abs((FRAME_INFO.centre.x * SIDE) - 40.0) <= 1.5 &&
              std::abs((FRAME_INFO.centre.y * SIDE) - 60.0) <= 1.5);
        CHECK(std::abs((FRAME_INFO.core * SIDE) - 20.0) <= 1.5);
        CHECK(std::abs((FRAME_INFO.reach * SIDE) - 70.0) <= 2.0);

        const SpriteFrame EMPTY = measure_sprite(
            std::vector<std::uint8_t>(static_cast<std::size_t>(SIDE) * SIDE, 0), SIDE);
        CHECK(EMPTY.core == 0.0 && EMPTY.reach == 0.0);
        CHECK(sprite_size(EMPTY, SpriteFit{40.0}, SpriteCover{60.0}, SpriteLock{0.5}) == 0.0);
        // On a 32-bit host the unchecked input square wraps to zero at this side.
        const auto TRUNCATED = measure_sprite({}, 65536);
        CHECK(TRUNCATED.core == 0 && TRUNCATED.reach == 0);

        // Loose, the whole silhouette fits; locked, the solid core covers the tile; it only grows
        // in between.
        CHECK(std::abs((FRAME_INFO.reach *
                           sprite_size(
                               FRAME_INFO, SpriteFit{40.0}, SpriteCover{60.0}, SpriteLock{0.0})) -
                       40.0) < 1e-9);
        CHECK(FRAME_INFO.core *
                  sprite_size(FRAME_INFO, SpriteFit{40.0}, SpriteCover{60.0}, SpriteLock{1.0}) >=
              60.0);
        double previous = 0.0;
        // NOLINTNEXTLINE(bugprone-float-loop-counter,clang-analyzer-security.FloatLoopCounter):
        // Preserve the FP recurrence and its exact endpoint.
        for (double lock = 0.0; lock <= 1.0; lock += 0.1) {
            const double SIZE =
                sprite_size(FRAME_INFO, SpriteFit{40.0}, SpriteCover{60.0}, SpriteLock{lock});
            CHECK(SIZE > previous);
            previous = SIZE;
        }
    }

    void walk_cycle_follows_distance() {
        CHECK(walk_frame(0.0, 80.0, 8) == 0);
        CHECK(walk_frame(45.0, 80.0, 8) == 4);
        CHECK(walk_frame(79.9, 80.0, 8) == 7);
        CHECK(walk_frame((80.0 * 12) + 15.0, 80.0, 8) == 1);
        CHECK(walk_frame(100.0, 0.0, 8) == 0);
        CHECK(walk_frame(100.0, 80.0, 1) == 0);
        // 2^60 leaves one pixel after complete three-pixel strides.
        CHECK(walk_frame(0x1p60, 3.0, 8) == 2);
        CHECK(walk_frame(-0x1p60, 3.0, 8) == 5);
        // Same remainder, but this quotient cannot even fit in a double.
        CHECK(walk_frame(1.0, 3.0 * std::numeric_limits<double>::denorm_min(), 8) == 2);
        // Facing up in the sheet and heading right is a quarter turn clockwise; half-turned is half
        // of it; the long way round is never taken.
        const double UP_ANGLE = -std::numbers::pi / 2;
        CHECK(std::abs(sprite_rotation(0.0, UP_ANGLE, 1.0) - (std::numbers::pi / 2)) < 1e-12);
        CHECK(std::abs(sprite_rotation(0.0, UP_ANGLE, 0.5) - (std::numbers::pi / 4)) < 1e-12);
        CHECK(sprite_rotation(UP_ANGLE, UP_ANGLE, 1.0) == 0.0);
        CHECK(std::abs(sprite_rotation(std::numbers::pi * 0.9, -std::numbers::pi * 0.9, 1.0) +
                       (std::numbers::pi * 0.2)) < 1e-12);
    }

    void moving_sprites_advance_at_a_visible_rate() {
        Swarm swarm;
        SwarmParams params;
        params.count = 250;
        swarm.begin(Transition::Open, params, 7, placement(), sides({Edge::Left}));
        bool observed = false;
        for (int step = 0; step < 60 && swarm.active(); ++step) {
            std::vector<double> before;
            for (const auto &prior_agent : swarm.agents()) {
                before.push_back(prior_agent.walk);
            }
            swarm.advance(1.0 / 60.0);
            auto agent_it = swarm.agents().begin();
            for (const double PREVIOUS_WALK : before) {
                const auto &agent = *agent_it++;
                if (agent.visible && !agent.docked && agent.walk > PREVIOUS_WALK) {
                    observed = true;
                    // At 60 Hz, no more than a quarter of the shortest creature cycle per frame.
                    CHECK(agent.walk - PREVIOUS_WALK <= 0.25 * 1.1 * 2.0 * agent.fit);
                }
            }
        }
        CHECK(observed);
    }

    void turning_sprites_keep_animating() {
        Swarm swarm;
        swarm.begin(Transition::Open, {}, 12, placement(), sides({Edge::Left}));
        bool observed = false;
        for (int step = 0; step < 1200 && swarm.active(); ++step) {
            const std::vector<SwarmAgent> BEFORE(swarm.agents().begin(), swarm.agents().end());
            swarm.advance(1.0 / 120.0);
            auto agent_it = swarm.agents().begin();
            for (const SwarmAgent &previous_agent : BEFORE) {
                const auto &agent = *agent_it++;
                if (previous_agent.docked && previous_agent.turn > agent.turn) {
                    observed = true;
                    CHECK(agent.walk > previous_agent.walk);
                }
            }
        }
        CHECK(observed);
    }

    void merging_sprites_keep_animating() {
        for (const bool MOVEMENT : {false, true}) {
            Swarm swarm;
            if (MOVEMENT) {
                swarm.begin_movement({}, 12, placement());
                for (int i = 0; i < 120; ++i) {
                    swarm.advance(FRAME, true);
                }
            } else {
                swarm.begin(Transition::Open, {}, 12, placement(), sides({Edge::Left}));
            }
            bool observed = false;
            bool changed_frame = false;
            for (int step = 0; step < 1200 && swarm.active(); ++step) {
                const std::vector<SwarmAgent> BEFORE(swarm.agents().begin(), swarm.agents().end());
                swarm.advance(FRAME);
                auto agent_it = swarm.agents().begin();
                for (const SwarmAgent &previous_agent : BEFORE) {
                    const auto &agent = *agent_it++;
                    if (previous_agent.docked && previous_agent.turn == 0.0 &&
                        agent.lock > previous_agent.lock && agent.lock < 1.0) {
                        observed = true;
                        CHECK(agent.walk > previous_agent.walk);
                        changed_frame |= walk_frame(agent.walk, 2.0 * agent.fit, 8) !=
                                         walk_frame(previous_agent.walk, 2.0 * agent.fit, 8);
                    }
                    if (previous_agent.lock == 1.0 && agent.lock == 1.0) {
                        CHECK(agent.walk == previous_agent.walk);
                    }
                }
            }
            CHECK(observed && changed_frame);
            CHECK(!swarm.active() && in_formation(swarm));
        }
    }

    void edge_sides_follow_the_neighbours() {
        const Box LEFT{.x = 0, .y = 0, .w = 960, .h = 1080};
        const Box RIGHT{.x = 960, .y = 0, .w = 960, .h = 1080};
        // Alone: the nearest edge; equidistant edges all count.
        CHECK(resolve_edge_sides({100, 300, 400, 400}, SCREEN, {}) == sides({Edge::Left}));
        CHECK(resolve_edge_sides({660, 340, 600, 400}, SCREEN, {}) ==
              sides({Edge::Top, Edge::Bottom}));
        // Beside a neighbour: the edge away from it.
        const std::array<Box, 1> ON_LEFT = {LEFT};
        CHECK(resolve_edge_sides(RIGHT, SCREEN, ON_LEFT) == sides({Edge::Right}));
        // Below one neighbour and beside another of equal contact: both open edges.
        const std::array<Box, 2> AROUND = {
            {
                {.x = 840, .y = 540, .w = 540, .h = 540},
                {.x = 1380, .y = 0, .w = 540, .h = 540},
            },
        };
        CHECK(resolve_edge_sides({1380, 540, 540, 540}, SCREEN, AROUND) ==
              sides({Edge::Right, Edge::Bottom}));
        // Wedged between two columns: the edges opposite its contacts are taken, so it uses the
        // free ones.
        const std::array<Box, 2> COLUMNS = {
            {
                {.x = 0, .y = 0, .w = 640, .h = 1080},
                {.x = 1280, .y = 0, .w = 640, .h = 1080},
            },
        };
        CHECK(resolve_edge_sides({640, 0, 640, 1080}, SCREEN, COLUMNS) ==
              sides({Edge::Top, Edge::Bottom}));
        CHECK(resolve_edge_sides({0, 0, 0, 0}, SCREEN, {}).empty());
    }

    void movement_retuning_preserves_agents_and_latches_close() {
        SwarmParams initial;
        initial.count = 12;
        initial.variation = 0.0;
        Swarm swarm;
        swarm.begin_movement(initial, 42, placement());
        swarm.update_movement(placement({.x = 200.0, .y = 0.0}));
        swarm.advance(FRAME, true);
        const auto BEFORE = swarm.agents().front();
        SwarmParams changed = initial;
        changed.count = 24;
        changed.speed *= 2.0;
        changed.variation = 1.0;
        changed.responsiveness = 1.0;
        swarm.set_movement_params(changed);
        CHECK(swarm.agents().size() == 12);
        CHECK(swarm.params().speed == changed.speed);
        CHECK(swarm.agents().front().position.x == BEFORE.position.x);
        CHECK(swarm.agents().front().velocity.x == BEFORE.velocity.x);
        CHECK(swarm.agents().front().grip != BEFORE.grip);
        swarm.begin(Transition::Close, initial, 43, placement(), sides({Edge::Bottom}));
        swarm.set_movement_params(changed);
        CHECK(swarm.params().speed == initial.speed);
    }

    void timed_advances_cap_steps_and_keep_exact_endpoints() {
        SwarmParams params;
        params.count = 24;
        params.speed = 1.0;  // Force the route estimate to the 6.6 s simulation-time clamp.
        const EdgeSet SIDES = sides({Edge::Left});
        const auto CHECK_CAPPED_FRAME = [](Swarm &swarm, double elapsed) {
            const double BEFORE = swarm.agents().front().due;
            CHECK(swarm.advance(elapsed));
            const double ELAPSED = BEFORE - swarm.agents().front().due;
            const int STEPS = static_cast<int>(std::lround(ELAPSED * 120.0));
            CHECK(STEPS == 30);
            CHECK(std::abs(ELAPSED - (static_cast<double>(STEPS) / 120.0)) < 1e-9);
            CHECK(std::ranges::none_of(
                swarm.agents(), [](const SwarmAgent &agent) { return agent.gone; }));
            CHECK(!in_formation(swarm));
        };

        Swarm opening;
        opening.begin(Transition::Open, params, 41, placement(), SIDES, 1000.0);
        for (int frame = 0; frame < 2; ++frame) {
            CHECK_CAPPED_FRAME(opening, 0.2);
        }
        Swarm half_progress = opening;
        Swarm later_progress = opening;
        CHECK_CAPPED_FRAME(half_progress, 0.1);
        CHECK_CAPPED_FRAME(later_progress, 0.2);
        const auto LOOSE = std::ranges::find_if(half_progress.agents(),
            [](const SwarmAgent &agent) { return agent.visible && !agent.docked; });
        CHECK(LOOSE != half_progress.agents().end());
        if (LOOSE != half_progress.agents().end()) {
            const std::size_t INDEX =
                static_cast<std::size_t>(std::distance(half_progress.agents().begin(), LOOSE));
            const auto DISTANCE_HOME = [](const SwarmAgent &agent) {
                return std::hypot(agent.position.x - agent.home.x, agent.position.y - agent.home.y);
            };
            const double BEFORE_GUIDANCE = DISTANCE_HOME(*LOOSE);
            const double AFTER_GUIDANCE = DISTANCE_HOME(later_progress.agents()[INDEX]);
            CHECK(BEFORE_GUIDANCE > 0.0);
            // smoothstep(0.5, 1.0, 0.6) is 0.104; the equal step counts isolate this guide.
            CHECK(std::abs(AFTER_GUIDANCE - (BEFORE_GUIDANCE * (1.0 - 0.104))) < 1e-6);
        }
        const double HALF_DUE = half_progress.agents().front().due;
        const double LATER_DUE = later_progress.agents().front().due;
        CHECK(half_progress.advance(0.001));
        CHECK(later_progress.advance(0.001));
        CHECK(half_progress.agents().front().due == HALF_DUE);
        CHECK(later_progress.agents().front().due == LATER_DUE);
        for (int frame = 2; frame < 4; ++frame) {
            CHECK_CAPPED_FRAME(opening, 0.2);
        }
        CHECK(opening.active() && !in_formation(opening));
        CHECK(!opening.advance(0.2));
        CHECK(!opening.active() && in_formation(opening));

        Swarm closing;
        closing.begin(Transition::Close, params, 42, placement(), SIDES, 1000.0);
        for (int frame = 0; frame < 4; ++frame) {
            CHECK_CAPPED_FRAME(closing, 0.2);
            CHECK(!closing.closed());
        }
        CHECK(closing.active() && !closing.closed());
        CHECK(!closing.advance(0.2));
        CHECK(closing.closed() && !closing.active());
        CHECK(closing.bounds().w == 0.0);
        CHECK(std::ranges::all_of(closing.agents(),
            [](const SwarmAgent &agent) { return agent.gone && !agent.visible; }));
    }

    void capped_advances_release_scheduled_agents() {
        constexpr double SIMULATION_SECONDS = 11.0;
        constexpr double REQUESTED_SECONDS = 0.5;
        constexpr double ACTUAL_TIME = 30.0 / 120.0;
        constexpr double MIN_SCHEDULED = ACTUAL_TIME + (1.0 / 120.0) + 1e-6;
        constexpr double MAX_SCHEDULED = (0.25 / REQUESTED_SECONDS) * SIMULATION_SECONDS;
        SwarmParams params;
        params.count = SWARM_MAX_AGENTS;
        params.speed = 50.0;  // Long routes hit the 5 s stagger + 6 s close-deadline clamp.
        params.stagger = 5.0;
        params.variation = 0.0;
        const EdgeSet SIDES = sides({Edge::Left});

        const auto CHECK_DELAYED_RELEASES = [&](Transition kind, std::uint64_t seed) {
            Swarm swarm;
            swarm.begin(kind, params, seed, placement(), SIDES, REQUESTED_SECONDS * 1000.0);
            const auto AGENTS = swarm.agents();
            std::size_t release_index = AGENTS.size();
            std::size_t waiting_index = AGENTS.size();
            double scheduled = 0.0;
            for (std::size_t release = 0; release < AGENTS.size() && release_index == AGENTS.size();
                ++release) {
                if (AGENTS[release].gone || AGENTS[release].delay <= ACTUAL_TIME) {
                    continue;
                }
                const double LOWER = std::max(AGENTS[release].delay, MIN_SCHEDULED);
                if (!(LOWER < MAX_SCHEDULED)) {
                    continue;
                }
                for (std::size_t waiting = 0; waiting < AGENTS.size(); ++waiting) {
                    if (AGENTS[waiting].gone || AGENTS[waiting].delay <= LOWER) {
                        continue;
                    }
                    const double UPPER = std::min(AGENTS[waiting].delay, MAX_SCHEDULED);
                    if (UPPER > LOWER) {
                        release_index = release;
                        waiting_index = waiting;
                        scheduled = LOWER + (0.5 * (UPPER - LOWER));
                        break;
                    }
                }
            }
            const bool FOUND = release_index < AGENTS.size() && waiting_index < AGENTS.size();
            CHECK(FOUND);
            if (!FOUND) {
                return;
            }

            const double RELEASE_DUE = AGENTS[release_index].due;
            const double WAITING_DUE = AGENTS[waiting_index].due;
            const double ELAPSED_SECONDS = (scheduled / SIMULATION_SECONDS) * REQUESTED_SECONDS;
            CHECK(ELAPSED_SECONDS < 0.25);
            CHECK(swarm.advance(ELAPSED_SECONDS));
            const double RELEASE_DUE_DELTA = RELEASE_DUE - swarm.agents()[release_index].due;
            const double WAITING_DUE_DELTA = WAITING_DUE - swarm.agents()[waiting_index].due;
            CHECK(std::abs(RELEASE_DUE_DELTA - ACTUAL_TIME) < 1e-9);
            CHECK(std::abs(WAITING_DUE_DELTA - ACTUAL_TIME) < 1e-9);

            const SwarmAgent &RELEASED = swarm.agents()[release_index];
            const SwarmAgent &WAITING = swarm.agents()[waiting_index];
            CHECK(RELEASED.delay == 0.0 && RELEASED.visible && !RELEASED.docked);
            CHECK(WAITING.delay > 0.0);
            if (kind == Transition::Open) {
                CHECK(!WAITING.visible && !WAITING.docked);
            } else {
                CHECK(WAITING.visible && WAITING.docked);
            }
        };
        CHECK_DELAYED_RELEASES(Transition::Open, 43);
        CHECK_DELAYED_RELEASES(Transition::Close, 44);
    }

    void durations_retime_opening_and_closing_motion() {
        SwarmParams params;
        params.count = 24;
        const EdgeSet SIDES = sides({Edge::Left});
        for (const Transition KIND : {Transition::Open, Transition::Close}) {
            Swarm quick;
            Swarm slow;
            quick.begin(KIND, params, 31, placement(), SIDES, 125.5);
            slow.begin(KIND, params, 31, placement(), SIDES, 2008.0);
            for (int i = 0; i < 16; ++i) {
                quick.advance(0.06275 / 16.0);
                slow.advance(0.06275);
            }
            CHECK(quick.active() && slow.active());
            CHECK(quick.bounds().w > 0.0);
            CHECK(!in_formation(quick));
            auto peer = slow.agents().begin();
            for (const auto &agent : quick.agents()) {
                CHECK(std::abs(agent.position.x - peer->position.x) < 1e-6);
                CHECK(std::abs(agent.lock - peer->lock) < 1e-6);
                ++peer;
            }
            for (int i = 0; i < 16; ++i) {
                const bool QUICK_ACTIVE = quick.advance(0.06275 / 16.0);
                const bool SLOW_ACTIVE = slow.advance(0.06275);
                CHECK(QUICK_ACTIVE == (i < 15));
                CHECK(SLOW_ACTIVE == (i < 15));
            }
            CHECK(!slow.active());
            if (KIND == Transition::Open) {
                CHECK(in_formation(quick) && in_formation(slow));
            } else {
                CHECK(quick.closed() && slow.closed());
                CHECK(quick.bounds().w == 0.0 && slow.bounds().w == 0.0);
            }
        }
        Swarm skipped;
        skipped.begin(Transition::Open, params, 31, placement(), SIDES, 0.0);
        CHECK(!skipped.active() && skipped.agents().empty());
        skipped.begin(Transition::Close, params, 31, placement(), SIDES, 0.0);
        CHECK(!skipped.active() && !skipped.closed());

        Swarm interrupted;
        interrupted.begin(Transition::Open, params, 31, placement(), SIDES, 300.0);
        interrupted.advance(0.15);
        const auto SHOWING = std::ranges::find_if(interrupted.agents(),
            [](const SwarmAgent &agent) { return agent.visible && !agent.docked; });
        CHECK(SHOWING != interrupted.agents().end());
        if (SHOWING != interrupted.agents().end()) {
            const Vec2 POSITION = SHOWING->position;
            interrupted.begin(Transition::Close, params, 32, placement(), SIDES, 80.25);
            CHECK(interrupted.active() && !interrupted.opening());
            CHECK(SHOWING->position.x == POSITION.x && SHOWING->position.y == POSITION.y);
            CHECK(interrupted.advance(0.04));
            CHECK(!interrupted.advance(0.041) && interrupted.closed());
        }
    }

}  // namespace

int main() {
    // Bare names resolve under the sprite directory; explicit paths stay unchanged.
    CHECK(hyprimate::sprite_path(hyprimate::SpriteName{"cat"},
              hyprimate::SpriteDirectory{"/sprites"}) == "/sprites/cat.png");
    CHECK(hyprimate::sprite_path(hyprimate::SpriteName{"bird.png"},
              hyprimate::SpriteDirectory{"/sprites"}) == "/sprites/bird.png");
    CHECK(hyprimate::sprite_path(hyprimate::SpriteName{"/tmp/custom.png"},
              hyprimate::SpriteDirectory{"/sprites"}) == "/tmp/custom.png");
    CHECK(hyprimate::sprite_path(hyprimate::SpriteName{"./custom.png"},
              hyprimate::SpriteDirectory{"/sprites"}) == "./custom.png");
    CHECK(hyprimate::sprite_path(hyprimate::SpriteName{""}, hyprimate::SpriteDirectory{"/sprites"})
            .empty());
    tiles_partition_the_capture();
    opening_enters_from_its_sides_and_settles_exactly();
    equal_seeds_give_equal_motion();
    seeded_crowded_trajectory();
    close_lets_go_in_no_order_and_leaves_through_its_side();
    tiles_fill_in_no_order();
    surface_rounding_waits_for_formation();
    closing_rounds_the_surface_as_parts_leave();
    close_takes_over_an_opening();
    close_inherits_an_in_flight_handoff();
    movement_without_motion_finishes_immediately();
    slow_movement_keeps_the_window_whole();
    workspace_slides_preserve_independent_flight();
    workspace_reversal_preserves_screen_positions();
    fast_movement_scatters_then_regroups();
    held_movement_wanders_until_release();
    held_movement_respects_wander();
    departures_are_staggered();
    routes_are_not_straight_lines();
    bodies_in_transit_do_not_pass_through_each_other();
    settling_hurries_the_landing();
    resize_recuts_the_tiles();
    resize_keeps_agents_assigned_to_nearby_tiles();
    movement_landings_are_randomly_staggered();
    invalid_input_leaves_the_model_alone();
    unrepresentable_cells_do_not_alias_neighbours();
    extreme_geometry_keeps_rows_and_bounds_representable();
    sprite_measures_body_and_legs();
    walk_cycle_follows_distance();
    moving_sprites_advance_at_a_visible_rate();
    turning_sprites_keep_animating();
    merging_sprites_keep_animating();
    movement_retuning_preserves_agents_and_latches_close();
    durations_retime_opening_and_closing_motion();
    timed_advances_cap_steps_and_keep_exact_endpoints();
    capped_advances_release_scheduled_agents();
    edge_sides_follow_the_neighbours();
    return g_failures == 0 ? 0 : 1;
}
