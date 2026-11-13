#include "Check.hpp"
#include "ClothFlutter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
using namespace liquid_glass;
constexpr Box WINDOW{100, 50, 1000, 700};
constexpr Box SCREEN{-300, -200, 1920, 1200};
constexpr int GRID = PAPER_CELLS + 1;
ClothParams   params() { return {.duration = 0.8, .flutter = 0.5, .shading = 0.7, .variation = 0.5}; }
struct Pose {
    SheetGrid p{}, v{};
};
Pose pose(const ClothFlutter &m) {
    Pose p;
    m.sampleVertices(p.p, p.v);
    const auto finite = [](const Vec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    CHECK(std::all_of(p.p.begin(), p.p.end(), finite));
    CHECK(std::all_of(p.v.begin(), p.v.end(), finite));
    return p;
}
double delta(const SheetGrid &a, const SheetGrid &b) {
    double d = 0;
    for (std::size_t k = 0; k < a.size(); ++k)
        d = std::max({d, std::abs(a[k].x - b[k].x), std::abs(a[k].y - b[k].y), std::abs(a[k].z - b[k].z)});
    return d;
}
void advance(ClothFlutter &m, double seconds, double step = 1.0 / 144) {
    while (seconds > 1e-12 && m.active()) {
        const double dt = std::min(step, seconds);
        m.advance(dt);
        seconds -= dt;
    }
}
ClothFlutter open(ClothParams p = params(), std::uint64_t seed = 7) {
    ClothFlutter m;
    m.begin(Transition::Open, p, seed, WINDOW, {});
    CHECK(m.waitForRecording());
    return m;
}
ClothFlutter close(Edge edge, ClothParams p = params()) {
    ClothFlutter m;
    m.begin(Transition::Close, p, 3, WINDOW, {}, ClothCloseGeometry{edge, SCREEN});
    CHECK(m.waitForRecording());
    return m;
}
void needsPhysics() {
    ClothFlutter m;
    m.begin(Transition::Open, params(), 1, WINDOW, {});
    CHECK(!m.active());
}
void pinnedDrop() {
    auto p = params();
    p.flutter = 0;
    auto       m = open(p);
    const auto start = pose(m);
    CHECK(m.opacity() == 0);
    const auto bottom = std::size_t(PAPER_CELLS) * GRID + PAPER_CELLS / 2;
    CHECK(std::abs(start.p[bottom].y + std::cos(80 * std::acos(-1) / 180)) < 1e-6);
    CHECK(std::abs(start.p[bottom].z - std::sin(80 * std::acos(-1) / 180)) < 1e-6);
    advance(m, 0.25);
    const auto dropped = pose(m);
    CHECK(dropped.p[bottom].y < start.p[bottom].y - 0.05);
    CHECK(dropped.v[bottom].y < 0);
    for (int i = 0; i < GRID; ++i) {
        CHECK(std::abs(dropped.p[i].x - (double(i) / PAPER_CELLS - 0.5)) < 1e-7);
        CHECK(std::abs(dropped.p[i].y) < 1e-9 && std::abs(dropped.p[i].z) < 1e-9);
        CHECK(std::abs(dropped.v[i].x) + std::abs(dropped.v[i].y) + std::abs(dropped.v[i].z) < 1e-7);
    }
    advance(m, 0.6);
    CHECK(!m.active() && !m.closed() && m.opacity() == 1);
    liquid_glass::SheetFrame frame;
    m.frame({WINDOW.w, WINDOW.h}, frame);
    for (std::size_t v = 0; v < liquid_glass::PAPER_GRID_VERTICES; ++v) {
        const auto uv = liquid_glass::sheetUv(v);
        CHECK(frame.position[v].x == uv.x - 0.5 && frame.position[v].y == uv.y - 0.5);
    }
    for (const auto &shade : frame.shade)
        for (const float s : shade)
            CHECK(s == 0.0F);
}
// The swinging free edge must be air-braked before it reaches the desktop plane; stopping dead there jolts the
// displayed sheet within a single frame.
void softLanding() {
    const double L = std::sqrt(WINDOW.w * WINDOW.h);
    for (double flutter : {0.0, 0.5}) {
        auto p = params();
        p.flutter = flutter;
        auto                   m = open(p);
        std::vector<SheetGrid> frames;
        while (m.active()) {
            m.advance(1.0 / 120);
            frames.push_back(pose(m).p);
        }
        double worst = 0;
        for (std::size_t f = 2; f < frames.size(); ++f)
            for (std::size_t k = 0; k < frames[f].size(); ++k) {
                const Vec3 a = frames[f - 2][k], b = frames[f - 1][k], c = frames[f][k];
                worst = std::max(worst, std::hypot((c.x - 2 * b.x + a.x) * WINDOW.w, (c.y - 2 * b.y + a.y) * WINDOW.h,
                                                   (c.z - 2 * b.z + a.z) * L));
            }
        // Per-frame velocity change in pixels at 120 Hz; the undamped swing peaked near 60 here.
        CHECK(worst < 30);
    }
}
void exits() {
    auto p = params();
    p.flutter = 0;
    auto       falling = close(Edge::Bottom, p);
    const auto start = pose(falling);
    advance(falling, 0.25);
    const auto drop = pose(falling);
    double     averageYVelocity = 0;
    for (int i = 0; i < GRID; ++i) {
        CHECK(drop.p[i].y < start.p[i].y - 0.1);
        averageYVelocity += drop.v[i].y;
    }
    CHECK(averageYVelocity < 0);
    CHECK(falling.opacity() == 1); // visible sheet cannot fade prematurely
    advance(falling, 0.45);
    const auto late = pose(falling);
    for (const auto &v : late.p)
        CHECK(WINDOW.y + WINDOW.h / 2 + (-v.y * WINDOW.h - WINDOW.h / 2) > SCREEN.y + SCREEN.h);
    advance(falling, 1);
    CHECK(falling.closed() && falling.opacity() == 0);
    for (const Edge edge : {Edge::Top, Edge::Left, Edge::Right}) {
        auto       pulling = close(edge, p);
        const auto initial = pose(pulling);
        advance(pulling, 0.4);
        const auto mid = pose(pulling);
        for (int i = 0; i < GRID; ++i) {
            const std::size_t k =
                edge == Edge::Top ? i : std::size_t(i) * GRID + (edge == Edge::Left ? 0 : PAPER_CELLS);
            if (edge == Edge::Top)
                CHECK(mid.p[k].y > initial.p[k].y + 0.5);
            if (edge == Edge::Left)
                CHECK(mid.p[k].x < initial.p[k].x - 0.5);
            if (edge == Edge::Right)
                CHECK(mid.p[k].x > initial.p[k].x + 0.5);
        }
        advance(pulling, 0.35);
        const auto end = pose(pulling);
        for (const auto &v : end.p) {
            if (edge == Edge::Top)
                CHECK(WINDOW.y - v.y * WINDOW.h < SCREEN.y);
            if (edge == Edge::Left)
                CHECK(WINDOW.x + WINDOW.w / 2 + v.x * WINDOW.w < SCREEN.x);
            if (edge == Edge::Right)
                CHECK(WINDOW.x + WINDOW.w / 2 + v.x * WINDOW.w > SCREEN.x + SCREEN.w);
        }
        advance(pulling, 1);
        CHECK(pulling.closed());
    }
}
void exitCorridor() {
    const Box window{100, 50, 1000, 400}, screen{0, 0, 1920, 1080};
    const Box below{100, 460, 1000, 500}, right{1110, 50, 600, 400};
    auto      p = params();
    p.flutter = p.variation = 0;
    p.duration = .9;
    for (const auto edge : {Edge::Bottom, Edge::Right}) {
        const Box    obstacle = edge == Edge::Bottom ? below : right;
        ClothFlutter clear, blocked;
        clear.begin(Transition::Close, p, 1, window, {}, ClothCloseGeometry{edge, screen});
        blocked.begin(Transition::Close, p, 1, window, std::span<const Box>(&obstacle, 1),
                      ClothCloseGeometry{edge, screen});
        CHECK(clear.waitForRecording());
        CHECK(blocked.waitForRecording());
        advance(clear, .7);
        advance(blocked, .7);
        CHECK(delta(pose(clear).p, pose(blocked).p) == 0);
        const auto final = pose(blocked);
        for (const auto &v : final.p) {
            CHECK(std::abs(v.x) < 20 && std::abs(v.y) < 20 && std::abs(v.z) < 20);
            if (edge == Edge::Bottom)
                CHECK(window.y - v.y * window.h > screen.y + screen.h);
            else
                CHECK(window.x + window.w / 2 + v.x * window.w > screen.x + screen.w);
        }
    }
}
void handoff() {
    for (double when : {0.015, 0.25, 0.73}) {
        auto m = open();
        advance(m, when);
        const auto   before = pose(m);
        const double alpha = m.opacity();
        auto         p = params();
        p.duration = 1.6;
        m.begin(Transition::Close, p, 12, WINDOW, {}, ClothCloseGeometry{Edge::Left, SCREEN});
        const auto after = pose(m);
        CHECK(delta(before.p, after.p) < 1e-12);
        CHECK(delta(before.v, after.v) < 1e-12);
        CHECK(m.opacity() == alpha);
        CHECK(m.waitForRecording());
        const auto ready = pose(m);
        CHECK(delta(before.p, ready.p) < 2e-6);
        CHECK(delta(before.v, ready.v) < 2e-5);
    }
}
void extremeDurationHandoff() {
    auto p = params();
    p.duration = .1;
    p.flutter = 1;
    p.variation = 0;
    auto m = open(p);
    advance(m, .03, .001);
    const auto   before = pose(m);
    const double alpha = m.opacity();
    p.duration = 3.430429;
    m.begin(Transition::Close, p, 2, WINDOW, {}, ClothCloseGeometry{Edge::Left, SCREEN});
    CHECK(m.waitForRecording());
    const auto after = pose(m);
    CHECK(delta(before.p, after.p) < 2e-6);
    CHECK(delta(before.v, after.v) < 2e-4);
    CHECK(m.opacity() == alpha);
}
void cadence() {
    const auto source = open();
    auto       reference = source;
    advance(reference, 0.63, 0.001);
    const auto expected = pose(reference);
    for (double hz : {60., 120., 144., 240.}) {
        auto m = source;
        advance(m, 0.63, 1 / hz);
        CHECK(delta(expected.p, pose(m).p) < 1e-10);
    }
    auto         irregular = source;
    double       time = 0;
    const double steps[] = {.003, .027, .011, .007, .045, .016};
    int          i = 0;
    while (time < .63 - 1e-12) {
        const double dt = std::min(steps[i++ % 6], .63 - time);
        irregular.advance(dt);
        time += dt;
    }
    CHECK(delta(expected.p, pose(irregular).p) < 1e-10);
}
void deterministic() {
    auto p = params();
    p.variation = 0;
    auto a = open(p, 5), b = open(p, 900);
    advance(a, .43);
    advance(b, .43);
    CHECK(delta(pose(a).p, pose(b).p) == 0);
}
void invalid() {
    auto p = params();
    p.duration = std::numeric_limits<double>::quiet_NaN();
    ClothFlutter m;
    m.begin(Transition::Open, p, 1, WINDOW, {});
    CHECK(!m.active());
    m.begin(Transition::Open, params(), 1, Box{0, 0, 0, 500}, {});
    CHECK(!m.active());
    m.begin(Transition::Open, params(), 1, Box{0, 0, std::numeric_limits<double>::max(), 500}, {});
    CHECK(!m.active());
    m.begin(static_cast<Transition>(99), params(), 1, WINDOW, {});
    CHECK(!m.active());
    m.begin(Transition::Close, params(), 1, WINDOW, {}, ClothCloseGeometry{Edge::Left, Box{0, 0, -1, 500}});
    CHECK(!m.active());
    m = open();
    m.advance(-1);
    CHECK(!m.active() && !m.closed());
    m = close(Edge::Top);
    m.advance(std::numeric_limits<double>::infinity());
    CHECK(m.closed());
}
void starvation() {
    ClothPhysics::pauseWorkersForTesting(true);
    ClothFlutter opening;
    opening.begin(Transition::Open, params(), 1, WINDOW, {});
    advance(opening, 0.4);
    CHECK(!opening.active() && opening.opacity() == 1);
    ClothFlutter closing;
    closing.begin(Transition::Close, params(), 1, WINDOW, {}, ClothCloseGeometry{Edge::Bottom, SCREEN});
    advance(closing, 0.4);
    CHECK(closing.closed() && closing.opacity() == 0);
    ClothPhysics::pauseWorkersForTesting(false);
    // Exhaust a partial recording by pausing publication after the worker has started.
    ClothPhysics::pauseWorkersForTesting(true, 2);
    ClothFlutter partial;
    partial.begin(Transition::Open, params(), 7, WINDOW, {});
    CHECK(partial.waitForSnapshots(2));
    advance(partial, 1);
    CHECK(!partial.active());
    ClothPhysics::pauseWorkersForTesting(false);
}
void cancelAndUnload() {
    ClothPhysics::pauseWorkersForTesting(true);
    for (int i = 0; i < 50; ++i) {
        ClothFlutter m;
        m.begin(Transition::Open, params(), i, WINDOW, {});
        m.reset();
    }
    ClothPhysics::pauseWorkersForTesting(false);
}

void movementStep(ClothFlutter &m, const Box &box, double dt, double &time) {
    m.updateMovement(box, dt);
    m.advance(dt);
    time += dt;
    if (m.moving()) {
        CHECK(m.waitForSnapshots(2 + int(std::floor(time * 120 + 1e-7))));
        m.advance(0);
    }
}

void movement() {
    ClothMovementParams params;
    params.flutter = 0;
    params.variation = 0;
    ClothFlutter m;
    m.beginMovement(params, 1, WINDOW, {.5, .5});
    CHECK(m.moving() && m.waitForSnapshots(2));
    Box    box = WINDOW;
    double time = 0;
    for (int i = 0; i < 24; ++i) {
        box.x += 4;
        movementStep(m, box, 1.0 / 120, time);
    }
    auto moved = pose(m);
    const auto held = moved.p[std::size_t(PAPER_CELLS / 2) * GRID + PAPER_CELLS / 2];
    CHECK(std::abs(held.x) < 1e-5 && std::abs(held.y + .5) < 1e-5 && std::abs(held.z) < 1e-5);
    double displacement = 0;
    for (std::size_t k = 0; k < moved.p.size(); ++k)
        displacement = std::max(displacement, std::abs(moved.p[k].x - (double(k % GRID) / PAPER_CELLS - .5)));
    CHECK(displacement > .001);
    for (int i = 0; i < 12; ++i) {
        box.x -= 4;
        movementStep(m, box, 1.0 / 120, time);
    }
    moved = pose(m);
    // A retained copy freezes the displayed pose; closing consumes it without sharing mutable playback state.
    ClothFlutter closing = m;
    const double alpha = m.opacity();
    closing.begin(Transition::Close, ::params(), 4, box, {}, ClothCloseGeometry{Edge::Right, SCREEN});
    CHECK(closing.waitForSnapshots(2));
    CHECK(delta(pose(closing).p, moved.p) < 1e-6);
    CHECK(delta(pose(closing).v, moved.v) < 1e-5);
    CHECK(std::abs(closing.opacity() - alpha) < 1e-8);
    box.w *= .9;
    box.h *= 1.1;
    movementStep(m, box, 1.0 / 120, time);
    for (int i = 0; i < 170 && m.moving(); ++i) // up to the hand-over to the exact flat window
        movementStep(m, box, 1.0 / 120, time);
    advance(m, .4);
    CHECK(!m.active() && m.opacity() == 1);
    liquid_glass::SheetFrame frame;
    m.frame({box.w, box.h}, frame);
    for (std::size_t v = 0; v < liquid_glass::PAPER_GRID_VERTICES; ++v) {
        const auto uv = liquid_glass::sheetUv(v);
        CHECK(frame.position[v].x == uv.x - .5 && frame.position[v].y == uv.y - .5);
    }
}

// Measured as displayed: offsets in units of the window, edges in pixels against their flat length.
struct MovementMetrics {
    double offset = 0, maxEdge = 0, front = 0, back = 0, trailX = 0, trailY = 0;
};
MovementMetrics movementMetrics(const SheetGrid &p, const Box &window) {
    MovementMetrics m;
    const double    L = std::sqrt(window.w * window.h);
    for (int j = 0; j < GRID; ++j)
        for (int i = 0; i < GRID; ++i) {
            const auto a = p[std::size_t(j) * GRID + i];
            const Vec3 rest{double(i) / PAPER_CELLS - .5, -double(j) / PAPER_CELLS, 0};
            m.offset = std::max(m.offset, std::hypot(std::hypot(a.x - rest.x, a.y - rest.y), a.z));
            m.front = std::max(m.front, a.z);
            m.back = std::min(m.back, a.z);
            m.trailX += (a.x - rest.x) / double(GRID * GRID);
            m.trailY += (a.y - rest.y) / double(GRID * GRID);
            for (const auto &[x, y] : {std::pair{i + 1, j}, std::pair{i, j + 1}}) {
                if (x >= GRID || y >= GRID)
                    continue;
                const auto   b = p[std::size_t(y) * GRID + x];
                const double length =
                    std::hypot(std::hypot((a.x - b.x) * window.w, (a.y - b.y) * window.h), (a.z - b.z) * L);
                m.maxEdge = std::max(m.maxEdge, length * PAPER_CELLS / (x > i ? window.w : window.h));
            }
        }
    return m;
}
// Drags the window by a point near its title bar, then lets go. The sheet is free cloth: it streams behind the
// pinch, folds to both sides of the screen plane, never stretches, and is the exact flat window again at rest.
void movementFabric() {
    for (const Box window : {Box{0, 0, 1000, 700}, Box{0, 0, 600, 400}})
        for (const int scenario : {0, 1, 2, 3, 4, 5}) {
            ClothMovementParams params;
            params.flutter = scenario == 5 ? 0 : .5;
            params.variation = 0;
            ClothFlutter m;
            m.beginMovement(params, 5, window, {.5, .1});
            CHECK(m.waitForSnapshots(2));
            Box             box = window;
            double          time = 0;
            MovementMetrics peak, last;
            double          left = 0, right = 0;
            const double    dt = 1.0 / 60;
            const int       frames = scenario == 2 ? 180 : 90;
            for (int n = 0; n < frames; ++n) {
                const double t = (n + 1) * dt;
                if (scenario == 2) {
                    box.x = window.x + 400 * std::sin(2 * std::numbers::pi * 2 * t);
                    box.y = window.y + 300 * std::sin(2 * std::numbers::pi * 1.5 * t);
                } else if (scenario == 3 || scenario == 4) {
                    box.y += (scenario == 3 ? 1800 : -1800) * dt;
                } else {
                    box.x += (scenario == 1 ? 350 : 2000) * dt;
                }
                movementStep(m, box, dt, time);
                last = movementMetrics(pose(m).p, box);
                peak.offset = std::max(peak.offset, last.offset);
                peak.maxEdge = std::max(peak.maxEdge, last.maxEdge);
                peak.front = std::max(peak.front, last.front);
                peak.back = std::min(peak.back, last.back);
                left = std::min(left, last.trailX);
                right = std::max(right, last.trailX);
            }
            for (int n = 0; n < 84; ++n) // just short of the hand-over to the exact flat window
                movementStep(m, box, dt, time);
            m.updateMovement(box, dt);
            m.advance(dt);
            const double quiet = movementMetrics(pose(m).p, box).offset;
            std::printf("movement %.0fx%.0f scenario %d trail %.3f,%.3f swing %.3f..%.3f offset %.3f z %.3f..%.3f "
                        "edge %.3f quiet %.3f\n",
                        window.w, window.h, scenario, last.trailX, last.trailY, left, right, peak.offset, peak.back,
                        peak.front, peak.maxEdge, quiet);
            CHECK(quiet < .02 && peak.maxEdge < 1.3 && peak.offset < 2.6);
            if (scenario == 0 || scenario == 5)
                CHECK(last.trailX < -.25 && peak.front > .05 && peak.back < -.05);
            if (scenario == 1)
                CHECK(last.trailX < -.005 && last.trailX > -.12 && peak.offset < .25);
            if (scenario == 2)
                CHECK(left < -.15 && right > .15);
            if (scenario == 3)
                CHECK(last.trailY > .02 && peak.front > .1); // lifted and billowing, if not yet flipped over
            if (scenario == 4)
                CHECK(last.trailY < -.05);
        }
}

// The harder the pull, the further the sheet trails. Once the window stops the sheet takes about a second to come
// flat, drawn taut from the pinch outwards rather than sprung back.
void movementResponse() {
    for (const Box window : {Box{0, 0, 1000, 700}, Box{0, 0, 600, 400}}) {
        double slower = 0;
        for (const double speed : {600.0, 2000.0}) {
            ClothFlutter m;
            m.beginMovement({}, 5, window, {.5, .1});
            CHECK(m.waitForSnapshots(2));
            Box    box = window;
            double time = 0;
            for (int i = 1; i <= 90; ++i) {
                box.x += speed / 120;
                movementStep(m, box, 1.0 / 120, time);
            }
            const double trail = -movementMetrics(pose(m).p, box).trailX;
            CHECK(trail > slower + .01);
            slower = trail;
            // It goes taut from the pinch outwards: cloth near the grab is flat while the far corners still hang.
            const auto strayed = [&](double v) {
                const int  row = int(v * PAPER_CELLS);
                const auto p = pose(m).p[std::size_t(row) * GRID + PAPER_CELLS / 2];
                return std::hypot(std::hypot(p.x, p.y + double(row) / PAPER_CELLS), p.z);
            };
            double early = 0, nearMid = 0, farMid = 0;
            for (int i = 1; i <= 170; ++i) {
                movementStep(m, box, 1.0 / 120, time);
                if (i == 36)
                    early = movementMetrics(pose(m).p, box).offset;
                if (i == 96) {
                    nearMid = strayed(.3);
                    farMid = strayed(1);
                }
            }
            CHECK(early > .3 * trail && farMid > .01 && nearMid < .3 * farMid);
            CHECK(movementMetrics(pose(m).p, box).offset < .022);
        }
    }
}

void movementHandoff() {
    auto m = open();
    advance(m, .3);
    const auto   before = pose(m);
    const double alpha = m.opacity();
    m.beginMovement({}, 2, WINDOW, {1, 0});
    CHECK(delta(pose(m).p, before.p) < 1e-6);
    CHECK(delta(pose(m).v, before.v) < 1e-6);
    CHECK(std::abs(m.opacity() - alpha) < 1e-8);
    CHECK(m.waitForSnapshots(2));
    Box box = WINDOW;
    box.x += 10;
    double time = 0;
    movementStep(m, box, 1.0 / 120, time);
    CHECK(m.moving());
    // Bounded starvation flattens, and a new move during that flattening continues from its visible pose.
    ClothPhysics::pauseWorkersForTesting(true, 2);
    advance(m, .12);
    const auto fallback = pose(m);
    box.y += 20;
    m.updateMovement(box, .01);
    CHECK(m.moving());
    CHECK(delta(pose(m).p, fallback.p) < 1e-6);
    ClothPhysics::pauseWorkersForTesting(false);
    m.reset();
}

Pose movementTrajectory(double cadence, std::uint64_t seed, bool irregular = false) {
    ClothMovementParams params;
    params.variation = 0;
    ClothFlutter m;
    m.beginMovement(params, seed, WINDOW, {1, 0});
    CHECK(m.waitForSnapshots(2));
    double time = 0;
    int    frame = 0;
    while (time < .2 - 1e-9) {
        const double dt = std::min(cadence * (irregular ? (frame++ % 2 ? 1.6 : .4) : 1), .2 - time);
        Box          box = WINDOW;
        box.x += 300 * (time + dt);
        movementStep(m, box, dt, time);
    }
    return pose(m);
}

void movementCadenceAndInvalid() {
    const auto reference = movementTrajectory(1.0 / 120, 7);
    for (double hz : {60., 144., 240.}) {
        const auto sampled = movementTrajectory(1 / hz, 100);
        CHECK(delta(reference.p, sampled.p) < .015);
    }
    const auto same = movementTrajectory(1.0 / 120, 900);
    CHECK(delta(reference.p, same.p) == 0);
    CHECK(delta(reference.p, movementTrajectory(1.0 / 120, 100, true).p) < .015);
    ClothFlutter m;
    auto         params = ClothMovementParams{};
    params.strength = 0;
    m.beginMovement(params, 1, WINDOW, {.5, .5});
    CHECK(!m.active());
    params.strength = std::numeric_limits<double>::quiet_NaN();
    m.beginMovement(params, 1, WINDOW, {.5, .5});
    CHECK(!m.active());
    m.beginMovement({}, 1, WINDOW, {1, 0});
    m.updateMovement(Box{0, 0, -1, 200}, .01);
    CHECK(!m.active());
    ClothPhysics::pauseWorkersForTesting(true, 2);
    m.beginMovement({}, 1, WINDOW, {.5, .5});
    CHECK(m.waitForSnapshots(2));
    advance(m, .4);
    CHECK(!m.active());
    ClothPhysics::pauseWorkersForTesting(false);
    // Exhaust the bounded input queue while the worker is paused, without blocking the producer.
    ClothPhysics::pauseWorkersForTesting(true, 2);
    m.beginMovement({}, 1, WINDOW, {1, 0});
    CHECK(m.waitForSnapshots(2));
    for (int i = 0; i < 70; ++i)
        m.advance(1e-6);
    advance(m, .25);
    CHECK(!m.active());
    ClothPhysics::pauseWorkersForTesting(false);
    for (int i = 0; i < 20; ++i) {
        m.beginMovement({}, i, WINDOW, {.5, .5});
        m.reset();
    }
}
// Each frame posts a target the worker has not answered yet. The displayed sheet must already be fully simulated, so
// it cannot depend on whether the worker happened to answer before the frame was drawn.
void movementIndependentOfWorkerTiming() {
    ClothMovementParams params;
    params.variation = 0;
    constexpr int          FRAMES = 30;
    constexpr double       FRAME = 1.0 / 60;
    std::vector<SheetGrid> answered;
    {
        ClothFlutter m;
        m.beginMovement(params, 3, WINDOW, {1, 0});
        CHECK(m.waitForSnapshots(2));
        Box box = WINDOW;
        for (int k = 1; k <= FRAMES; ++k) {
            box.x += 5;
            m.updateMovement(box, FRAME);
            m.advance(FRAME);
            CHECK(m.waitForSnapshots(2 + 2 * k));
            m.advance(0);
            answered.push_back(pose(m).p);
        }
    }
    ClothFlutter m;
    m.beginMovement(params, 3, WINDOW, {1, 0});
    CHECK(m.waitForSnapshots(2));
    Box    box = WINDOW;
    double worst = 0;
    for (int k = 1; k <= FRAMES; ++k) {
        // Let the worker answer every earlier target, then hold it before the one this frame posts.
        ClothPhysics::pauseWorkersForTesting(true, 2 + 2 * (k - 1));
        CHECK(m.waitForSnapshots(2 + 2 * (k - 1)));
        box.x += 5;
        m.updateMovement(box, FRAME);
        m.advance(FRAME);
        worst = std::max(worst, delta(answered[std::size_t(k - 1)], pose(m).p));
    }
    ClothPhysics::pauseWorkersForTesting(false);
    CHECK(worst < 1e-9);
}

void movementBoundsAndRestart() {
    ClothFlutter m;
    m.beginMovement({}, 1, WINDOW, {1, 0});
    CHECK(m.waitForSnapshots(2));
    double time = 0;
    Box    box = WINDOW;
    box.x += 300;
    movementStep(m, box, 1.0 / 120, time);
    movementStep(m, box, 1.0 / 120, time);
    const auto moved = pose(m);
    const auto anchor = std::size_t(PAPER_CELLS);
    CHECK(std::abs(moved.p[anchor].x - .5) < 1e-5 && std::abs(moved.p[anchor].y) < 1e-5);
    for (std::size_t k = 0; k < moved.p.size(); ++k) {
        const Vec3   r{double(k % GRID) / PAPER_CELLS - .5, -double(k / GRID) / PAPER_CELLS, 0};
        const auto   p = moved.p[k];
        const double d = std::sqrt(std::pow((p.x - r.x) * box.w, 2) + std::pow((p.y - r.y) * box.h, 2) +
                                   std::pow(p.z * std::sqrt(box.w * box.h), 2));
        CHECK(std::isfinite(d) && d <= 10 * std::max(box.w, box.h));
    }
    for (int i = 0; i < 168; ++i)
        movementStep(m, box, 1.0 / 120, time);
    m.advance(.12); // begin the bounded final flattening transition
    const auto settling = pose(m);
    box.y += 10;
    m.updateMovement(box, .01);
    CHECK(m.moving());
    CHECK(delta(pose(m).p, settling.p) < 1e-6);
    CHECK(delta(pose(m).v, settling.v) < 1e-6);
}

// A sheet that has paused is pinched afresh when the next pull comes from somewhere else, and only then.
void movementRegrab() {
    ClothFlutter m;
    m.beginMovement({}, 1, WINDOW, {.5, .1});
    CHECK(m.waitForSnapshots(2));
    double time = 0;
    Box    box = WINDOW;
    for (int i = 0; i < 12; ++i) {
        box.x += 10;
        movementStep(m, box, 1.0 / 120, time);
    }
    CHECK(!m.regrabs({.9, .9})); // still being pulled
    for (int i = 0; i < 30; ++i)
        movementStep(m, box, 1.0 / 120, time);
    CHECK(m.regrabs({.9, .9}) && !m.regrabs({.52, .1}));
    const auto before = pose(m);
    m.beginMovement({}, 2, box, {.9, .9});
    CHECK(m.moving() && !m.regrabs({.9, .9}));
    CHECK(delta(pose(m).p, before.p) < 1e-6);
}

void clothShine() {
    SheetGrid grid{};
    for (int j = 0; j < GRID; ++j)
        for (int i = 0; i < GRID; ++i) {
            const double x = double(i) / PAPER_CELLS;
            const double y = double(j) / PAPER_CELLS;
            grid[j * GRID + i] = {(x - .5) * WINDOW.w, (y - .5) * WINDOW.h, 40 * std::sin(x * 8)};
        }
    SheetFrame dark, bright, legacy;
    projectSheet(grid, {WINDOW.w, WINDOW.h}, .7, SheetLighting::ClothSmooth, dark, 0);
    projectSheet(grid, {WINDOW.w, WINDOW.h}, .7, SheetLighting::ClothSmooth, bright, 1);
    projectSheet(grid, {WINDOW.w, WINDOW.h}, .7, SheetLighting::ClothSmooth, legacy);
    bool shadow = false, highlight = false;
    for (std::size_t t = 0; t < PAPER_TRIANGLES; ++t)
        for (int c = 0; c < 3; ++c) {
            CHECK(dark.shade[t][c] <= 0);
            CHECK(bright.shade[t][c] == legacy.shade[t][c]);
            if (bright.shade[t][c] < 0) {
                shadow = true;
                CHECK(dark.shade[t][c] == bright.shade[t][c]);
            }
            if (bright.shade[t][c] > 0)
                highlight = true;
        }
    CHECK(shadow && highlight);
    ClothFlutter m;
    auto         p = params();
    p.shine = std::numeric_limits<double>::quiet_NaN();
    m.begin(Transition::Open, p, 1, WINDOW, {});
    CHECK(!m.active());
    ClothMovementParams movement;
    movement.shine = std::numeric_limits<double>::infinity();
    m.beginMovement(movement, 1, WINDOW, {});
    CHECK(!m.active());
}

void movementGrabBeforePublication() {
    ClothFlutter m;
    m.beginMovement({}, 1, WINDOW, {1, 0});
    CHECK(m.waitForSnapshots(2));
    ClothPhysics::pauseWorkersForTesting(true, 2);
    Box box = WINDOW;
    box.x += 20;
    m.updateMovement(box, 1.0 / 60);
    m.advance(1.0 / 60);
    const auto onset = pose(m);
    CHECK(std::abs(onset.p[PAPER_CELLS].x - .5) < 1e-7);
    CHECK(std::abs(onset.v[PAPER_CELLS].x - 1.2) < 1e-6);
    ClothPhysics::pauseWorkersForTesting(false);
    m.reset();

    m.beginMovement({}, 1, WINDOW, {1, 0});
    CHECK(m.waitForSnapshots(2));
    box = WINDOW;
    double time = 0;
    for (int i = 0; i < 4; ++i) {
        box.x += 10;
        movementStep(m, box, 1.0 / 120, time);
    }
    ClothPhysics::pauseWorkersForTesting(true, 6);
    m.updateMovement(box, 1.0 / 60);
    m.advance(1.0 / 60);
    const auto stopped = pose(m);
    CHECK(std::abs(stopped.p[PAPER_CELLS].x - .5) < 1e-6);
    CHECK(std::abs(stopped.v[PAPER_CELLS].x) < 1e-5);
    ClothPhysics::pauseWorkersForTesting(false);
}
} // namespace
int main() {
    needsPhysics();
    {
        ClothPhysics physics;
        pinnedDrop();
        softLanding();
        exits();
        exitCorridor();
        handoff();
        extremeDurationHandoff();
        cadence();
        deterministic();
        invalid();
        starvation();
        cancelAndUnload();
        movement();
        movementFabric();
        movementResponse();
        movementHandoff();
        movementCadenceAndInvalid();
        movementIndependentOfWorkerTiming();
        movementBoundsAndRestart();
        movementGrabBeforePublication();
        movementRegrab();
        clothShine();
    }
    // Shutdown must join a currently paused worker before unregistering Jolt, even with a model reference retained.
    ClothFlutter survivor;
    {
        ClothPhysics physics;
        ClothPhysics::pauseWorkersForTesting(true);
        survivor.begin(Transition::Open, params(), 1, WINDOW, {});
    }
    survivor.reset();
    if (g_failures) {
        std::fprintf(stderr, "%d cloth checks failed\n", g_failures);
        return 1;
    }
    std::puts("cloth flutter tests passed");
}
