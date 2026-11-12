#include "PaperCrumple.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using liquid_glass::PaperCrumple;
using liquid_glass::PaperFacets;
using liquid_glass::PaperParams;
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

PaperParams openParams() { return {.duration = 0.6, .crumpliness = 0.6, .shading = 0.7, .variation = 0.5}; }
PaperParams closeParams() { return {.duration = 0.5, .crumpliness = 0.6, .shading = 0.7, .variation = 0.5}; }

void advanceFor(PaperCrumple &model, double seconds) {
    const double step = 0.01;
    for (double t = 0.0; t + 1e-12 < seconds; t += step)
        model.advance(std::min(step, seconds - t));
}

PaperFacets facetsOf(const PaperCrumple &model, Vec2 size = SIZE) {
    PaperFacets out{};
    model.facets(size, out);
    return out;
}

// Largest displayed distance from the centre, in logical px.
double maxRadiusPx(const PaperFacets &facets, Vec2 size = SIZE) {
    double best = 0.0;
    for (const auto &facet : facets)
        for (const Vec2 &p : facet.position)
            best = std::max(best, std::hypot(p.x * size.x, p.y * size.y));
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

bool isIdentity(const PaperFacets &facets, double tolerance) {
    for (const auto &facet : facets)
        for (std::size_t c = 0; c < 3; ++c) {
            if (std::abs(facet.position[c].x - (facet.uv[c].x - 0.5)) > tolerance ||
                std::abs(facet.position[c].y - (facet.uv[c].y - 0.5)) > tolerance || facet.shade[c] != 0.0)
                return false;
        }
    return true;
}

// 1. Open ends at identity.
void testOpenEndsAtIdentity() {
    PaperCrumple model;
    model.begin(Transition::Open, openParams(), 7);
    advanceFor(model, 1.0);
    CHECK(!model.active());
    CHECK(!model.closed());
    CHECK(model.opacity() == 1.0);
    CHECK(isIdentity(facetsOf(model), 1e-12));
}

// 2. Open starts as a hidden ball at the window's spot.
void testOpenStartsAsHiddenBall() {
    PaperCrumple model;
    model.begin(Transition::Open, openParams(), 7);
    CHECK(model.active());
    CHECK(model.opacity() == 0.0);
    const double radius = maxRadiusPx(facetsOf(model));
    const double limit = 0.25 * std::sqrt(SIZE.x * SIZE.y);
    std::fprintf(stdout, "[measure] check 2: open start max radius %.3f px, limit %.3f px\n", radius, limit);
    CHECK(radius <= limit);
}

// 3. Close finishes; mid-close and mid-open steps behave as specified.
void testCloseFinishesAndAbortsOnLargeStep() {
    PaperCrumple close;
    close.begin(Transition::Close, closeParams(), 3);
    advanceFor(close, 0.6);
    CHECK(close.closed());
    CHECK(!close.active());
    CHECK(close.opacity() == 0.0);

    PaperCrumple midClose;
    midClose.begin(Transition::Close, closeParams(), 3);
    midClose.advance(0.3); // > 0.25 s step aborts: Closing -> Closed
    CHECK(midClose.closed());
    CHECK(!midClose.active());

    PaperCrumple midOpen;
    midOpen.begin(Transition::Open, openParams(), 3);
    midOpen.advance(0.3); // > 0.25 s step aborts: Opening -> Idle
    CHECK(!midOpen.active());
    CHECK(!midOpen.closed());
    CHECK(midOpen.opacity() == 1.0);
}

// 4. Closing during an open keeps the geometry continuous.
void testContinuityWhenClosingDuringOpen() {
    PaperCrumple open;
    open.begin(Transition::Open, openParams(), 1);
    advanceFor(open, 0.4 * openParams().duration);
    const PaperFacets before = facetsOf(open);

    open.begin(Transition::Close, closeParams(), 99);
    const PaperFacets after = facetsOf(open);
    const double      delta = maxPositionDelta(before, after);
    std::fprintf(stdout, "[measure] check 4: open->close continuity max delta %.3e\n", delta);
    CHECK(delta <= 1e-9);
}

// 5. Determinism and variation.
void testDeterminismAndVariation() {
    PaperCrumple a, b;
    a.begin(Transition::Close, closeParams(), 42);
    b.begin(Transition::Close, closeParams(), 42);
    advanceFor(a, 0.2);
    advanceFor(b, 0.2);
    CHECK(maxPositionDelta(facetsOf(a), facetsOf(b)) == 0.0);

    PaperParams flat = closeParams();
    flat.variation = 0.0;
    PaperCrumple s1, s2;
    s1.begin(Transition::Close, flat, 1);
    s2.begin(Transition::Close, flat, 2);
    advanceFor(s1, 0.2);
    advanceFor(s2, 0.2);
    CHECK(maxPositionDelta(facetsOf(s1), facetsOf(s2)) == 0.0);

    // With variation, windows differ: some seed among a handful must pick another take or mirroring.
    PaperParams varied = closeParams();
    varied.variation = 1.0;
    PaperCrumple v1;
    v1.begin(Transition::Close, varied, 1);
    advanceFor(v1, 0.2);
    const PaperFacets first = facetsOf(v1);
    double            spread = 0.0;
    for (std::uint64_t seed = 2; seed <= 8; ++seed) {
        PaperCrumple other;
        other.begin(Transition::Close, varied, seed);
        advanceFor(other, 0.2);
        spread = std::max(spread, maxPositionDelta(first, facetsOf(other)));
    }
    std::fprintf(stdout, "[measure] check 5: variation 1 seed spread %.3e\n", spread);
    CHECK(spread > 1e-3);
}

// 6. Shading.
void testShading() {
    PaperParams unlit = closeParams();
    unlit.shading = 0.0;
    PaperCrumple dark;
    dark.begin(Transition::Close, unlit, 5);
    advanceFor(dark, 0.25); // s ~ 0.5
    CHECK(!dark.shaded());
    bool allZero = true;
    for (const auto &facet : facetsOf(dark))
        for (const double s : facet.shade)
            allZero = allZero && s == 0.0;
    CHECK(allZero);

    PaperCrumple lit;
    lit.begin(Transition::Close, closeParams(), 5); // shading 0.7
    advanceFor(lit, 0.25);
    bool anyNonZero = false, inRange = true;
    for (const auto &facet : facetsOf(lit))
        for (const double s : facet.shade) {
            anyNonZero = anyNonZero || s != 0.0;
            inRange = inRange && s >= -1.0 && s <= 1.0;
        }
    CHECK(anyNonZero);
    CHECK(inRange);

    PaperParams full = closeParams();
    full.shading = 1.0;
    PaperCrumple fullLit;
    fullLit.begin(Transition::Close, full, 5);
    advanceFor(fullLit, 0.25);
    bool fullInRange = true;
    bool fullAny = false;
    for (const auto &facet : facetsOf(fullLit))
        for (const double s : facet.shade) {
            fullInRange = fullInRange && s >= -1.0 && s <= 1.0;
            fullAny = fullAny || s != 0.0;
        }
    CHECK(fullAny);
    CHECK(fullInRange);
}

// 7. Crumpliness: higher crumpliness gives a smaller ball.
void testCrumplinessShrinksBall() {
    PaperParams loose;
    loose.duration = 1.0;
    loose.crumpliness = 0.0;
    PaperParams tight = loose;
    tight.crumpliness = 1.0;

    PaperCrumple a, b;
    a.begin(Transition::Close, loose, 11);
    b.begin(Transition::Close, tight, 11);
    advanceFor(a, 0.75); // s ~ 0.75
    advanceFor(b, 0.75);
    const double looseRadius = maxRadiusPx(facetsOf(a));
    const double tightRadius = maxRadiusPx(facetsOf(b));
    std::fprintf(stdout, "[measure] check 7: s=0.75 extents radius crumpliness 0 %.3f px, 1 %.3f px\n", looseRadius,
                 tightRadius);
    CHECK(tightRadius < looseRadius);
}

// 8. Invalid params leave the model inactive.
void testInvalidParams() {
    PaperParams nanShading = openParams();
    nanShading.shading = std::numeric_limits<double>::quiet_NaN();
    PaperCrumple a;
    a.begin(Transition::Open, nanShading, 1);
    CHECK(!a.active());
    CHECK(!a.closed());

    PaperParams zeroDuration = closeParams();
    zeroDuration.duration = 0.0;
    PaperCrumple b;
    b.begin(Transition::Close, zeroDuration, 1);
    CHECK(!b.active());
    CHECK(!b.closed());

    PaperParams nanDuration = closeParams();
    nanDuration.duration = std::numeric_limits<double>::quiet_NaN();
    PaperCrumple c;
    c.begin(Transition::Close, nanDuration, 1);
    CHECK(!c.active());
}

// 9. A close starts from the window's own rest shape whatever its aspect: the baked take is stretched (and swapped
// for portrait windows) so its flat sheet lands exactly on the window, with no jump on the first frame.
void testCloseStartsAtRestForAnyAspect() {
    for (const Vec2 size : {Vec2{1000.0, 700.0}, Vec2{600.0, 1000.0}, Vec2{3000.0, 800.0}, Vec2{900.0, 900.0}}) {
        for (std::uint64_t seed : {1ULL, 2ULL, 3ULL, 4ULL}) {
            PaperParams params = closeParams();
            params.variation = 1.0; // exercise mirroring and every take
            PaperCrumple model;
            model.begin(Transition::Close, params, seed);
            model.advance(1e-4);
            const PaperFacets facets = facetsOf(model, size);
            double            worst = 0.0;
            for (const auto &facet : facets)
                for (std::size_t c = 0; c < 3; ++c)
                    worst = std::max({worst, std::abs(facet.position[c].x - (facet.uv[c].x - 0.5)) * size.x,
                                      std::abs(facet.position[c].y - (facet.uv[c].y - 0.5)) * size.y});
            CHECK(worst < 1.0); // under a pixel
        }
    }
}

// 10. A compositor stall (a long but not aborting step) delays the crumple rather than skipping part of it: a 0.2 s
// step plays like a single 1/30 s frame.
void testStallDelaysInsteadOfSkipping() {
    PaperCrumple stalled, smooth;
    stalled.begin(Transition::Close, closeParams(), 7);
    smooth.begin(Transition::Close, closeParams(), 7);
    stalled.advance(0.2);
    smooth.advance(1.0 / 30.0);
    CHECK(stalled.active());
    CHECK(maxPositionDelta(facetsOf(stalled), facetsOf(smooth)) == 0.0);
}

} // namespace

int main() {
    testOpenEndsAtIdentity();
    testOpenStartsAsHiddenBall();
    testCloseFinishesAndAbortsOnLargeStep();
    testContinuityWhenClosingDuringOpen();
    testDeterminismAndVariation();
    testShading();
    testCrumplinessShrinksBall();
    testInvalidParams();
    testCloseStartsAtRestForAnyAspect();
    testStallDelaysInsteadOfSkipping();
    return g_failures == 0 ? 0 : 1;
}
