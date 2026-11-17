#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

#include "Check.hpp"
#include "animations/shared/EdgeTransition.hpp"
#include "animations/swarm/Swarm.hpp"
#include "animations/swarm/SwarmSprite.hpp"

namespace {
    using namespace hyprimate;

    constexpr Box SCREEN{0, 0, 1920, 1080};
    constexpr Box BODY{400, 200, 900, 600};
    constexpr Box CAPTURE{380, 180, 940, 640};  // 20 px of shadow around the body
    constexpr double FRAME = 1.0 / 144.0;

    SwarmPlacement placement(Vec2 shift = {}) {
        return {{CAPTURE.x + shift.x, CAPTURE.y + shift.y, CAPTURE.w, CAPTURE.h},
            {BODY.x + shift.x, BODY.y + shift.y, BODY.w, BODY.h},
            SCREEN};
    }
    EdgeSet sides(std::initializer_list<Edge> edges) {
        EdgeSet s;
        for (Edge e : edges) s.add(e);
        return s;
    }
    // Runs until the model stops or `seconds` pass; returns the time used.
    double run(Swarm &m, double seconds, double step = FRAME) {
        double t = 0.0;
        while (t < seconds && m.active()) {
            m.advance(step);
            t += step;
        }
        return t;
    }
    // Agent position in the placement's space, for a window that has not moved.
    Vec2 onScreen(const SwarmAgent &a) {
        return {CAPTURE.x + a.position.x, CAPTURE.y + a.position.y};
    }
    bool inFormation(const Swarm &m) {
        return m.rounding() == 0.0 && std::ranges::all_of(m.agents(), [](const SwarmAgent &a) {
            return a.docked && a.lock == 1.0 && a.turn == 0.0 && a.position.x == a.home.x &&
                   a.position.y == a.home.y;
        });
    }
    // Pearson correlation of two equally long series.
    double correlation(const std::vector<double> &a, const std::vector<double> &b) {
        double meanA = 0.0, meanB = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            meanA += a[i] / a.size(), meanB += b[i] / b.size();
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            ab += (a[i] - meanA) * (b[i] - meanB);
            aa += (a[i] - meanA) * (a[i] - meanA);
            bb += (b[i] - meanB) * (b[i] - meanB);
        }
        return ab / std::sqrt(aa * bb);
    }
    // When each agent of an opening first docks, in seconds; the model is run to its end.
    std::vector<double> landings(Swarm &m) {
        std::vector<double> at(m.agents().size(), -1.0);
        for (double t = 0.0; m.active() && t < 30.0; t += FRAME) {
            m.advance(FRAME);
            for (std::size_t i = 0; i < at.size(); ++i) {
                if (at[i] < 0.0 && m.agents()[i].docked) at[i] = t;
            }
        }
        return at;
    }

    void tilesPartitionTheCapture() {
        for (int count : {1, 2, 7, 80, 400}) {
            Swarm m;
            SwarmParams p;
            p.count = count;
            m.beginMovement(p, 1, placement());
            CHECK(static_cast<int>(m.agents().size()) == count);
            double area = 0.0, x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
            for (const SwarmAgent &a : m.agents()) {
                area += a.tile.w * a.tile.h;
                x0 = std::min(x0, a.tile.x), y0 = std::min(y0, a.tile.y);
                x1 = std::max(x1, a.tile.x + a.tile.w), y1 = std::max(y1, a.tile.y + a.tile.h);
                // The creature fits inside the body's share of its own tile.
                CHECK(a.home.x - a.fit >= a.tile.x - 1e-9 &&
                      a.home.x + a.fit <= a.tile.x + a.tile.w + 1e-9);
                CHECK(a.home.y - a.fit >= a.tile.y - 1e-9 &&
                      a.home.y + a.fit <= a.tile.y + a.tile.h + 1e-9);
                CHECK(
                    a.home.x >= 20.0 && a.home.x <= 920.0 && a.home.y >= 20.0 && a.home.y <= 620.0);
            }
            // Same total area and same outline as the capture: the tiles cover it without overlap.
            CHECK(std::abs(area - CAPTURE.w * CAPTURE.h) < 1e-6);
            CHECK(x0 == 0.0 && y0 == 0.0 && x1 == CAPTURE.w && y1 == CAPTURE.h);
        }
        // Near-square pieces: 80 agents on a 3:2 body.
        Swarm m;
        SwarmParams p;
        p.count = 80;
        m.beginMovement(p, 1, placement());
        for (const SwarmAgent &a : m.agents()) CHECK(a.fit > 30.0 && a.fit < 60.0);
    }

    void openingEntersFromItsSidesAndSettlesExactly() {
        Swarm m;
        m.begin(Transition::Open, {}, 7, placement(), sides({Edge::Top, Edge::Right}));
        CHECK(m.opening() && m.active());
        for (const SwarmAgent &a : m.agents()) {
            const Vec2 p = onScreen(a);
            CHECK(!a.visible && a.lock == 0.0);
            CHECK(p.y < SCREEN.y || p.x > SCREEN.x + SCREEN.w);
        }
        // Nothing is on screen yet, so there is nothing to draw.
        CHECK(m.bounds().w == 0.0);

        run(m, 0.8);
        CHECK(m.active());
        CHECK(std::ranges::any_of(m.agents(), [](const SwarmAgent &a) { return a.visible; }));
        CHECK(m.bounds().w > 0.0);

        // Well inside the point at which stragglers are placed by force.
        const double used = 0.8 + run(m, 8.0);
        CHECK(!m.active() && !m.closed());
        CHECK(used < 4.5);
        CHECK(inFormation(m));
    }

    void equalSeedsGiveEqualMotion() {
        const auto midFlight = [](std::uint64_t seed) {
            Swarm m;
            m.begin(Transition::Open, {}, seed, placement(), sides({Edge::Left}));
            run(m, 0.4);
            std::vector<Vec2> out;
            for (const SwarmAgent &a : m.agents()) out.push_back(a.position);
            return out;
        };
        const auto a = midFlight(3), b = midFlight(3), c = midFlight(4);
        bool same = true, differs = false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            same = same && a[i].x == b[i].x && a[i].y == b[i].y;
            differs = differs || a[i].x != c[i].x || a[i].y != c[i].y;
        }
        CHECK(same);
        CHECK(differs);
    }

    void closeLetsGoInNoOrderAndLeavesThroughItsSide() {
        Swarm m;
        m.begin(Transition::Close, {}, 5, placement(), sides({Edge::Bottom}));
        CHECK(m.active() && !m.closed());
        CHECK(inFormation(m));

        // Half way through the stagger about half have let go, and which half has nothing to do
        // with where they stand: the window does not come apart from the side it leaves through.
        run(m, 0.3);
        std::vector<double> loose, row, column;
        for (const SwarmAgent &a : m.agents()) {
            loose.push_back(a.docked ? 0.0 : 1.0);
            row.push_back(a.home.y);
            column.push_back(a.home.x);
        }
        const auto gone = std::ranges::count(loose, 1.0);
        CHECK(gone > static_cast<long>(loose.size()) / 4 &&
              gone < 3 * static_cast<long>(loose.size()) / 4);
        CHECK(std::abs(correlation(loose, row)) < 0.2);
        CHECK(std::abs(correlation(loose, column)) < 0.2);

        const double used = 0.3 + run(m, 8.0);
        CHECK(m.closed() && !m.active());
        CHECK(used < 4.0);
        // They leave through the whole length of the edge, not just the stretch under the window.
        double leftmost = 1e9, rightmost = -1e9;
        for (const SwarmAgent &a : m.agents()) {
            CHECK(a.gone && !a.visible);
            CHECK(onScreen(a).y > SCREEN.y + SCREEN.h);
            leftmost = std::min(leftmost, onScreen(a).x),
            rightmost = std::max(rightmost, onScreen(a).x);
        }
        CHECK(leftmost < BODY.x - 200.0 && rightmost > BODY.x + BODY.w + 200.0);
        CHECK(m.bounds().w == 0.0);
    }

    void tilesFillInNoOrder() {
        // From one edge, tiles near it are not filled before tiles far from it, and agents do not
        // walk in along their own rows: where one enters says nothing about where its tile is.
        Swarm m;
        m.begin(Transition::Open, {}, 7, placement(), sides({Edge::Left}));
        std::vector<double> column, row, entry;
        for (const SwarmAgent &a : m.agents()) {
            column.push_back(a.home.x);
            row.push_back(a.home.y);
            entry.push_back(a.position.y);
        }
        CHECK(std::abs(correlation(entry, row)) < 0.2);
        const double top = *std::ranges::min_element(entry),
                     bottom = *std::ranges::max_element(entry);
        CHECK(CAPTURE.y + top < SCREEN.y + 0.1 * SCREEN.h &&
              CAPTURE.y + bottom > SCREEN.y + 0.9 * SCREEN.h);
        const std::vector<double> landed = landings(m);
        CHECK(inFormation(m));
        CHECK(std::abs(correlation(landed, column)) < 0.25);
        CHECK(std::abs(correlation(landed, row)) < 0.25);

        // With several edges to choose from, each is used about equally.
        Swarm all;
        all.begin(Transition::Open,
            {},
            8,
            placement(),
            sides({Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}));
        int fromLeft = 0, fromTop = 0;
        for (const SwarmAgent &a : all.agents()) {
            fromLeft += onScreen(a).x < SCREEN.x;
            fromTop += onScreen(a).y < SCREEN.y;
        }
        const int count = static_cast<int>(all.agents().size());
        CHECK(fromLeft > count / 8 && fromLeft < 3 * count / 8);
        CHECK(fromTop > count / 8 && fromTop < 3 * count / 8);
    }

    void surfaceRoundingWaitsForFormation() {
        for (bool movement : {false, true}) {
            Swarm m;
            if (movement) {
                m.beginMovement({}, 17, placement());
                CHECK(m.rounding() == 0.0);
                for (int i = 0; i < 120; ++i) m.advance(1.0 / 120.0, true);
            } else {
                m.begin(Transition::Open, {}, 17, placement(), sides({Edge::Left, Edge::Top}));
            }
            CHECK(m.rounding() == 1.0);
            bool sawJoining = false, sawHandoff = false;
            for (int frame = 0; frame < 1200 && m.active(); ++frame) {
                m.advance(1.0 / 120.0);
                const bool whole = std::ranges::all_of(m.agents(),
                    [](const SwarmAgent &a) { return a.docked && a.turn == 0.0 && a.lock == 1.0; });
                if (!whole) {
                    sawJoining = true;
                    CHECK(m.rounding() == 1.0);
                    CHECK(m.active());
                } else if (m.rounding() > 0.0) {
                    sawHandoff = true;
                    CHECK(m.active());
                }
            }
            CHECK(sawJoining && sawHandoff);
            CHECK(!m.active() && inFormation(m));
        }
    }

    void closingRoundsTheSurfaceAsPartsLeave() {
        Swarm m;
        m.begin(Transition::Close, {}, 5, placement(), sides({Edge::Bottom}));
        CHECK(m.rounding() == 0.0 && inFormation(m));
        m.advance(1.0 / 120.0);
        CHECK(m.rounding() > 0.0 && m.rounding() < 1.0);
        run(m, 0.25);
        CHECK(m.rounding() == 1.0);
        run(m, 8.0);
        CHECK(m.closed());
    }

    void closeTakesOverAnOpening() {
        Swarm m;
        m.begin(Transition::Open, {}, 9, placement(), sides({Edge::Left}));
        run(m, 0.25);
        std::vector<Vec2> before;
        const double rounding = m.rounding();
        for (const SwarmAgent &a : m.agents()) before.push_back(a.position);
        m.begin(Transition::Close, {}, 10, placement(), sides({Edge::Left}));
        CHECK(m.active() && !m.opening());
        CHECK(m.rounding() == rounding);
        // Agents carry on from where they were instead of jumping into formation.
        for (std::size_t i = 0; i < before.size(); ++i)
            CHECK(
                m.agents()[i].position.x == before[i].x && m.agents()[i].position.y == before[i].y);
        run(m, 8.0);
        CHECK(m.closed());
    }

    void closeInheritsAnInFlightHandoff() {
        Swarm m;
        m.begin(Transition::Open, {}, 9, placement(), sides({Edge::Left}));
        for (int frame = 0; frame < 1200 && m.rounding() == 1.0; ++frame) m.advance(1.0 / 120.0);
        const double rounding = m.rounding();
        CHECK(m.active() && rounding > 0.0 && rounding < 1.0);
        m.begin(Transition::Close, {}, 10, placement(), sides({Edge::Left}));
        CHECK(m.rounding() == rounding);
        m.advance(1.0 / 120.0);
        CHECK(m.rounding() > rounding);
        run(m, 8.0);
        CHECK(m.closed());
    }

    void movementWithoutMotionFinishesImmediately() {
        Swarm m;
        m.beginMovement({}, 2, placement());
        CHECK(m.rounding() == 0.0);
        m.advance(1.0 / 120.0);
        CHECK(!m.active() && inFormation(m));
    }

    void slowMovementKeepsTheWindowWhole() {
        Swarm m;
        m.beginMovement({}, 2, placement());
        CHECK(m.moving());
        // 30 px/s is under every agent's grip, so the window never comes apart.
        m.updateMovement(placement({30.0 * FRAME, 0.0}));
        m.advance(FRAME);
        CHECK(!m.active());
        CHECK(inFormation(m));
    }

    void workspaceSlidesPreserveIndependentFlight() {
        for (const Vec2 travel :
            {Vec2{-SCREEN.w, 0}, Vec2{SCREEN.w, 0}, Vec2{0, -SCREEN.h}, Vec2{0, SCREEN.h}}) {
            for (bool incoming : {false, true}) {
                Swarm m;
                Vec2 offset = incoming ? Vec2{-travel.x, -travel.y} : Vec2{};
                m.beginMovement({}, 2, placement(offset));
                for (int frame = 0; frame < 40; ++frame) {
                    offset.x += travel.x / 40.0;
                    offset.y += travel.y / 40.0;
                    m.updateMovement(placement(offset));
                    m.advance(FRAME);
                }
                // The native slide is over, but independently moving creatures are still flying.
                CHECK(m.active());
                if (!incoming) CHECK(m.bounds().w > 0.0 && m.bounds().h > 0.0);
                std::vector<Vec2> before;
                for (const auto &a : m.agents()) before.push_back(a.position);
                run(m, 0.1);
                double flight = 0.0;
                for (std::size_t i = 0; i < before.size(); ++i)
                    flight += std::hypot(m.agents()[i].position.x - before[i].x,
                        m.agents()[i].position.y - before[i].y);
                CHECK(flight / before.size() > 1.0);
                run(m, 8.0);
                CHECK(inFormation(m));
                if (incoming)
                    CHECK(m.bounds().w > 0.0 && m.bounds().h > 0.0);
                else
                    CHECK(m.bounds().w == 0.0 && m.bounds().h == 0.0);
            }
        }
    }

    void workspaceReversalPreservesScreenPositions() {
        Swarm baseline;
        baseline.begin(Transition::Open, {}, 9, placement(), sides({Edge::Left}));
        run(baseline, 0.25);
        Swarm moved = baseline;
        Vec2 offset{};
        for (const Vec2 delta : {Vec2{-300, 40}, Vec2{500, -80}, Vec2{-200, 40}}) {
            offset.x += delta.x;
            offset.y += delta.y;
            moved.updateMovement(placement(offset));
            // Apply the coordinate change without consuming a physics substep.
            moved.advance(1e-6);
            CHECK(moved.opening());
            for (std::size_t i = 0; i < moved.agents().size(); ++i) {
                const auto &a = moved.agents()[i];
                const auto &b = baseline.agents()[i];
                CHECK(std::abs(a.position.x + offset.x - b.position.x) < 1e-8);
                CHECK(std::abs(a.position.y + offset.y - b.position.y) < 1e-8);
                CHECK(a.visible == b.visible && a.lock == b.lock);
            }
        }
    }

    void fastMovementScattersThenRegroups() {
        Swarm m;
        m.beginMovement({}, 2, placement());
        Vec2 shift{};
        for (int i = 0; i < 40; ++i) {  // 3000 px/s to the left for ~0.28 s
            shift.x -= 3000.0 * FRAME;
            m.updateMovement(placement(shift));
            m.advance(FRAME);
        }
        CHECK(m.active());
        double trailing = 0.0, lock = 0.0;
        for (const SwarmAgent &a : m.agents()) {
            // Loose agents trail behind a window that moved left: they sit to the right of their
            // homes.
            trailing += a.position.x - a.home.x;
            lock += a.lock;
            CHECK(std::isfinite(a.position.x) && std::isfinite(a.position.y));
        }
        CHECK(trailing / m.agents().size() > 40.0);
        CHECK(lock / m.agents().size() < 0.2);

        const double used = run(m, 8.0);
        CHECK(!m.active());
        CHECK(used < 4.0);
        CHECK(inFormation(m));
    }

    void heldMovementWandersUntilRelease() {
        for (double wander : {0.1, 0.5, 1.0}) {
            Swarm m;
            SwarmParams p;
            p.wander = wander;
            m.beginMovement(p, 23, placement());
            // No translations at all, including a pause longer than the forced-settle deadline.
            for (int interval = 0; interval < 20; ++interval) {
                const std::vector<SwarmAgent> before(m.agents().begin(), m.agents().end());
                for (int frame = 0; frame < 60; ++frame) m.advance(1.0 / 120.0, true);
                CHECK(m.moving());
                for (std::size_t i = 0; i < before.size(); ++i) {
                    const auto &a = m.agents()[i];
                    CHECK(!a.docked);
                    CHECK(a.travel > before[i].travel + 0.01);
                    CHECK(a.walk > before[i].walk);
                    CHECK(std::isfinite(a.position.x) && std::isfinite(a.position.y));
                }
            }
            run(m, 10.0);
            CHECK(!m.active());
            CHECK(inFormation(m));
        }
    }

    void heldMovementRespectsWander() {
        const auto distance = [](double wander) {
            Swarm m;
            SwarmParams p;
            p.count = 1;
            p.wander = wander;
            m.beginMovement(p, 23, placement());
            for (int frame = 0; frame < 600; ++frame) m.advance(1.0 / 120.0, true);
            if (wander == 0.0) {
                CHECK(!m.active());
                CHECK(inFormation(m));
            }
            return m.agents()[0].travel;
        };
        CHECK(distance(0.0) == 0.0);
        CHECK(distance(1.0) > distance(0.1) * 2.0);
    }

    void departuresAreStaggered() {
        // How long the middle four fifths of an opening's agents take to land.
        const auto landingSpan = [](double stagger) {
            Swarm m;
            SwarmParams p;
            p.stagger = stagger;
            m.begin(Transition::Open, p, 5, placement(), sides({Edge::Left}));
            m.advance(2.0 * FRAME);
            // The first to go goes at once, so the animation never opens on a wait.
            CHECK(std::ranges::any_of(m.agents(), [](const SwarmAgent &a) { return a.visible; }));
            std::vector<double> at = landings(m);
            std::ranges::sort(at);
            return at[at.size() * 9 / 10] - at[at.size() / 10];
        };
        const auto holding = [](double stagger, double seconds) {
            Swarm m;
            SwarmParams p;
            p.stagger = stagger;
            m.begin(Transition::Close, p, 5, placement(), sides({Edge::Bottom}));
            run(m, seconds);
            return std::ranges::count_if(m.agents(), [](const SwarmAgent &a) { return a.docked; });
        };
        const long all = SwarmParams{}.count;
        const double first = 2.0 * FRAME;  // long enough for one simulation step
        // An opening's agents are timed to land over the stagger, wherever they come from.
        CHECK(landingSpan(2.0) > landingSpan(0.0) + 1.0);
        // A closing window lets every agent go at once without a stagger; with one, the first goes
        // at once and the rest follow steadily until the stagger is up.
        CHECK(holding(0.0, first) == 0);
        CHECK(holding(1.0, first) > 9 * all / 10 && holding(1.0, first) < all);
        CHECK(holding(1.0, 0.5) > all / 4 && holding(1.0, 0.5) < 3 * all / 4);
        CHECK(holding(1.0, 1.05) == 0);

        // Movement: agents shaken loose do not all give chase in the same instant.
        Swarm m;
        m.beginMovement({}, 2, placement());
        Vec2 shift{};
        for (int i = 0; i < 12; ++i) {  // 3000 px/s for ~0.08 s
            shift.x -= 3000.0 * FRAME;
            m.updateMovement(placement(shift));
            m.advance(FRAME);
        }
        const auto waiting =
            std::ranges::count_if(m.agents(), [](const SwarmAgent &a) { return a.travel == 0.0; });
        CHECK(waiting > all / 4 && waiting < all);
    }

    void routesAreNotStraightLines() {
        // Mean distance walked over the straight-line distance, with nothing in the way.
        const auto detour = [](double wander, double variation) {
            Swarm m;
            SwarmParams p;
            p.wander = wander;
            p.variation = variation;
            p.separation = 0.0;
            p.cohesion = 0.0;
            m.begin(Transition::Open, p, 31, placement(), sides({Edge::Left}));
            std::vector<double> straight;
            for (const SwarmAgent &a : m.agents())
                straight.push_back(std::hypot(a.position.x - a.home.x, a.position.y - a.home.y));
            run(m, 10.0);
            CHECK(inFormation(m));
            double total = 0.0;
            for (std::size_t i = 0; i < straight.size(); ++i)
                total += m.agents()[i].travel / straight[i];
            return total / straight.size();
        };
        // With neither meander nor per-agent arcs an agent walks straight home; each of the two
        // bends its path.
        CHECK(std::abs(detour(0.0, 0.0) - 1.0) < 0.01);
        CHECK(detour(1.0, 0.0) > 1.06);
        CHECK(detour(0.0, 1.0) > 1.06);
        CHECK(detour(1.0, 0.0) > detour(0.5, 0.0));
    }

    void bodiesInTransitDoNotPassThroughEachOther() {
        // Among agents still far from home, the share of close encounters in which two bodies
        // overlap by over a quarter.
        const auto overlapping = [](double separation) {
            Swarm m;
            SwarmParams p;
            p.separation = separation;
            m.begin(Transition::Open, p, 21, placement(), sides({Edge::Left}));
            const double far = 5.0 * std::sqrt(BODY.w * BODY.h / p.count);
            const auto inTransit = [&](const SwarmAgent &a) {
                return a.visible && !a.docked &&
                       std::hypot(a.position.x - a.home.x, a.position.y - a.home.y) > far;
            };
            long encounters = 0, overlaps = 0;
            while (m.active()) {
                m.advance(FRAME);
                const auto agents = m.agents();
                for (std::size_t i = 0; i < agents.size(); ++i) {
                    if (!inTransit(agents[i])) continue;
                    for (std::size_t j = i + 1; j < agents.size(); ++j) {
                        if (!inTransit(agents[j])) continue;
                        const double d = std::hypot(agents[i].position.x - agents[j].position.x,
                            agents[i].position.y - agents[j].position.y);
                        const double bodies = kSwarmBodyShare * (agents[i].fit + agents[j].fit);
                        encounters += d < 2.0 * bodies;
                        overlaps += d < 0.75 * bodies;
                    }
                }
            }
            CHECK(encounters > 1000);
            return static_cast<double>(overlaps) / std::max(1L, encounters);
        };
        // Free to cross, agents walk through each other all the time; with solid bodies it is rare.
        CHECK(overlapping(0.0) > 0.08);
        CHECK(overlapping(0.5) < 0.02);
        CHECK(overlapping(1.0) < 0.02);
    }

    void settlingHurriesTheLanding() {
        const auto duration = [](double settling) {
            Swarm m;
            SwarmParams p;
            p.settling = settling;
            m.begin(Transition::Open, p, 13, placement(), sides({Edge::Bottom}));
            return run(m, 10.0);
        };
        CHECK(duration(1.0) < duration(0.0));
    }

    void resizeRecutsTheTiles() {
        Swarm m;
        m.begin(Transition::Open, {}, 6, placement(), sides({Edge::Left}));
        run(m, 0.2);
        SwarmPlacement bigger = placement();
        bigger.capture.w += 300.0;
        bigger.body.w += 300.0;
        m.updateMovement(bigger);
        double area = 0.0;
        for (const SwarmAgent &a : m.agents()) area += a.tile.w * a.tile.h;
        CHECK(std::abs(area - bigger.capture.w * bigger.capture.h) < 1e-6);
        run(m, 8.0);
        CHECK(!m.active() && inFormation(m));
    }

    void resizeKeepsAgentsAssignedToNearbyTiles() {
        Swarm m;
        SwarmParams p;
        p.count = 250;
        SwarmPlacement box = {{380, 180, 864, 640}, {400, 200, 824, 600}, SCREEN};
        m.beginMovement(p, 2, box);
        box.capture.x -= 100.0;
        box.body.x -= 100.0;
        m.updateMovement(box);
        m.advance(1.0 / 60.0);
        std::vector<Vec2> homes;
        for (const auto &a : m.agents()) homes.push_back(a.home);
        box.capture.w -= 2.0;
        box.body.w -= 2.0;
        m.updateMovement(box);
        for (std::size_t i = 0; i < homes.size(); ++i) {
            const auto &home = m.agents()[i].home;
            CHECK(std::hypot(home.x - homes[i].x, home.y - homes[i].y) < 3.0);
        }
        run(m, 8.0);
        CHECK(!m.active() && inFormation(m));
    }

    void movementLandingsAreRandomlyStaggered() {
        Swarm m;
        SwarmParams p;
        p.stagger = 1.5;
        m.beginMovement(p, 7, placement());
        m.updateMovement(placement({-200.0, 0.0}));
        m.advance(1.0 / 60.0);
        std::vector<double> arrived(m.agents().size(), -1.0);
        int delayed = 0;
        for (double t = 0.0; t < 6.0 && m.active(); t += FRAME) {
            m.advance(FRAME);
            for (std::size_t i = 0; i < arrived.size(); ++i) {
                const auto &a = m.agents()[i];
                if (arrived[i] < 0.0 && !a.docked &&
                    std::hypot(a.position.x - a.home.x, a.position.y - a.home.y) < 1.5 &&
                    std::hypot(a.velocity.x, a.velocity.y) < 60.0)
                    arrived[i] = t;
                if (arrived[i] >= 0.0 && a.docked) {
                    if (t - arrived[i] > 0.25) ++delayed;
                    arrived[i] = -2.0;
                }
            }
        }
        CHECK(delayed > 20);
        CHECK(!m.active() && inFormation(m));
    }

    void invalidInputLeavesTheModelAlone() {
        Swarm m;
        m.begin(Transition::Open, {}, 1, placement(), EdgeSet{});
        CHECK(!m.active());
        m.begin(Transition::Open, {}, 1, {{0, 0, 0, 100}, BODY, SCREEN}, sides({Edge::Left}));
        CHECK(!m.active());
        SwarmParams bad;
        bad.speed = std::numeric_limits<double>::quiet_NaN();
        m.begin(Transition::Close, bad, 1, placement(), sides({Edge::Left}));
        CHECK(!m.active() && !m.closed());

        m.begin(Transition::Open, {}, 1, placement(), sides({Edge::Left}));
        run(m, 0.3);
        const Vec2 before = m.agents()[0].position;
        CHECK(m.advance(std::numeric_limits<double>::quiet_NaN()));
        CHECK(m.advance(-1.0));
        CHECK(m.agents()[0].position.x == before.x && m.agents()[0].position.y == before.y);
        // A stalled frame is clamped rather than integrated whole.
        m.advance(30.0);
        for (const SwarmAgent &a : m.agents())
            CHECK(std::isfinite(a.position.x) && std::isfinite(a.position.y));
        m.reset();
        CHECK(!m.active() && m.agents().empty());
    }

    void spriteMeasuresBodyAndLegs() {
        // A disc of radius 20 at (40, 60) with a thin leg reaching right to x = 110, in a 128 px
        // frame.
        constexpr int side = 128;
        std::vector<std::uint8_t> alpha(side * side, 0);
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
                if (std::hypot(x + 0.5 - 40.0, y + 0.5 - 60.0) <= 20.0 ||
                    (y >= 59 && y <= 60 && x >= 40 && x < 110))
                    alpha[y * side + x] = 255;
        const SpriteFrame f = measureSprite(alpha, side);
        CHECK(
            std::abs(f.centre.x * side - 40.0) <= 1.5 && std::abs(f.centre.y * side - 60.0) <= 1.5);
        CHECK(std::abs(f.core * side - 20.0) <= 1.5);
        CHECK(std::abs(f.reach * side - 70.0) <= 2.0);

        const SpriteFrame empty = measureSprite(std::vector<std::uint8_t>(side * side, 0), side);
        CHECK(empty.core == 0.0 && empty.reach == 0.0);
        CHECK(spriteSize(empty, 40.0, 60.0, 0.5) == 0.0);

        // Loose, the whole silhouette fits; locked, the solid core covers the tile; it only grows
        // in between.
        CHECK(std::abs(f.reach * spriteSize(f, 40.0, 60.0, 0.0) - 40.0) < 1e-9);
        CHECK(f.core * spriteSize(f, 40.0, 60.0, 1.0) >= 60.0);
        double previous = 0.0;
        for (double lock = 0.0; lock <= 1.0; lock += 0.1) {
            const double size = spriteSize(f, 40.0, 60.0, lock);
            CHECK(size > previous);
            previous = size;
        }
    }

    void walkCycleFollowsDistance() {
        CHECK(walkFrame(0.0, 80.0, 8) == 0);
        CHECK(walkFrame(45.0, 80.0, 8) == 4);
        CHECK(walkFrame(79.9, 80.0, 8) == 7);
        CHECK(walkFrame(80.0 * 12 + 15.0, 80.0, 8) == 1);
        CHECK(walkFrame(100.0, 0.0, 8) == 0);
        CHECK(walkFrame(100.0, 80.0, 1) == 0);
        // Facing up in the sheet and heading right is a quarter turn clockwise; half-turned is half
        // of it; the long way round is never taken.
        const double up = -std::numbers::pi / 2;
        CHECK(std::abs(spriteRotation(0.0, up, 1.0) - std::numbers::pi / 2) < 1e-12);
        CHECK(std::abs(spriteRotation(0.0, up, 0.5) - std::numbers::pi / 4) < 1e-12);
        CHECK(spriteRotation(up, up, 1.0) == 0.0);
        CHECK(std::abs(spriteRotation(std::numbers::pi * 0.9, -std::numbers::pi * 0.9, 1.0) +
                       std::numbers::pi * 0.2) < 1e-12);
    }

    void movingSpritesAdvanceAtAVisibleRate() {
        Swarm m;
        SwarmParams p;
        p.count = 250;
        m.begin(Transition::Open, p, 7, placement(), sides({Edge::Left}));
        bool observed = false;
        for (int step = 0; step < 60 && m.active(); ++step) {
            std::vector<double> before;
            for (const auto &a : m.agents()) before.push_back(a.walk);
            m.advance(1.0 / 60.0);
            for (std::size_t i = 0; i < before.size(); ++i) {
                const auto &a = m.agents()[i];
                if (a.visible && !a.docked && a.walk > before[i]) {
                    observed = true;
                    // At 60 Hz, no more than a quarter of the shortest creature cycle per frame.
                    CHECK(a.walk - before[i] <= 0.25 * 1.1 * 2.0 * a.fit);
                }
            }
        }
        CHECK(observed);
    }

    void turningSpritesKeepAnimating() {
        Swarm m;
        m.begin(Transition::Open, {}, 12, placement(), sides({Edge::Left}));
        bool observed = false;
        for (int step = 0; step < 1200 && m.active(); ++step) {
            const std::vector<SwarmAgent> before(m.agents().begin(), m.agents().end());
            m.advance(1.0 / 120.0);
            for (std::size_t i = 0; i < before.size(); ++i) {
                const auto &a = m.agents()[i];
                if (before[i].docked && before[i].turn > a.turn) {
                    observed = true;
                    CHECK(a.walk > before[i].walk);
                }
            }
        }
        CHECK(observed);
    }

    void mergingSpritesKeepAnimating() {
        for (bool movement : {false, true}) {
            Swarm m;
            if (movement) {
                m.beginMovement({}, 12, placement());
                for (int i = 0; i < 120; ++i) m.advance(FRAME, true);
            } else
                m.begin(Transition::Open, {}, 12, placement(), sides({Edge::Left}));
            bool observed = false, changedFrame = false;
            for (int step = 0; step < 1200 && m.active(); ++step) {
                const std::vector<SwarmAgent> before(m.agents().begin(), m.agents().end());
                m.advance(FRAME);
                for (std::size_t i = 0; i < before.size(); ++i) {
                    const auto &a = m.agents()[i];
                    if (before[i].docked && before[i].turn == 0.0 && a.lock > before[i].lock &&
                        a.lock < 1.0) {
                        observed = true;
                        CHECK(a.walk > before[i].walk);
                        changedFrame |= walkFrame(a.walk, 2.0 * a.fit, 8) !=
                                        walkFrame(before[i].walk, 2.0 * a.fit, 8);
                    }
                    if (before[i].lock == 1.0 && a.lock == 1.0) CHECK(a.walk == before[i].walk);
                }
            }
            CHECK(observed && changedFrame);
            CHECK(!m.active() && inFormation(m));
        }
    }

    void edgeSidesFollowTheNeighbours() {
        const Box left{0, 0, 960, 1080}, right{960, 0, 960, 1080};
        // Alone: the nearest edge; equidistant edges all count.
        CHECK(resolveEdgeSides({100, 300, 400, 400}, SCREEN, {}) == sides({Edge::Left}));
        CHECK(
            resolveEdgeSides({660, 340, 600, 400}, SCREEN, {}) == sides({Edge::Top, Edge::Bottom}));
        // Beside a neighbour: the edge away from it.
        const Box onLeft[] = {left};
        CHECK(resolveEdgeSides(right, SCREEN, onLeft) == sides({Edge::Right}));
        // Below one neighbour and beside another of equal contact: both open edges.
        const Box around[] = {{840, 540, 540, 540}, {1380, 0, 540, 540}};
        CHECK(resolveEdgeSides({1380, 540, 540, 540}, SCREEN, around) ==
              sides({Edge::Right, Edge::Bottom}));
        // Wedged between two columns: the edges opposite its contacts are taken, so it uses the
        // free ones.
        const Box columns[] = {{0, 0, 640, 1080}, {1280, 0, 640, 1080}};
        CHECK(resolveEdgeSides({640, 0, 640, 1080}, SCREEN, columns) ==
              sides({Edge::Top, Edge::Bottom}));
        CHECK(resolveEdgeSides({0, 0, 0, 0}, SCREEN, {}).empty());
    }

}  // namespace

int main() {
    // Bare names resolve under the sprite directory; explicit paths stay unchanged.
    CHECK(hyprimate::spritePath("cat", "/sprites") == "/sprites/cat.png");
    CHECK(hyprimate::spritePath("bird.png", "/sprites") == "/sprites/bird.png");
    CHECK(hyprimate::spritePath("/tmp/custom.png", "/sprites") == "/tmp/custom.png");
    CHECK(hyprimate::spritePath("./custom.png", "/sprites") == "./custom.png");
    CHECK(hyprimate::spritePath("", "/sprites").empty());
    tilesPartitionTheCapture();
    openingEntersFromItsSidesAndSettlesExactly();
    equalSeedsGiveEqualMotion();
    closeLetsGoInNoOrderAndLeavesThroughItsSide();
    tilesFillInNoOrder();
    surfaceRoundingWaitsForFormation();
    closingRoundsTheSurfaceAsPartsLeave();
    closeTakesOverAnOpening();
    closeInheritsAnInFlightHandoff();
    movementWithoutMotionFinishesImmediately();
    slowMovementKeepsTheWindowWhole();
    workspaceSlidesPreserveIndependentFlight();
    workspaceReversalPreservesScreenPositions();
    fastMovementScattersThenRegroups();
    heldMovementWandersUntilRelease();
    heldMovementRespectsWander();
    departuresAreStaggered();
    routesAreNotStraightLines();
    bodiesInTransitDoNotPassThroughEachOther();
    settlingHurriesTheLanding();
    resizeRecutsTheTiles();
    resizeKeepsAgentsAssignedToNearbyTiles();
    movementLandingsAreRandomlyStaggered();
    invalidInputLeavesTheModelAlone();
    spriteMeasuresBodyAndLegs();
    walkCycleFollowsDistance();
    movingSpritesAdvanceAtAVisibleRate();
    turningSpritesKeepAnimating();
    mergingSpritesKeepAnimating();
    edgeSidesFollowTheNeighbours();
    return g_failures == 0 ? 0 : 1;
}
