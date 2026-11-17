#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "Check.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"

namespace {

    using hyprimate::Transition;
    using hyprimate::TransitionParams;
    using hyprimate::Vec2;

    constexpr double kStrength = 0.65;

    // Task 2: edge selection.
    constexpr hyprimate::Box kScreen{.x = 0.0, .y = 0.0, .w = 1920.0, .h = 1080.0};
    hyprimate::Box withShadow(hyprimate::Box b) {
        return {.x = b.x - 12.0, .y = b.y - 12.0, .w = b.w + 24.0, .h = b.h + 24.0};
    }
    std::optional<hyprimate::Edge> pick(hyprimate::Box body,
        hyprimate::Edge fallback,
        bool first,
        std::span<const hyprimate::Box> neighbours = {},
        hyprimate::Box screen = kScreen) {
        const auto f = hyprimate::resolveEdgeFrame(
            body, withShadow(body), screen, neighbours, fallback, first);
        return f ? std::optional{f->edge} : std::nullopt;
    }

    void testEdgeFollowsDwindleSplits() {
        using hyprimate::Edge;
        CHECK(pick({24, 24, 1872, 1032}, Edge::Bottom, true) ==
              Edge::Bottom);  // first window: configured side
        CHECK(pick({24, 24, 1872, 1032}, Edge::Left, true) == Edge::Left);
        CHECK(pick({972, 24, 924, 1032}, Edge::Bottom, false) == Edge::Right);  // right half
        CHECK(pick({972, 548, 924, 508}, Edge::Bottom, false) ==
              Edge::Bottom);  // bottom-right quarter
        CHECK(pick({1438, 548, 458, 508}, Edge::Bottom, false) == Edge::Right);  // right eighth
    }

    void testEdgeTiesAndValidity() {
        using hyprimate::Edge;
        CHECK(pick({760, 340, 400, 400}, Edge::Top, false) ==
              Edge::Top);  // top/bottom tie -> fallback
        CHECK(pick({760, 340, 400, 400}, Edge::Right, false) ==
              Edge::Bottom);  // fallback not tied -> order
        CHECK(pick({500, 900, 400, 300}, Edge::Bottom, true) ==
              Edge::Left);  // fallback invalid (crosses bottom)
        CHECK(!pick({-10, -10, 1940, 1100}, Edge::Bottom, false));  // crosses every edge
        CHECK(!pick({0, 0, 0, 100}, Edge::Bottom, false));  // unusable
    }

    void testFrameGeometry() {
        const hyprimate::Box BODY{.x = 972, .y = 548, .w = 924, .h = 508};
        const auto f = hyprimate::resolveEdgeFrame(
            BODY, withShadow(BODY), kScreen, {}, hyprimate::Edge::Bottom, false);
        CHECK(f && f->edge == hyprimate::Edge::Bottom);
        // Capture centre (1434, 802): 278 px above the bottom edge; capture half height 266.
        CHECK(std::abs(f->edgeDistance - 278.0) < 1e-9 && std::abs(f->captureFar - 544.0) < 1e-9);
        CHECK(std::abs(f->bodyNear - 24.0) < 1e-9 && std::abs(f->bodyFar - 532.0) < 1e-9);
        CHECK(std::abs(f->bodyT0 + 462.0) < 1e-9 && std::abs(f->bodyT1 - 462.0) < 1e-9);
        // The frame maps round-trip, and n grows into the screen.
        const hyprimate::Vec2 p = hyprimate::edgePoint(*f, 40.0, 100.0);
        CHECK(std::abs(hyprimate::edgeNormal(*f, p) - 100.0) < 1e-9 &&
              std::abs(hyprimate::edgeTangent(*f, p) - 40.0) < 1e-9);
        CHECK(std::abs(p.y - 178.0) < 1e-9);  // 278 - 100, y down from the capture centre
    }

    // close_side = origin: the origin edge wins when valid, otherwise the nearest valid edge.
    void testOriginEdgeWhenValidElseNearest() {
        using hyprimate::Edge;
        CHECK(pick({972, 548, 924, 508}, Edge::Top, true) ==
              Edge::Top);  // body nearest Bottom, origin Top valid
        CHECK(pick({760, 340, 400, 400}, Edge::Left, true) ==
              Edge::Left);  // origin Left valid, Top/Bottom tied
        CHECK(pick({500, 900, 400, 300}, Edge::Bottom, true) ==
              Edge::Left);  // origin Bottom invalid -> nearest (Left)
        CHECK(pick({972, 548, 924, 508}, Edge::Right, true) ==
              Edge::Right);  // origin Right valid though Bottom is nearer
        CHECK(pick({1438, 24, 458, 300}, Edge::Right, true) ==
              Edge::Right);  // origin Right valid (24 px gap)
        CHECK(pick({1000, 24, 300, 300}, Edge::Right, true) ==
              Edge::Right);  // origin Right valid: preferred over nearer Top
        CHECK(pick({1000, 24, 300, 300}, Edge::Left, true) ==
              Edge::Left);  // origin Left valid, preferred
        CHECK(pick({-10, -10, 1940, 1100}, Edge::Right, true) ==
              std::nullopt);  // origin invalid and no valid edge
    }

    // Contact rule: a tiled window emerges from the side away from the neighbours it sits beside.
    constexpr hyprimate::Box kWideScreen{.x = 0.0, .y = 0.0, .w = 1600.0, .h = 900.0};

    void testDwindleContactKeepsDwindleEdges() {
        using hyprimate::Edge;
        const std::array<hyprimate::Box, 1> LEFT_HALF{{{.x = 24, .y = 24, .w = 924, .h = 1032}}};
        CHECK(pick({972, 24, 924, 1032}, Edge::Bottom, false, LEFT_HALF) ==
              Edge::Right);  // right half beside left half
        const std::array<hyprimate::Box, 1> TOP_RIGHT{{{.x = 972, .y = 24, .w = 924, .h = 508}}};
        CHECK(pick({972, 548, 924, 508}, Edge::Bottom, false, TOP_RIGHT) ==
              Edge::Bottom);  // bottom-right quarter below top-right
        const std::array<hyprimate::Box, 1> BOTTOM_RIGHT{
            {{.x = 972, .y = 548, .w = 924, .h = 508}}};
        CHECK(pick({972, 24, 924, 508}, Edge::Bottom, false, BOTTOM_RIGHT) ==
              Edge::Top);  // top-right quarter above bottom-right
        const std::array<hyprimate::Box, 1> SIBLING{{{.x = 972, .y = 548, .w = 458, .h = 508}}};
        CHECK(pick({1438, 548, 458, 508}, Edge::Bottom, false, SIBLING) ==
              Edge::Right);  // right eighth beside its sibling
    }

    void testScrollingColumnsEmergeAwayFromNeighbours() {
        using hyprimate::Edge;
        // 1600x900 monitor, full-height columns: gaps_out top 15, bottom 30, sides 30; gaps_in 12.
        const std::array<hyprimate::Box, 1> LEFT{{{.x = 30, .y = 15, .w = 700, .h = 855}}};
        CHECK(pick({742, 15, 800, 855}, Edge::Bottom, false, LEFT, kWideScreen) ==
              Edge::Right);  // new column right of the left column
        const std::array<hyprimate::Box, 1> RIGHT{{{.x = 870, .y = 15, .w = 700, .h = 855}}};
        CHECK(pick({58, 15, 800, 855}, Edge::Bottom, false, RIGHT, kWideScreen) ==
              Edge::Left);  // new column left of the right column
        const std::array<hyprimate::Box, 2> AROUND{
            {{.x = 30, .y = 15, .w = 400, .h = 855}, {.x = 1060, .y = 15, .w = 510, .h = 855}}};
        CHECK(pick({442, 15, 600, 855}, Edge::Bottom, false, AROUND, kWideScreen) ==
              Edge::Left);  // between two columns, nearer Left
        const std::array<hyprimate::Box, 2> AROUND2{
            {{.x = 30, .y = 15, .w = 600, .h = 855}, {.x = 1000, .y = 15, .w = 570, .h = 855}}};
        CHECK(pick({640, 15, 360, 855}, Edge::Bottom, false, AROUND2, kWideScreen) ==
              Edge::Right);  // between two columns, nearer Right
        CHECK(pick({742, 15, 800, 855}, Edge::Bottom, false, {}, kWideScreen) ==
              Edge::Top);  // no neighbours: nearest edge
        CHECK(pick({742, 15, 800, 855}, Edge::Top, true, LEFT, kWideScreen) ==
              Edge::Top);  // first window: side beats contact
    }

    void testInvalidContactCandidateFallsBackToNearest() {
        using hyprimate::Edge;
        // The body crosses the right screen edge, so the Right candidate is invalid; the nearest
        // valid edge is Top.
        const std::array<hyprimate::Box, 1> LEFT{{{.x = 600, .y = 24, .w = 380, .h = 400}}};
        CHECK(pick({1000, 24, 1000, 400}, Edge::Bottom, false, LEFT) == Edge::Top);
        CHECK(pick({1000, 24, 1000, 400}, Edge::Bottom, false) == Edge::Top);
    }

    void testOnlyTouchingNeighboursCount() {
        using hyprimate::Edge;
        const hyprimate::Box BODY{.x = 972, .y = 548, .w = 924, .h = 508};
        const std::array<hyprimate::Box, 1> GAP64{
            {{.x = 600, .y = 548, .w = 308, .h = 508}}};  // facing gap 64 px: counts
        CHECK(pick(BODY, Edge::Bottom, false, GAP64) == Edge::Right);
        const std::array<hyprimate::Box, 1> GAP100{
            {{.x = 600, .y = 548, .w = 272, .h = 508}}};  // facing gap 100 px: ignored
        CHECK(pick(BODY, Edge::Bottom, false, GAP100) == Edge::Bottom);
        const std::array<hyprimate::Box, 1> ASIDE{
            {{.x = 600, .y = 24, .w = 360, .h = 400}}};  // 12 px gap, no vertical overlap: ignored
        CHECK(pick(BODY, Edge::Bottom, false, ASIDE) == Edge::Bottom);
        const std::array<hyprimate::Box, 1> OVERLAPPING{
            {{.x = 1200, .y = 300, .w = 600, .h = 600}}};  // floating-like overlap: ignored
        CHECK(pick(BODY, Edge::Bottom, false, OVERLAPPING) == Edge::Bottom);
    }

    // Both materials call this close policy, including an in-place cloth opening's logical origin.
    void testSharedCloseDirectionPolicy() {
        using hyprimate::Box;
        using hyprimate::CloseSide;
        using hyprimate::Edge;
        const Box BODY{.x = 972, .y = 548, .w = 924, .h = 508};
        const std::array<Box, 1> LEFT{{{.x = 600, .y = 548, .w = 308, .h = 508}}};
        const auto CLOSE = [&](CloseSide side, std::optional<Edge> origin = std::nullopt) {
            const auto FRAME = hyprimate::resolveCloseEdgeFrame(
                BODY, withShadow(BODY), kScreen, LEFT, Edge::Bottom, side, origin);
            return FRAME ? std::optional{FRAME->edge} : std::nullopt;
        };
        CHECK(CLOSE(CloseSide::Auto) == Edge::Right);
        CHECK(CLOSE(CloseSide::Nearest) == Edge::Bottom);  // ignores tiled contact
        CHECK(CLOSE(CloseSide::Origin, Edge::Top) == Edge::Top);
        CHECK(CLOSE(CloseSide::Origin) == CLOSE(CloseSide::Auto));
        const auto LAST = hyprimate::resolveCloseEdgeFrame(
            BODY, withShadow(BODY), kScreen, {}, Edge::Top, CloseSide::Auto, std::nullopt, true);
        CHECK(LAST && LAST->edge == Edge::Top);
        const auto NEAREST = hyprimate::resolveCloseEdgeFrame(
            BODY, withShadow(BODY), kScreen, {}, Edge::Top, CloseSide::Nearest, std::nullopt, true);
        CHECK(NEAREST && NEAREST->edge == Edge::Bottom);
        for (const auto &[SIDE, EDGE] :
            std::array<std::pair<CloseSide, Edge>, 4>{{{CloseSide::Left, Edge::Left},
                {CloseSide::Right, Edge::Right},
                {CloseSide::Top, Edge::Top},
                {CloseSide::Bottom, Edge::Bottom}}}) {
            CHECK(CLOSE(SIDE) == EDGE);
            CHECK(CLOSE(SIDE, Edge::Top) ==
                  EDGE);  // an opening origin does not override a fixed direction
        }

        const Box CROSSING_BOTTOM{.x = 500, .y = 900, .w = 400, .h = 300};
        const auto FALLBACK = hyprimate::resolveCloseEdgeFrame(CROSSING_BOTTOM,
            withShadow(CROSSING_BOTTOM),
            kScreen,
            {},
            Edge::Bottom,
            CloseSide::Bottom);
        CHECK(FALLBACK &&
              FALLBACK->edge == Edge::Left);  // fixed preference invalid: normal resolver fallback
        const auto ORIGIN_FALLBACK = hyprimate::resolveCloseEdgeFrame(CROSSING_BOTTOM,
            withShadow(CROSSING_BOTTOM),
            kScreen,
            {},
            Edge::Bottom,
            CloseSide::Origin,
            Edge::Bottom);
        CHECK(ORIGIN_FALLBACK && ORIGIN_FALLBACK->edge == Edge::Left);
    }

    void testCloseMonitorAndWorkAreaBounds() {
        using hyprimate::Box;
        using hyprimate::CloseSide;
        using hyprimate::Edge;
        const Box BODY{.x = 760, .y = 340, .w = 400, .h = 400};
        const auto SCREEN = hyprimate::resolveCloseEdgeFrame(
            BODY, withShadow(BODY), kScreen, {}, Edge::Bottom, CloseSide::Nearest);
        const auto WORK = hyprimate::resolveCloseEdgeFrame(BODY,
            withShadow(BODY),
            {.x = 0, .y = 100, .w = 1920, .h = 980},
            {},
            Edge::Bottom,
            CloseSide::Nearest);
        CHECK(SCREEN && SCREEN->edge == Edge::Bottom);
        CHECK(WORK && WORK->edge == Edge::Top);

        // Translating the monitor, window, capture and neighbours must preserve every direction and
        // boundary distance.
        const Box SHIFTED_BODY{.x = BODY.x - 1920, .y = BODY.y - 200, .w = BODY.w, .h = BODY.h};
        for (CloseSide side : {CloseSide::Auto,
                 CloseSide::Nearest,
                 CloseSide::Origin,
                 CloseSide::Top,
                 CloseSide::Bottom,
                 CloseSide::Left,
                 CloseSide::Right}) {
            const auto a = hyprimate::resolveCloseEdgeFrame(
                BODY, withShadow(BODY), kScreen, {}, Edge::Bottom, side, Edge::Right);
            const auto b = hyprimate::resolveCloseEdgeFrame(SHIFTED_BODY,
                withShadow(SHIFTED_BODY),
                {.x = -1920, .y = -200, .w = 1920, .h = 1080},
                {},
                Edge::Bottom,
                side,
                Edge::Right);
            CHECK(a && b && a->edge == b->edge);
            CHECK(a && b && std::abs(a->edgeDistance - b->edgeDistance) < 1e-9);
        }
        CHECK(!hyprimate::resolveCloseEdgeFrame(
            BODY, withShadow(BODY), {}, {}, Edge::Bottom, CloseSide::Auto));
        CHECK(!hyprimate::resolveCloseEdgeFrame(
            {-10, -10, 1940, 1100}, kScreen, kScreen, {}, Edge::Bottom, CloseSide::Right));
    }

    // Bead transitions.
    using hyprimate::EdgeFrame;
    using hyprimate::EdgeTransition;
    using hyprimate::LiquidMesh;
    constexpr double kStep = 1.0 / 240.0;

    EdgeFrame frameFor(hyprimate::Box body,
        hyprimate::Edge fallback = hyprimate::Edge::Bottom,
        bool first = false) {
        return *hyprimate::resolveEdgeFrame(body, withShadow(body), kScreen, {}, fallback, first);
    }
    const EdgeFrame kQuarter = frameFor({.x = 972, .y = 548, .w = 924, .h = 508});  // Bottom edge
    // Capture (decorated + shadow) size of kQuarter: 948 x 532, half extents 474 x 266.
    constexpr Vec2 kCapture{.x = 948.0, .y = 532.0};

    // Displayed centre displacement (logical px) from the rest capture centre.
    Vec2 centreOf(const EdgeTransition &tr) {
        return tr.map({.x = 0.0, .y = 0.0}, kCapture);
    }

    // Movement-field gain for `mesh` and `tr` at the rest capture size (no grid).
    double fitGain(
        const LiquidMesh &mesh, const EdgeTransition &tr, Vec2 comp = {.x = 1.0, .y = 1.0}) {
        return hyprimate::fitLiquidPoseGain(
            hyprimate::LiquidPose{.mesh = mesh, .transition = tr, .compensation = comp},
            kCapture,
            nullptr);
    }

    // Centre displacement into the screen along the edge normal (0 at rest, negative toward and
    // behind the edge).
    double centreInward(const EdgeTransition &tr) {
        const Vec2 c = centreOf(tr);
        const Vec2 in = hyprimate::inwardNormal(tr.frame().edge);
        return (c.x * in.x) + (c.y * in.y);
    }

    // Displayed n (into the screen) of the body near side at the body's tangential centre.
    double nearSide(const EdgeTransition &tr) {
        const EdgeFrame &f = tr.frame();
        const Vec2 p = hyprimate::edgePoint(f, 0.5 * (f.bodyT0 + f.bodyT1), f.bodyNear);
        return hyprimate::edgeNormal(f, tr.map(p, kCapture));
    }
    double centreT(const EdgeFrame &f) {
        return 0.5 * (f.bodyT0 + f.bodyT1);
    }

    void testOpenStartsHiddenAndSettlesExactly() {
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        CHECK(tr.active());
        // Hidden start: every capture corner is on the far side of the edge (n <= 0).
        for (const double sx : {-1.0, 1.0}) {
            for (const double sy : {-1.0, 1.0}) {
                CHECK(hyprimate::edgeNormal(kQuarter, tr.map({sx * 474.0, sy * 266.0}, kCapture)) <=
                      1e-6);
            }
        }
        const Vec2 FIRST = tr.takeTravel();
        CHECK(FIRST.x == 0.0 && FIRST.y == 0.0);  // begin is not travel
        double maxInward = -1e9;
        for (int i = 0; i < 960 && tr.active(); ++i) {
            tr.advance(kStep);
            maxInward = std::max(maxInward, centreInward(tr));
        }
        CHECK(!tr.active());
        // Exact identity once settled.
        const Vec2 c = centreOf(tr);
        CHECK(c.x == 0.0 && c.y == 0.0);
        CHECK(tr.map({123.0, -45.0}, kCapture).x == 123.0 &&
              tr.map({123.0, -45.0}, kCapture).y == -45.0);
        CHECK(maxInward > 5.0);  // visible pop overshoot
    }

    // The frame's capture is taken when the open begins, before the shadow reports its extents; the
    // live capture is larger. Once the spring is nearly settled, the window must fill the live
    // capture along the normal, not the frame's.
    void testOpenFillsLiveCaptureAlongNormal() {
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        constexpr Vec2 K_LIVE{
            .x = kCapture.x, .y = kCapture.y + 24.0};  // Bottom edge: the normal is vertical
        double worst = 0.0;
        int checked = 0;
        for (int i = 0; i < 960 && tr.active(); ++i) {
            tr.advance(kStep);
            if (i < 240 || !tr.active()) {
                continue;  // from 1 s of 240 Hz steps the spring is within a fraction of a pixel
            }
            const double FAR =
                hyprimate::edgeNormal(kQuarter, tr.map({.x = 0.0, .y = -0.5 * K_LIVE.y}, K_LIVE));
            const double NEAR =
                hyprimate::edgeNormal(kQuarter, tr.map({.x = 0.0, .y = 0.5 * K_LIVE.y}, K_LIVE));
            worst = std::max(worst, std::abs((FAR - NEAR) - K_LIVE.y));
            ++checked;
        }
        CHECK(checked > 0);
        CHECK(worst < 1.0);
    }

    // The map never folds the image: every cell of a 33x33 grid keeps its positive orientation
    // during the open.
    void testBeadMapDoesNotFold() {
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        constexpr int K_SIDE = 33;
        for (int frame = 0; frame < 240 && tr.active(); ++frame) {
            tr.advance(kStep);
            if (!tr.active()) {
                break;
            }
            std::array<Vec2, K_SIDE * K_SIDE> mapped{};
            for (int j = 0; j < K_SIDE; ++j) {
                for (int i = 0; i < K_SIDE; ++i) {
                    const double u = static_cast<double>(i) / (K_SIDE - 1);
                    const double v = static_cast<double>(j) / (K_SIDE - 1);
                    mapped[(j * K_SIDE) + i] = tr.map(
                        {.x = (u - 0.5) * kCapture.x, .y = (v - 0.5) * kCapture.y}, kCapture);
                }
            }
            for (int j = 0; j + 1 < K_SIDE; ++j) {
                for (int i = 0; i + 1 < K_SIDE; ++i) {
                    const Vec2 a = mapped[(j * K_SIDE) + i];
                    const Vec2 b = mapped[(j * K_SIDE) + i + 1];
                    const Vec2 c = mapped[((j + 1) * K_SIDE) + i];
                    const double AREA = ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
                    CHECK(AREA > 0.0);
                }
            }
        }
    }

    void testOpenEdgesPointTheRightWay() {
        using hyprimate::Edge;
        const std::array<std::pair<Edge, Vec2>, 4> CASES{{{Edge::Bottom, {.x = 0, .y = 1}},
            {Edge::Top, {.x = 0, .y = -1}},
            {Edge::Left, {.x = -1, .y = 0}},
            {Edge::Right, {.x = 1, .y = 0}}}};
        for (const auto &[EDGE, SIGN] : CASES) {
            EdgeTransition tr;
            tr.begin(Transition::Open,
                frameFor({.x = 24, .y = 24, .w = 1872, .h = 1032}, EDGE, true),
                TransitionParams{},
                kStrength);
            const Vec2 o = centreOf(tr);
            CHECK(o.x * SIGN.x + o.y * SIGN.y > 200.0);  // starts hidden beyond that edge
        }
    }

    // Meniscus during open: the neck attaches, snaps once, and recoils.
    void testOpenNeckAttachesSnapsOnceAndRecoils() {
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        int snaps = 0;
        bool tucked = false;
        bool sawZeroAfterSnap = false;
        bool secondLobe = false;
        double prevPeak = tr.meniscusPeak();
        double prevNear = nearSide(tr);
        for (int i = 0; i < 480 && tr.active(); ++i) {
            tr.advance(kStep);
            const double PEAK = tr.meniscusPeak();
            const double n = nearSide(tr);
            CHECK(PEAK >= 0.0);
            // Tolerance 8 px: the recoil spring legitimately moves ~5 px per substep. Snaps to or
            // from zero (>= Fs+Lp ~ 44 px) still fail.
            CHECK(std::abs(PEAK - prevPeak) <= std::abs(n - prevNear) + 8.0);  // continuous
            if (n >= 0.0 && PEAK > n + 1.0) {
                tucked = true;  // tip under the window
            }
            if (tucked && PEAK < n - 1.0 && snaps == 0) {
                ++snaps;  // gap opened: snapped
            } else if (snaps == 1 && PEAK > n + 1.0) {
                ++snaps;  // must never re-attach
            }
            if (snaps == 1 && PEAK == 0.0) {
                sawZeroAfterSnap = true;
            }
            if (sawZeroAfterSnap && PEAK > 0.5) {
                secondLobe = true;  // the bar's small rebound bump
            }
            prevPeak = PEAK;
            prevNear = n;
        }
        CHECK(tucked);
        CHECK(snaps == 1);
        CHECK(secondLobe);
        CHECK(tr.meniscusPeak() == 0.0);
    }

    void testMeniscusShapeBounds() {
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        for (int i = 0; i < 240 && tr.active(); ++i) {
            tr.advance(kStep);
            const double lo = tr.meniscusLow();
            const double hi = tr.meniscusHigh();
            CHECK(lo <= hi);
            CHECK(tr.meniscus(lo - 1.0) == 0.0 && tr.meniscus(hi + 1.0) == 0.0);
            for (int s = 0; s <= 64; ++s) {
                CHECK(tr.meniscus(lo + (hi - lo) * s / 64.0) <= tr.meniscusPeak() + 1e-9);
            }
            CHECK(tr.meniscus(centreT(kQuarter)) ==
                  tr.meniscusPeak());  // peak at the window centre column
        }
    }

    // Refresh-rate agreement.
    void testOpenAgreesAcrossRefreshRates() {
        EdgeTransition a;
        EdgeTransition b;
        a.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        b.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        for (int k = 1; k <= 24; ++k) {  // compare every 1/48 s for 0.5 s
            for (int i = 0; i < 5; ++i) {
                a.advance(1.0 / 240.0);  // 240 Hz-equivalent chunks (60 Hz = 4 of these)
            }
            for (int i = 0; i < 3; ++i) {
                b.advance(1.0 / 144.0);
            }
            CHECK(std::abs(centreOf(a).y - centreOf(b).y) < 0.5);
            CHECK(std::abs(a.meniscusPeak() - b.meniscusPeak()) < 0.5);
        }
    }

    // Movement-field feed: summed travel over the open equals the centre's total inward
    // displacement.
    void testOpenTravelSumsToCentreDisplacement() {
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        const Vec2 INWARD = hyprimate::inwardNormal(kQuarter.edge);
        const double START = centreInward(tr);
        double sum = 0.0;
        for (int i = 0; i < 960 && tr.active(); ++i) {
            tr.advance(kStep);
            const Vec2 t = tr.takeTravel();
            sum += (t.x * INWARD.x) + (t.y * INWARD.y);
        }
        CHECK(std::abs(sum - (0.0 - START)) < 0.5);  // from the start displacement back to rest
    }

    // Close.
    // Substep of the drain from rest at defaults (0.46 s): no liquid before it.
    constexpr int kDrainSubstep = 111;

    void testCloseFromRestDrainsIntoTheEdge() {
        EdgeTransition tr;
        tr.begin(Transition::Close, kQuarter, TransitionParams{}, kStrength);
        CHECK(tr.active() && std::abs(centreOf(tr).x) < 1e-9 && std::abs(centreOf(tr).y) < 1e-9);
        double prev = centreInward(tr);
        for (int i = 0; i < 480 && tr.active(); ++i) {
            tr.advance(kStep);
            if (!tr.active()) {
                break;
            }
            CHECK(centreInward(tr) <= prev + 1e-9);
            prev = centreInward(tr);  // always toward the edge
        }
        CHECK(tr.closed());
        CHECK(tr.meniscusPeak() == 0.0);
    }

    // Shrinking into a bead must not brake to a hover before the dive.
    void testCloseKeepsMovingThroughTheShrink() {
        for (const double MOMENTUM : {0.0, 0.5, 1.0}) {
            EdgeTransition tr;
            tr.begin(Transition::Close,
                kQuarter,
                TransitionParams{.momentum = MOMENTUM},
                kStrength,
                false);
            double previousTravel = 0.0;
            bool continuous = true;
            for (int i = 0; i < 100; ++i) {
                tr.advance(kStep);
                const Vec2 TRAVEL = tr.takeTravel();
                const Vec2 INWARD = hyprimate::inwardNormal(kQuarter.edge);
                const double OUTWARD = -((TRAVEL.x * INWARD.x) + (TRAVEL.y * INWARD.y));
                if (i >= 24) {
                    continuous = continuous && OUTWARD > 0.0 && OUTWARD >= 0.95 * previousTravel;
                }
                previousTravel = OUTWARD;
            }
            CHECK(continuous);
        }
    }

    void testCloseContinuesAnInterruptedOpen() {
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        for (int i = 0; i < 30; ++i) {
            tr.advance(kStep);
        }
        const Vec2 BEFORE = centreOf(tr);
        const double PEAK_BEFORE = tr.meniscusPeak();
        const auto OTHER = frameFor({.x = 24, .y = 24, .w = 900, .h = 500},
            hyprimate::Edge::Left,
            true);  // different edge: ignored
        tr.begin(Transition::Close, OTHER, TransitionParams{}, kStrength);
        CHECK(tr.frame().edge == hyprimate::Edge::Bottom);
        CHECK(std::abs(centreOf(tr).x - BEFORE.x) < 1e-9 &&
              std::abs(centreOf(tr).y - BEFORE.y) < 1e-9);
        CHECK(std::abs(tr.meniscusPeak() - PEAK_BEFORE) < 1e-9);  // Open->Close begin is continuous
        for (int i = 0; i < 480 && tr.active(); ++i) {
            tr.advance(kStep);
        }
        CHECK(tr.closed());
    }

    // A close from rest shows no liquid until the drain (0.46 s at default elasticity = substep
    // 111).
    void testCloseFromRestHasNoLiquidBeforeDrain() {
        constexpr int K_DRAIN_STEP = 111;
        EdgeTransition tr;
        tr.begin(Transition::Close, kQuarter, TransitionParams{}, kStrength);
        for (int i = 1; i < K_DRAIN_STEP && tr.active(); ++i) {
            tr.advance(kStep);
            CHECK(tr.meniscusPeak() == 0.0);
        }
    }

    // Close momentum changes the dive even when no opening was interrupted.
    void testCloseMomentumChangesDive() {
        EdgeTransition low;
        EdgeTransition high;
        low.begin(Transition::Close, kQuarter, TransitionParams{.momentum = 0.0}, kStrength, false);
        high.begin(
            Transition::Close, kQuarter, TransitionParams{.momentum = 1.0}, kStrength, false);
        for (int i = 0; i < 90; ++i) {
            low.advance(kStep);
            high.advance(kStep);
        }
        const double LOW_NORMAL =
            hyprimate::edgeNormal(kQuarter, low.map({.x = 0, .y = 0}, kCapture));
        const double HIGH_NORMAL =
            hyprimate::edgeNormal(kQuarter, high.map({.x = 0, .y = 0}, kCapture));
        CHECK(HIGH_NORMAL < LOW_NORMAL);
    }

    // A window whose real box follows the pop-out must sit exactly where the outline map puts the
    // capture's extremes, on every step, and be the rest box once the open has settled.
    void testBeadBoxMatchesMap() {
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        double worst = 0.0;
        for (int i = 0; i < 960 && tr.active(); ++i) {
            tr.advance(kStep);
            if (!tr.active()) {
                break;
            }
            const Vec2 c = tr.beadCentre();
            const Vec2 h = tr.beadHalf(kCapture);
            const Vec2 TOP = tr.map({.x = 0.0, .y = -0.5 * kCapture.y}, kCapture);
            const Vec2 BOTTOM = tr.map({.x = 0.0, .y = 0.5 * kCapture.y}, kCapture);
            const Vec2 LEFT = tr.map({.x = -0.5 * kCapture.x, .y = 0.0}, kCapture);
            const Vec2 RIGHT = tr.map({.x = 0.5 * kCapture.x, .y = 0.0}, kCapture);
            worst = std::max({worst,
                std::abs(TOP.y - (c.y - h.y)),
                std::abs(BOTTOM.y - (c.y + h.y)),
                std::abs(LEFT.x - (c.x - h.x)),
                std::abs(RIGHT.x - (c.x + h.x))});
        }
        CHECK(worst < 1e-6);
        const Vec2 c = tr.beadCentre();
        const Vec2 h = tr.beadHalf(kCapture);
        CHECK(c.x == 0.0 && c.y == 0.0);
        CHECK(h.x == 0.5 * kCapture.x && h.y == 0.5 * kCapture.y);
    }

    // Movement-field guard: idle and settled meshes give 1; a moving mesh gives a power-of-two gain
    // in [0, 1].
    void testDisplacementGainGuard() {
        EdgeTransition idle;
        LiquidMesh fresh;
        CHECK(fresh.settled());
        CHECK(fitGain(fresh, idle) == 1.0);

        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        CHECK(fitGain(fresh, tr) == 1.0);  // settled mesh: no gain reduction

        LiquidMesh moving;
        moving.recordTranslation({.x = 40.0, .y = 0.0}, kCapture, kStrength);
        for (int i = 0; i < 24; ++i) {
            moving.advance(kStep, kCapture);
        }
        const double g = fitGain(moving, tr);
        CHECK(g >= 0.0 && g <= 1.0);
        if (g > 0.0) {
            CHECK(std::abs(std::log2(g) - std::round(std::log2(g))) < 1e-9);  // halvings of 1
        }
    }

    // Render mesh the renderer draws at displacement gain g, filled through the same LiquidPose
    // path. Output is in logical px (unit output device), so triangle-area ratios equal those in
    // device px.
    std::vector<hyprimate::LiquidVertex> renderMesh(
        const EdgeTransition &tr, const LiquidMesh &mesh, double g, Vec2 size, Vec2 c) {
        hyprimate::LiquidPose pose{.mesh = mesh, .transition = tr, .compensation = c};
        pose.displacementGain = g;
        hyprimate::LiquidPoseGrid grid;
        std::vector<hyprimate::LiquidVertex> out;
        const hyprimate::Box LOGICAL{.x = 0.0, .y = 0.0, .w = size.x, .h = size.y};
        hyprimate::fillLiquidRenderMesh(pose,
            LOGICAL,
            1.0,
            hyprimate::Box{.x = 0.0, .y = 0.0, .w = 1.0, .h = 1.0},
            Vec2{.x = 0.0, .y = 0.0},
            size,
            grid,
            out);
        return out;
    }

    // Smallest triangle-area ratio (moved / gain-0) over the emitted render mesh. A ratio under 0.2
    // is a fold that pinches the window.
    double minRenderRatio(
        const EdgeTransition &tr, const LiquidMesh &mesh, double g, Vec2 size, Vec2 c) {
        const auto BASE = renderMesh(tr, mesh, 0.0, size, c);
        const auto MOVED = renderMesh(tr, mesh, g, size, c);
        const auto AREA = [](const hyprimate::LiquidVertex &a,
                              const hyprimate::LiquidVertex &b,
                              const hyprimate::LiquidVertex &q) {
            return ((static_cast<double>(b.x) - a.x) * (static_cast<double>(q.y) - a.y)) -
                   ((static_cast<double>(b.y) - a.y) * (static_cast<double>(q.x) - a.x));
        };
        double worst = 1.0;
        for (std::size_t t = 0; t + 2 < BASE.size();
            t += 3) {  // six vertices per cell: two triangles of three
            const double REF = AREA(BASE[t], BASE[t + 1], BASE[t + 2]);
            if (std::abs(REF) < 1e-9) {
                continue;  // degenerate at rest: nothing to fold
            }
            worst = std::min(worst, AREA(MOVED[t], MOVED[t + 1], MOVED[t + 2]) / REF);
        }
        return worst;
    }

    // Regression for the renderer-order fold: the gain must keep every triangle >= 20% of its
    // gain-0 area at any open scale.
    void testDisplacementGainKeepsTrianglesWhenCompensated() {
        LiquidMesh mesh;
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        for (int i = 0; i < 60 && tr.active(); ++i) {
            tr.advance(kStep);  // mid-grow
        }
        for (int i = 0; i < 12; ++i) {
            mesh.recordTranslation({.x = -60.0, .y = 0.0}, kCapture, kStrength);
            mesh.advance(kStep, kCapture);
        }
        for (const double c : {1.0, 1.5, 2.0}) {
            const Vec2 COMP{.x = c, .y = c};
            const double g = fitGain(mesh, tr, COMP);
            CHECK(g < 1.0);
            CHECK(minRenderRatio(tr, mesh, g, kCapture, COMP) >= 0.2 - 1e-9);
        }
    }

    // Closing from rest returns the dive's final travel when it reaches Closed; nothing is returned
    // afterwards.
    void testCloseReturnsFinalTravel() {
        EdgeTransition tr;
        tr.begin(Transition::Close, kQuarter, TransitionParams{}, kStrength, false);
        const Vec2 INWARD = hyprimate::inwardNormal(kQuarter.edge);
        bool sawClosed = false;
        double closingTravel = 0.0;
        for (int i = 0; i < 480 && tr.active(); ++i) {
            tr.advance(kStep);
            const bool REACHED = tr.closed();
            const Vec2 t = tr.takeTravel();
            if (REACHED && !sawClosed) {
                sawClosed = true;
                closingTravel = (t.x * INWARD.x) + (t.y * INWARD.y);
            }
        }
        CHECK(sawClosed);
        CHECK(std::abs(closingTravel) > 1e-3);
        const Vec2 AFTER = tr.takeTravel();
        CHECK(AFTER.x == 0.0 && AFTER.y == 0.0);
    }

    // Close begun on the same substep as an open: the open's neck is still hidden, so there is no
    // liquid until the drain.
    void testCloseAfterOpenBeginHasNoLiquid() {
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        tr.begin(Transition::Close, kQuarter, TransitionParams{}, kStrength);
        CHECK(tr.meniscusPeak() == 0.0);
        for (int i = 1; i < kDrainSubstep && tr.active(); ++i) {
            tr.advance(kStep);
            CHECK(tr.meniscusPeak() == 0.0);
        }
    }

    constexpr double kBeadRadius = 0.14 * 508.0;  // kQuarter bead radius (px)

    // Local maxima of the meniscus above 5% of `peak`, sampled every 0.02 r across +-2 r of the
    // window centre `c`.
    std::vector<double> lobeCentres(const EdgeTransition &tr, double c, double peak) {
        constexpr int K_HALF = 100;
        std::vector<double> v((2 * K_HALF) + 1);
        for (int k = -K_HALF; k <= K_HALF; ++k) {
            v[k + K_HALF] = tr.meniscus(c + (k * 0.02 * kBeadRadius));
        }
        std::vector<double> centres;
        for (int k = 1; k + 1 < static_cast<int>(v.size()); ++k) {
            if (v[k] > 0.05 * peak && v[k] > v[k - 1] && v[k] >= v[k + 1]) {
                centres.push_back(c + ((k - K_HALF) * 0.02 * kBeadRadius));
            }
        }
        return centres;
    }

    // After the drain the splash rises as a single bump centred where the window went under (no
    // side ripples), then returns to exactly zero and the transition is closed.
    void testCloseSplashAfterDrain() {
        EdgeTransition tr;
        tr.begin(Transition::Close, kQuarter, TransitionParams{}, kStrength);
        const double c = centreT(kQuarter);
        double splashPeak = 0.0;
        double prevPeak = 0.0;
        double prevNear = nearSide(tr);
        int firstSplash = -1;
        int lastSplash = -1;
        bool oneCentredLobe = false;
        for (int i = 1; i <= 600 && tr.active(); ++i) {
            tr.advance(kStep);
            const double PEAK = tr.meniscusPeak();
            const double n = nearSide(tr);
            CHECK(std::abs(PEAK - prevPeak) <= std::abs(n - prevNear) + 8.0);
            prevPeak = PEAK;
            prevNear = n;
            if (PEAK > 0.0) {
                if (firstSplash < 0) {
                    firstSplash = i;
                }
                lastSplash = i;
            }
            if (PEAK > splashPeak) {
                splashPeak = PEAK;
                const auto LOBES = lobeCentres(tr, c, PEAK);
                oneCentredLobe = LOBES.size() == 1 && std::abs(LOBES[0] - c) < 0.1 * kBeadRadius;
            }
        }
        CHECK(splashPeak > 0.3 * kBeadRadius);
        CHECK(oneCentredLobe);
        CHECK(firstSplash >= kDrainSubstep);
        CHECK(lastSplash - firstSplash >= 95 &&
              lastSplash - firstSplash <= 110);  // 0.42 s = ~101 substeps
        CHECK(tr.meniscusPeak() == 0.0);
        CHECK(tr.closed());
    }

    // closeSplash = false: no liquid at any time, and the transition is closed by the drain.
    void testCloseSplashOffLeavesNoLiquid() {
        EdgeTransition tr;
        tr.begin(Transition::Close, kQuarter, TransitionParams{}, kStrength, false);
        int closedAt = -1;
        for (int i = 1; i <= 480 && tr.active(); ++i) {
            tr.advance(kStep);
            CHECK(tr.meniscusPeak() == 0.0);
            if (tr.closed() && closedAt < 0) {
                closedAt = i;
            }
        }
        CHECK(closedAt == kDrainSubstep);
    }

    // A close interrupting an opening with a visible neck: the liquid snaps into the recoil within
    // the continuity tolerance, decays away, and only then does the splash rise.
    void testCloseInterruptingOpenNeckDecaysThenSplashes() {
        EdgeTransition tr;
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        for (int i = 0; i < 240 && tr.active() && tr.meniscusPeak() < 5.0; ++i) {
            tr.advance(kStep);
        }
        CHECK(tr.meniscusPeak() >= 5.0);
        tr.begin(Transition::Close, kQuarter, TransitionParams{}, kStrength);
        double prevPeak = tr.meniscusPeak();
        double prevNear = nearSide(tr);
        bool sawQuiet = false;
        bool splashAfterQuiet = false;
        for (int i = 0; i < 960 && tr.active(); ++i) {
            tr.advance(kStep);
            const double PEAK = tr.meniscusPeak();
            const double n = nearSide(tr);
            CHECK(std::abs(PEAK - prevPeak) <= std::abs(n - prevNear) + 8.0);
            prevPeak = PEAK;
            prevNear = n;
            if (PEAK <= 0.5) {
                sawQuiet = true;
            } else if (sawQuiet && PEAK > 0.3 * kBeadRadius) {
                splashAfterQuiet = true;
            }
        }
        CHECK(sawQuiet && splashAfterQuiet);
        CHECK(tr.closed());
    }

    // The splash agrees between 60 Hz and 144 Hz frame clocks within 0.5 px, sampled every 1/12 s.
    void testCloseSplashAgreesAcrossRefreshRates() {
        EdgeTransition a;
        EdgeTransition b;
        a.begin(Transition::Close, kQuarter, TransitionParams{}, kStrength);
        b.begin(Transition::Close, kQuarter, TransitionParams{}, kStrength);
        for (int k = 1; k <= 12; ++k) {
            for (int i = 0; i < 5; ++i) {
                a.advance(1.0 / 60.0);
            }
            for (int i = 0; i < 12; ++i) {
                b.advance(1.0 / 144.0);
            }
            CHECK(std::abs(a.meniscusPeak() - b.meniscusPeak()) < 0.5);
        }
    }

    // Invalid input.
    void testInvalidInputs() {
        constexpr double K_NA_N = std::numeric_limits<double>::quiet_NaN();
        EdgeTransition tr;
        tr.begin(Transition::Open, EdgeFrame{}, TransitionParams{}, kStrength);
        CHECK(!tr.active());
        tr.begin(Transition::Open, kQuarter, TransitionParams{.elasticity = K_NA_N}, kStrength);
        CHECK(!tr.active());
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, 0.0);
        CHECK(!tr.active());
        tr.begin(Transition::Open, kQuarter, TransitionParams{}, kStrength);
        tr.advance(K_NA_N);
        CHECK(!tr.active() && !tr.closed());
        CHECK(tr.map({0.0, 0.0}, kCapture).x == 0.0 && tr.map({0.0, 0.0}, kCapture).y == 0.0);
        tr.begin(Transition::Close, kQuarter, TransitionParams{}, kStrength);
        tr.advance(0.3);
        CHECK(tr.closed());
        tr.begin(Transition::Close, EdgeFrame{}, TransitionParams{}, kStrength);
        CHECK(!tr.active() && !tr.closed());
    }

}  // namespace

int main() {
    testEdgeFollowsDwindleSplits();
    testEdgeTiesAndValidity();
    testFrameGeometry();
    testOriginEdgeWhenValidElseNearest();
    testDwindleContactKeepsDwindleEdges();
    testScrollingColumnsEmergeAwayFromNeighbours();
    testInvalidContactCandidateFallsBackToNearest();
    testOnlyTouchingNeighboursCount();
    testSharedCloseDirectionPolicy();
    testCloseMonitorAndWorkAreaBounds();
    testOpenStartsHiddenAndSettlesExactly();
    testOpenFillsLiveCaptureAlongNormal();
    testBeadMapDoesNotFold();
    testOpenEdgesPointTheRightWay();
    testOpenNeckAttachesSnapsOnceAndRecoils();
    testBeadBoxMatchesMap();
    testMeniscusShapeBounds();
    testOpenAgreesAcrossRefreshRates();
    testOpenTravelSumsToCentreDisplacement();
    testCloseFromRestDrainsIntoTheEdge();
    testCloseKeepsMovingThroughTheShrink();
    testCloseContinuesAnInterruptedOpen();
    testCloseFromRestHasNoLiquidBeforeDrain();
    testCloseMomentumChangesDive();
    testDisplacementGainGuard();
    testDisplacementGainKeepsTrianglesWhenCompensated();
    testCloseReturnsFinalTravel();
    testCloseAfterOpenBeginHasNoLiquid();
    testCloseSplashAfterDrain();
    testCloseSplashOffLeavesNoLiquid();
    testCloseInterruptingOpenNeckDecaysThenSplashes();
    testCloseSplashAgreesAcrossRefreshRates();
    testInvalidInputs();
    return g_failures == 0 ? 0 : 1;
}
