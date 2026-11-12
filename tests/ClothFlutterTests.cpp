#include "ClothFlutter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
using namespace liquid_glass;
int failures = 0;
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            ++failures;                                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);                                            \
        }                                                                                                              \
    } while (false)
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
    PaperFacets facets;
    m.facets({WINDOW.w, WINDOW.h}, facets);
    for (const auto &f : facets)
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(f.position[i].x == f.uv[i].x - 0.5 && f.position[i].y == f.uv[i].y - 0.5);
            CHECK(f.shade[i] == 0);
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
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) {
        ClothFlutter m;
        m.begin(Transition::Open, params(), i, WINDOW, {});
        m.reset();
    }
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    ClothPhysics::pauseWorkersForTesting(false);
}
} // namespace
int main() {
    needsPhysics();
    {
        ClothPhysics physics;
        pinnedDrop();
        exits();
        exitCorridor();
        handoff();
        extremeDurationHandoff();
        cadence();
        deterministic();
        invalid();
        starvation();
        cancelAndUnload();
    }
    // Shutdown must join a currently paused worker before unregistering Jolt, even with a model reference retained.
    ClothFlutter survivor;
    {
        ClothPhysics physics;
        ClothPhysics::pauseWorkersForTesting(true);
        survivor.begin(Transition::Open, params(), 1, WINDOW, {});
    }
    survivor.reset();
    if (failures) {
        std::fprintf(stderr, "%d cloth checks failed\n", failures);
        return 1;
    }
    std::puts("cloth flutter tests passed");
}
