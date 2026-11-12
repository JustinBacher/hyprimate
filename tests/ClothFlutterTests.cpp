#include "ClothFlutter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

namespace {

using liquid_glass::Box;
using liquid_glass::ClothFlutter;
using liquid_glass::ClothParams;
using liquid_glass::ClothPhysics;
using liquid_glass::PaperFacets;
using liquid_glass::Transition;
using liquid_glass::Vec2;

int g_failures = 0;

void report(bool ok, const char *expression, const char *file, int line) {
    if (ok)
        return;
    ++g_failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}

#define CHECK(expr) report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

constexpr Vec2 SIZE{1000.0, 700.0};
constexpr Box  WINDOW{100.0, 50.0, SIZE.x, SIZE.y};

ClothParams params() { return {.duration = 0.8, .flutter = 0.5, .shading = 0.7, .variation = 0.5}; }

// Plays the model in 10 ms steps, pacing on the wall clock so the worker can record ahead. Stops when the model
// is no longer active, after `seconds` of animation, or after a generous wall-clock timeout.
void play(ClothFlutter &model, double seconds = std::numeric_limits<double>::infinity()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (double t = 0.0; t + 1e-12 < seconds && model.active() && std::chrono::steady_clock::now() < deadline;) {
        const double step = std::min(0.01, seconds - t);
        model.advance(step);
        t += step;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

PaperFacets facetsOf(const ClothFlutter &model) {
    PaperFacets out{};
    model.facets(SIZE, out);
    return out;
}

bool isIdentity(const PaperFacets &facets, double tolerance) {
    for (const auto &facet : facets)
        for (std::size_t c = 0; c < 3; ++c)
            if (std::abs(facet.position[c].x - (facet.uv[c].x - 0.5)) > tolerance ||
                std::abs(facet.position[c].y - (facet.uv[c].y - 0.5)) > tolerance || facet.shade[c] != 0.0)
                return false;
    return true;
}

// Lowest displayed point, in units of the rest height from the rest centre (0.5 is the window's bottom edge).
double lowest(const PaperFacets &facets) {
    double best = -std::numeric_limits<double>::infinity();
    for (const auto &facet : facets)
        for (const Vec2 &p : facet.position)
            best = std::max(best, p.y);
    return best;
}

double maxPositionDelta(const PaperFacets &a, const PaperFacets &b) {
    double best = 0.0;
    for (std::size_t t = 0; t < a.size(); ++t)
        for (std::size_t c = 0; c < 3; ++c) {
            best = std::max(best, std::abs(a[t].position[c].x - b[t].position[c].x));
            best = std::max(best, std::abs(a[t].position[c].y - b[t].position[c].y));
        }
    return best;
}

// Without live physics there is nothing to simulate with: the cloth stays idle and the window draws flat.
void testNeedsPhysics() {
    ClothFlutter model;
    model.begin(Transition::Open, params(), 1, WINDOW, {});
    CHECK(!model.active());
    CHECK(isIdentity(facetsOf(model), 0.0));
}

// The open starts wound on a roll at the top edge, faded out, and ends exactly on the flat window.
void testOpenUnrollsToRest() {
    ClothFlutter model;
    model.begin(Transition::Open, params(), 7, WINDOW, {});
    CHECK(model.active());
    CHECK(model.opacity() < 0.05);
    CHECK(lowest(facetsOf(model)) < -0.25);
    play(model, 0.3);
    CHECK(model.active());
    CHECK(lowest(facetsOf(model)) > -0.25); // the guided roll has begun to descend
    play(model);
    CHECK(!model.active());
    CHECK(!model.closed());
    CHECK(model.opacity() == 1.0);
    CHECK(isIdentity(facetsOf(model), 0.0));
}

// The close winds the cloth up to the top edge and fades it out completely.
void testCloseWindsUpAndFades() {
    ClothFlutter model;
    model.begin(Transition::Close, params(), 3, WINDOW, {});
    CHECK(model.active());
    CHECK(model.opacity() == 1.0);
    CHECK(maxPositionDelta(facetsOf(model), facetsOf(ClothFlutter{})) < 0.01); // starts flat
    play(model, 0.75);
    CHECK(model.active());
    CHECK(lowest(facetsOf(model)) < -0.2); // wound up near the top
    play(model);
    CHECK(model.closed());
    CHECK(model.opacity() == 0.0);
}

// A close begun mid-open picks up the cloth where it is instead of jumping to the flat window.
void testCloseContinuesOpening() {
    ClothFlutter model;
    model.begin(Transition::Open, params(), 11, WINDOW, {});
    play(model, 0.25);
    const PaperFacets before = facetsOf(model);
    CHECK(!isIdentity(before, 0.01));
    model.begin(Transition::Close, params(), 12, WINDOW, {});
    CHECK(model.active());
    CHECK(maxPositionDelta(before, facetsOf(model)) < 0.02);
}

// Invalid input leaves the cloth idle rather than animating garbage.
void testInvalidInputStaysIdle() {
    ClothParams bad = params();
    bad.duration = std::numeric_limits<double>::quiet_NaN();
    ClothFlutter model;
    model.begin(Transition::Open, bad, 1, WINDOW, {});
    CHECK(!model.active());
    model.begin(Transition::Open, params(), 1, Box{0, 0, 0, 500}, {});
    CHECK(!model.active());
}

// The cloth flaps against a window right beside it but never passes behind it: the projected cloth may cover the
// neighbour only by the little it hangs in front of the neighbour's face.
void testNeighbourBlocksCloth() {
    ClothParams windy = params();
    windy.flutter = 1.0;
    const Box    neighbour{WINDOW.x + WINDOW.w + 4.0, WINDOW.y, 600.0, WINDOW.h};
    ClothFlutter model;
    model.begin(Transition::Close, windy, 5, WINDOW, std::span<const Box>(&neighbour, 1));
    double furthest = 0.0;
    while (model.active()) {
        play(model, 0.05);
        for (const auto &facet : facetsOf(model))
            for (const Vec2 &p : facet.position)
                furthest = std::max(furthest, p.x * SIZE.x - SIZE.x / 2.0);
    }
    CHECK(furthest < 60.0);
}

// A late adjacent window must not be displaced by earlier, distant obstacles.
void testNearestObstaclesWin() {
    std::vector<Box> obstacles;
    for (int i = 0; i < 32; ++i)
        obstacles.push_back({WINDOW.x + WINDOW.w + 10000.0 + i * 1000.0, WINDOW.y, 500.0, WINDOW.h});
    const Box neighbour{WINDOW.x + WINDOW.w + 4.0, WINDOW.y, 600.0, WINDOW.h};
    obstacles.push_back(neighbour);
    ClothParams windy = params();
    windy.flutter = 1.0;
    std::vector<Box> reordered{neighbour};
    reordered.insert(reordered.end(), obstacles.begin(), obstacles.end() - 1);
    ClothFlutter crowded, adjacent;
    crowded.begin(Transition::Close, windy, 5, WINDOW, obstacles);
    adjacent.begin(Transition::Close, windy, 5, WINDOW, reordered);
    std::this_thread::sleep_for(std::chrono::seconds(1)); // Let both recordings get ahead of playback.
    play(crowded, 0.4);
    play(adjacent, 0.4);
    CHECK(maxPositionDelta(facetsOf(crowded), facetsOf(adjacent)) < 1e-6);
}

} // namespace

int main() {
    testNeedsPhysics();
    {
        ClothPhysics physics;
        testOpenUnrollsToRest();
        testCloseWindsUpAndFades();
        testCloseContinuesOpening();
        testInvalidInputStaysIdle();
        testNearestObstaclesWin();
        testNeighbourBlocksCloth();
    }
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("cloth flutter tests passed");
    return 0;
}
