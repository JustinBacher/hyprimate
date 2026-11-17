#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

#include "Check.hpp"
#include "animations/paper/PaperCrumple.hpp"
#include "animations/paper/PaperTakes.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/SheetFrame.hpp"

namespace {

    using hyprimate::PaperCrumple;
    using hyprimate::PaperParams;
    using hyprimate::SheetFrame;
    using hyprimate::Transition;
    using hyprimate::Vec2;

    constexpr Vec2 SIZE{.x = 1000.0, .y = 700.0};

    PaperParams openParams() {
        return {.duration = 0.6, .crumpliness = 0.6, .shading = 0.7, .variation = 0.5};
    }
    PaperParams closeParams() {
        return {.duration = 0.5, .crumpliness = 0.6, .shading = 0.7, .variation = 0.5};
    }

    void advanceFor(PaperCrumple &model, double seconds) {
        const double STEP = 0.01;
        for (double time = 0.0; time + 1e-12 < seconds; time += STEP) {
            model.advance(std::min(STEP, seconds - time));
        }
    }

    SheetFrame frameOf(const PaperCrumple &model, Vec2 size = SIZE) {
        SheetFrame out{};
        model.frame(size, out);
        return out;
    }

    // Largest displayed distance from the centre, in logical px.
    double maxRadiusPx(const SheetFrame &frame, Vec2 size = SIZE) {
        double best = 0.0;
        for (const Vec2 &point : frame.position) {
            best = std::max(best, std::hypot(point.x * size.x, point.y * size.y));
        }
        return best;
    }

    double maxPositionDelta(const SheetFrame &a, const SheetFrame &b) {
        double best = 0.0;
        for (std::size_t vertex = 0; vertex < hyprimate::PAPER_GRID_VERTICES; ++vertex) {
            best = std::max(best, std::abs(a.position[vertex].x - b.position[vertex].x));
            best = std::max(best, std::abs(a.position[vertex].y - b.position[vertex].y));
        }
        return best;
    }

    bool isIdentity(const SheetFrame &frame, double tolerance) {
        for (std::size_t vertex = 0; vertex < hyprimate::PAPER_GRID_VERTICES; ++vertex) {
            const Vec2 TEX_COORD = hyprimate::sheetUv(vertex);
            if (std::abs(frame.position[vertex].x - (TEX_COORD.x - 0.5)) > tolerance ||
                std::abs(frame.position[vertex].y - (TEX_COORD.y - 0.5)) > tolerance) {
                return false;
            }
        }
        for (const auto &shade : frame.shade) {
            for (const float SHADE : shade) {
                if (SHADE != 0.0F) {
                    return false;
                }
            }
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
        CHECK(isIdentity(frameOf(model), 1e-12));
    }

    // 2. Open starts as a hidden ball at the window's spot.
    void testOpenStartsAsHiddenBall() {
        PaperCrumple model;
        model.begin(Transition::Open, openParams(), 7);
        CHECK(model.active());
        CHECK(model.opacity() == 0.0);
        const double RADIUS = maxRadiusPx(frameOf(model));
        const double LIMIT = 0.25 * std::sqrt(SIZE.x * SIZE.y);
        std::fprintf(stdout,
            "[measure] check 2: open start max radius %.3f px, limit %.3f px\n",
            RADIUS,
            LIMIT);
        CHECK(RADIUS <= LIMIT);
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
        midClose.advance(0.3);  // > 0.25 s step aborts: Closing -> Closed
        CHECK(midClose.closed());
        CHECK(!midClose.active());

        PaperCrumple midOpen;
        midOpen.begin(Transition::Open, openParams(), 3);
        midOpen.advance(0.3);  // > 0.25 s step aborts: Opening -> Idle
        CHECK(!midOpen.active());
        CHECK(!midOpen.closed());
        CHECK(midOpen.opacity() == 1.0);
    }

    // 4. Closing during an open keeps the geometry continuous.
    void testContinuityWhenClosingDuringOpen() {
        PaperCrumple open;
        open.begin(Transition::Open, openParams(), 1);
        advanceFor(open, 0.4 * openParams().duration);
        const SheetFrame BEFORE = frameOf(open);

        open.begin(Transition::Close, closeParams(), 99);
        const SheetFrame AFTER = frameOf(open);
        const double DELTA = maxPositionDelta(BEFORE, AFTER);
        std::fprintf(stdout, "[measure] check 4: open->close continuity max delta %.3e\n", DELTA);
        CHECK(DELTA <= 1e-9);
    }

    // 5. Determinism and variation.
    void testDeterminismAndVariation() {
        PaperCrumple sameSeedFirst;
        PaperCrumple sameSeedSecond;
        sameSeedFirst.begin(Transition::Close, closeParams(), 42);
        sameSeedSecond.begin(Transition::Close, closeParams(), 42);
        advanceFor(sameSeedFirst, 0.2);
        advanceFor(sameSeedSecond, 0.2);
        CHECK(maxPositionDelta(frameOf(sameSeedFirst), frameOf(sameSeedSecond)) == 0.0);

        PaperParams flat = closeParams();
        flat.variation = 0.0;
        PaperCrumple flatSeedFirst;
        PaperCrumple flatSeedSecond;
        flatSeedFirst.begin(Transition::Close, flat, 1);
        flatSeedSecond.begin(Transition::Close, flat, 2);
        advanceFor(flatSeedFirst, 0.2);
        advanceFor(flatSeedSecond, 0.2);
        CHECK(maxPositionDelta(frameOf(flatSeedFirst), frameOf(flatSeedSecond)) == 0.0);

        // With variation, windows differ: some seed among a handful must pick another take or
        // mirroring.
        PaperParams varied = closeParams();
        varied.variation = 1.0;
        PaperCrumple variedSeedFirst;
        variedSeedFirst.begin(Transition::Close, varied, 1);
        advanceFor(variedSeedFirst, 0.2);
        const SheetFrame FIRST = frameOf(variedSeedFirst);
        double spread = 0.0;
        for (std::uint64_t seed = 2; seed <= 8; ++seed) {
            PaperCrumple other;
            other.begin(Transition::Close, varied, seed);
            advanceFor(other, 0.2);
            spread = std::max(spread, maxPositionDelta(FIRST, frameOf(other)));
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
        advanceFor(dark, 0.25);  // s ~ 0.5
        CHECK(!dark.shaded());
        bool allZero = true;
        const SheetFrame DARK_FRAME = frameOf(dark);
        for (const auto &tri : DARK_FRAME.shade) {
            for (const float SHADE_COMPONENT : tri) {
                allZero = allZero && SHADE_COMPONENT == 0.0F;
            }
        }
        CHECK(allZero);

        PaperCrumple lit;
        lit.begin(Transition::Close, closeParams(), 5);  // shading 0.7
        advanceFor(lit, 0.25);
        bool anyNonZero = false;
        bool inRange = true;
        const SheetFrame LIT_FRAME = frameOf(lit);
        for (const auto &tri : LIT_FRAME.shade) {
            for (const float SHADE_VALUE : tri) {
                anyNonZero = anyNonZero || SHADE_VALUE != 0.0F;
                inRange = inRange && SHADE_VALUE >= -1.0F && SHADE_VALUE <= 1.0F;
            }
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
        const SheetFrame FULL_FRAME = frameOf(fullLit);
        for (const auto &tri : FULL_FRAME.shade) {
            for (const float SHADE : tri) {
                fullInRange = fullInRange && SHADE >= -1.0F && SHADE <= 1.0F;
                fullAny = fullAny || SHADE != 0.0F;
            }
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

        PaperCrumple loosePaper;
        PaperCrumple tightPaper;
        loosePaper.begin(Transition::Close, loose, 11);
        tightPaper.begin(Transition::Close, tight, 11);
        advanceFor(loosePaper, 0.75);  // s ~ 0.75
        advanceFor(tightPaper, 0.75);
        const double LOOSE_RADIUS = maxRadiusPx(frameOf(loosePaper));
        const double TIGHT_RADIUS = maxRadiusPx(frameOf(tightPaper));
        std::fprintf(stdout,
            "[measure] check 7: s=0.75 extents radius crumpliness 0 %.3f px, 1 %.3f px\n",
            LOOSE_RADIUS,
            TIGHT_RADIUS);
        CHECK(TIGHT_RADIUS < LOOSE_RADIUS);
    }

    // 8. Invalid params leave the model inactive.
    void testInvalidParams() {
        PaperParams nanShading = openParams();
        nanShading.shading = std::numeric_limits<double>::quiet_NaN();
        PaperCrumple invalidShading;
        invalidShading.begin(Transition::Open, nanShading, 1);
        CHECK(!invalidShading.active());
        CHECK(!invalidShading.closed());

        PaperParams zeroDuration = closeParams();
        zeroDuration.duration = 0.0;
        PaperCrumple zeroDurationPaper;
        zeroDurationPaper.begin(Transition::Close, zeroDuration, 1);
        CHECK(!zeroDurationPaper.active());
        CHECK(!zeroDurationPaper.closed());

        PaperParams nanDuration = closeParams();
        nanDuration.duration = std::numeric_limits<double>::quiet_NaN();
        PaperCrumple invalidDuration;
        invalidDuration.begin(Transition::Close, nanDuration, 1);
        CHECK(!invalidDuration.active());
    }

    // 9. A close starts from the window's own rest shape whatever its aspect: the baked take is
    // stretched (and swapped for portrait windows) so its flat sheet lands exactly on the window,
    // with no jump on the first frame.
    void testCloseStartsAtRestForAnyAspect() {
        for (const Vec2 SIZE : {Vec2{.x = 1000.0, .y = 700.0},
                 Vec2{.x = 600.0, .y = 1000.0},
                 Vec2{.x = 3000.0, .y = 800.0},
                 Vec2{.x = 900.0, .y = 900.0}}) {
            for (std::uint64_t seed : {1ULL, 2ULL, 3ULL, 4ULL}) {
                PaperParams params = closeParams();
                params.variation = 1.0;  // exercise mirroring and every take
                PaperCrumple model;
                model.begin(Transition::Close, params, seed);
                model.advance(1e-4);
                const SheetFrame FRAME = frameOf(model, SIZE);
                double worst = 0.0;
                for (std::size_t vertex = 0; vertex < hyprimate::PAPER_GRID_VERTICES; ++vertex) {
                    const Vec2 TEX_COORD = hyprimate::sheetUv(vertex);
                    worst = std::max({worst,
                        std::abs(FRAME.position[vertex].x - (TEX_COORD.x - 0.5)) * SIZE.x,
                        std::abs(FRAME.position[vertex].y - (TEX_COORD.y - 0.5)) * SIZE.y});
                }
                CHECK(worst < 1.0);  // under a pixel
            }
        }
    }

    // 10. A compositor stall (a long but not aborting step) delays the crumple rather than skipping
    // part of it: a 0.2 s step plays like a single 1/30 s frame.
    void testStallDelaysInsteadOfSkipping() {
        PaperCrumple stalled;
        PaperCrumple smooth;
        stalled.begin(Transition::Close, closeParams(), 7);
        smooth.begin(Transition::Close, closeParams(), 7);
        stalled.advance(0.2);
        smooth.advance(1.0 / 30.0);
        CHECK(stalled.active());
        CHECK(maxPositionDelta(frameOf(stalled), frameOf(smooth)) == 0.0);
    }

    // 11. Along a real take's playback, frame.order is back to front by (mean world z, triangle
    // id), as a fresh sort computes it from the same world data. One SheetFrame carries each order
    // into the next frame, so every projection seeds from the previous one: this checks the seeded
    // sort never drifts from a fresh sort. Each take plays forward (open) and back (close).
    void testOrderMatchesFreshSortAlongPlayback() {
        using hyprimate::PAPER_GRID_VERTICES;
        using hyprimate::PAPER_TRIANGLES;
        const int FRAMES = hyprimate::paperTakeFrames();
        SheetFrame out{};
        for (int take = 0; take < hyprimate::paperTakeCount(); ++take) {
            std::vector<int> path;
            for (int frameIndex = 0; frameIndex < FRAMES; ++frameIndex) {
                path.push_back(frameIndex);
            }
            for (int reverseFrame = FRAMES - 1; reverseFrame >= 0; --reverseFrame) {
                path.push_back(reverseFrame);
            }
            for (const int FRAME_INDEX : path) {
                hyprimate::SheetGrid world{};
                for (std::size_t vertex = 0; vertex < PAPER_GRID_VERTICES; ++vertex) {
                    double position[3];
                    hyprimate::paperTakeVertex(take, FRAME_INDEX, vertex, position);
                    world[vertex] = {.x = position[0], .y = position[1], .z = position[2]};
                }
                hyprimate::projectSheet(
                    world, SIZE, 0.7, hyprimate::SheetLighting::PaperCreases, out);

                // Independent recomputation: mean z per triangle with the same arithmetic, then a
                // fresh sort of (z, t) pairs.
                std::vector<std::pair<double, std::uint16_t>> keyed;
                keyed.reserve(PAPER_TRIANGLES);
                for (std::size_t triangleIndex = 0; triangleIndex < PAPER_TRIANGLES; ++triangleIndex) {
                    const auto &corners = hyprimate::sheetTriangle(triangleIndex);
                    const double DEPTH =
                        (world[corners[0]].z + world[corners[1]].z + world[corners[2]].z) / 3.0;
                    keyed.emplace_back(DEPTH, static_cast<std::uint16_t>(triangleIndex));
                }
                std::sort(keyed.begin(), keyed.end());

                bool same = true;
                for (std::size_t i = 0; i < PAPER_TRIANGLES; ++i) {
                    same = same && out.order[i] == keyed[i].second;
                }
                CHECK(same);
                if (!same) {
                    std::printf("order mismatch: take %d frame %d\n", take, FRAME_INDEX);
                    return;
                }
            }
        }
    }

}  // namespace

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
    testOrderMatchesFreshSortAlongPlayback();
    return g_failures == 0 ? 0 : 1;
}
