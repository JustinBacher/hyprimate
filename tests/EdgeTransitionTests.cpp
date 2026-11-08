#include "EdgeTransition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

using liquid_glass::MaterialParams;
using liquid_glass::Transition;
using liquid_glass::Vec2;

int g_failures = 0;

void report(bool ok, const char* expression, const char* file, int line) {
    if (ok)
        return;
    ++g_failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}

#define CHECK(expr) report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

constexpr double kStrength = 0.65;

// Task 2: edge selection.
constexpr liquid_glass::Box kScreen{0.0, 0.0, 1920.0, 1080.0};
liquid_glass::Box withShadow(liquid_glass::Box b) { return {b.x - 12.0, b.y - 12.0, b.w + 24.0, b.h + 24.0}; }
std::optional<liquid_glass::Edge> pick(liquid_glass::Box body, liquid_glass::Edge fallback, bool first,
                                       std::span<const liquid_glass::Box> neighbours = {}, liquid_glass::Box screen = kScreen) {
    const auto f = liquid_glass::resolveEdgeFrame(body, withShadow(body), screen, neighbours, fallback, first);
    return f ? std::optional{f->edge} : std::nullopt;
}

void testEdgeFollowsDwindleSplits() {
    using liquid_glass::Edge;
    CHECK(pick({24, 24, 1872, 1032}, Edge::Bottom, true) == Edge::Bottom);   // first window: configured side
    CHECK(pick({24, 24, 1872, 1032}, Edge::Left, true) == Edge::Left);
    CHECK(pick({972, 24, 924, 1032}, Edge::Bottom, false) == Edge::Right);   // right half
    CHECK(pick({972, 548, 924, 508}, Edge::Bottom, false) == Edge::Bottom);  // bottom-right quarter
    CHECK(pick({1438, 548, 458, 508}, Edge::Bottom, false) == Edge::Right);  // right eighth
}

void testEdgeTiesAndValidity() {
    using liquid_glass::Edge;
    CHECK(pick({760, 340, 400, 400}, Edge::Top, false) == Edge::Top);        // top/bottom tie -> fallback
    CHECK(pick({760, 340, 400, 400}, Edge::Right, false) == Edge::Bottom);   // fallback not tied -> order
    CHECK(pick({500, 900, 400, 300}, Edge::Bottom, true) == Edge::Left);     // fallback invalid (crosses bottom)
    CHECK(!pick({-10, -10, 1940, 1100}, Edge::Bottom, false));               // crosses every edge
    CHECK(!pick({0, 0, 0, 100}, Edge::Bottom, false));                       // unusable
}

void testFrameGeometry() {
    const liquid_glass::Box body{972, 548, 924, 508};
    const auto f = liquid_glass::resolveEdgeFrame(body, withShadow(body), kScreen, {}, liquid_glass::Edge::Bottom, false);
    CHECK(f && f->edge == liquid_glass::Edge::Bottom);
    // Capture centre (1434, 802): 278 px above the bottom edge; capture half height 266.
    CHECK(std::abs(f->edgeDistance - 278.0) < 1e-9 && std::abs(f->captureFar - 544.0) < 1e-9);
    CHECK(std::abs(f->bodyNear - 24.0) < 1e-9 && std::abs(f->bodyFar - 532.0) < 1e-9);
    CHECK(std::abs(f->bodyT0 + 462.0) < 1e-9 && std::abs(f->bodyT1 - 462.0) < 1e-9);
    // The frame maps round-trip, and n grows into the screen.
    const liquid_glass::Vec2 p = liquid_glass::edgePoint(*f, 40.0, 100.0);
    CHECK(std::abs(liquid_glass::edgeNormal(*f, p) - 100.0) < 1e-9 && std::abs(liquid_glass::edgeTangent(*f, p) - 40.0) < 1e-9);
    CHECK(std::abs(p.y - 178.0) < 1e-9); // 278 - 100, y down from the capture centre
}

// close_side = origin: the origin edge wins when valid, otherwise the nearest valid edge.
void testOriginEdgeWhenValidElseNearest() {
    using liquid_glass::Edge;
    CHECK(pick({972, 548, 924, 508}, Edge::Top, true) == Edge::Top);     // body nearest Bottom, origin Top valid
    CHECK(pick({760, 340, 400, 400}, Edge::Left, true) == Edge::Left);   // origin Left valid, Top/Bottom tied
    CHECK(pick({500, 900, 400, 300}, Edge::Bottom, true) == Edge::Left); // origin Bottom invalid -> nearest (Left)
    CHECK(pick({972, 548, 924, 508}, Edge::Right, true) == Edge::Right); // origin Right valid though Bottom is nearer
    CHECK(pick({1438, 24, 458, 300}, Edge::Right, true) == Edge::Right); // origin Right valid (24 px gap)
    CHECK(pick({1000, 24, 300, 300}, Edge::Right, true) == Edge::Right); // origin Right valid: preferred over nearer Top
    CHECK(pick({1000, 24, 300, 300}, Edge::Left, true) == Edge::Left);   // origin Left valid, preferred
    CHECK(pick({-10, -10, 1940, 1100}, Edge::Right, true) == std::nullopt); // origin invalid and no valid edge
}

// Contact rule: a tiled window emerges from the side away from the neighbours it sits beside.
constexpr liquid_glass::Box kWideScreen{0.0, 0.0, 1600.0, 900.0};

void testDwindleContactKeepsDwindleEdges() {
    using liquid_glass::Edge;
    const std::array<liquid_glass::Box, 1> leftHalf{{{24, 24, 924, 1032}}};
    CHECK(pick({972, 24, 924, 1032}, Edge::Bottom, false, leftHalf) == Edge::Right);       // right half beside left half
    const std::array<liquid_glass::Box, 1> topRight{{{972, 24, 924, 508}}};
    CHECK(pick({972, 548, 924, 508}, Edge::Bottom, false, topRight) == Edge::Bottom);      // bottom-right quarter below top-right
    const std::array<liquid_glass::Box, 1> bottomRight{{{972, 548, 924, 508}}};
    CHECK(pick({972, 24, 924, 508}, Edge::Bottom, false, bottomRight) == Edge::Top);       // top-right quarter above bottom-right
    const std::array<liquid_glass::Box, 1> sibling{{{972, 548, 458, 508}}};
    CHECK(pick({1438, 548, 458, 508}, Edge::Bottom, false, sibling) == Edge::Right);       // right eighth beside its sibling
}

void testScrollingColumnsEmergeAwayFromNeighbours() {
    using liquid_glass::Edge;
    // 1600x900 monitor, full-height columns: gaps_out top 15, bottom 30, sides 30; gaps_in 12.
    const std::array<liquid_glass::Box, 1> left{{{30, 15, 700, 855}}};
    CHECK(pick({742, 15, 800, 855}, Edge::Bottom, false, left, kWideScreen) == Edge::Right);    // new column right of the left column
    const std::array<liquid_glass::Box, 1> right{{{870, 15, 700, 855}}};
    CHECK(pick({58, 15, 800, 855}, Edge::Bottom, false, right, kWideScreen) == Edge::Left);     // new column left of the right column
    const std::array<liquid_glass::Box, 2> around{{{30, 15, 400, 855}, {1060, 15, 510, 855}}};
    CHECK(pick({442, 15, 600, 855}, Edge::Bottom, false, around, kWideScreen) == Edge::Left);   // between two columns, nearer Left
    const std::array<liquid_glass::Box, 2> around2{{{30, 15, 600, 855}, {1000, 15, 570, 855}}};
    CHECK(pick({640, 15, 360, 855}, Edge::Bottom, false, around2, kWideScreen) == Edge::Right); // between two columns, nearer Right
    CHECK(pick({742, 15, 800, 855}, Edge::Bottom, false, {}, kWideScreen) == Edge::Top);        // no neighbours: nearest edge
    CHECK(pick({742, 15, 800, 855}, Edge::Top, true, left, kWideScreen) == Edge::Top);          // first window: side beats contact
}

void testInvalidContactCandidateFallsBackToNearest() {
    using liquid_glass::Edge;
    // The body crosses the right screen edge, so the Right candidate is invalid; the nearest valid edge is Top.
    const std::array<liquid_glass::Box, 1> left{{{600, 24, 380, 400}}};
    CHECK(pick({1000, 24, 1000, 400}, Edge::Bottom, false, left) == Edge::Top);
    CHECK(pick({1000, 24, 1000, 400}, Edge::Bottom, false) == Edge::Top);
}

void testOnlyTouchingNeighboursCount() {
    using liquid_glass::Edge;
    const liquid_glass::Box                   body{972, 548, 924, 508};
    const std::array<liquid_glass::Box, 1> gap64{{{600, 548, 308, 508}}};        // facing gap 64 px: counts
    CHECK(pick(body, Edge::Bottom, false, gap64) == Edge::Right);
    const std::array<liquid_glass::Box, 1> gap100{{{600, 548, 272, 508}}};       // facing gap 100 px: ignored
    CHECK(pick(body, Edge::Bottom, false, gap100) == Edge::Bottom);
    const std::array<liquid_glass::Box, 1> aside{{{600, 24, 360, 400}}};         // 12 px gap, no vertical overlap: ignored
    CHECK(pick(body, Edge::Bottom, false, aside) == Edge::Bottom);
    const std::array<liquid_glass::Box, 1> overlapping{{{1200, 300, 600, 600}}}; // floating-like overlap: ignored
    CHECK(pick(body, Edge::Bottom, false, overlapping) == Edge::Bottom);
}

// Bead transitions.
using liquid_glass::EdgeFrame;
using liquid_glass::EdgeTransition;
using liquid_glass::LiquidMesh;
constexpr double kStep = 1.0 / 240.0;

EdgeFrame frameFor(liquid_glass::Box body, liquid_glass::Edge fallback = liquid_glass::Edge::Bottom, bool first = false) {
    return *liquid_glass::resolveEdgeFrame(body, withShadow(body), kScreen, {}, fallback, first);
}
const EdgeFrame kQuarter = frameFor({972, 548, 924, 508}); // Bottom edge
// Capture (decorated + shadow) size of kQuarter: 948 x 532, half extents 474 x 266.
constexpr Vec2 kCapture{948.0, 532.0};

// Displayed centre displacement (logical px) from the rest capture centre.
Vec2 centreOf(const EdgeTransition& tr) { return tr.map({0.0, 0.0}, kCapture); }

// Centre displacement into the screen along the edge normal (0 at rest, negative toward and behind the edge).
double centreInward(const EdgeTransition& tr) {
    const Vec2 c = centreOf(tr), in = liquid_glass::inwardNormal(tr.frame().edge);
    return c.x * in.x + c.y * in.y;
}

// Displayed n (into the screen) of the body near side at the body's tangential centre.
double nearSide(const EdgeTransition& tr) {
    const EdgeFrame& f = tr.frame();
    const Vec2 p = liquid_glass::edgePoint(f, 0.5 * (f.bodyT0 + f.bodyT1), f.bodyNear);
    return liquid_glass::edgeNormal(f, tr.map(p, kCapture));
}
double centreT(const EdgeFrame& f) { return 0.5 * (f.bodyT0 + f.bodyT1); }

void testOpenStartsHiddenAndSettlesExactly() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    CHECK(tr.active());
    // Hidden start: every capture corner is on the far side of the edge (n <= 0).
    for (double sx : {-1.0, 1.0})
        for (double sy : {-1.0, 1.0})
            CHECK(liquid_glass::edgeNormal(kQuarter, tr.map({sx * 474.0, sy * 266.0}, kCapture)) <= 1e-6);
    const Vec2 first = tr.takeTravel();
    CHECK(first.x == 0.0 && first.y == 0.0);                                  // begin is not travel
    double maxInward = -1e9;
    for (int i = 0; i < 960 && tr.active(); ++i) { tr.advance(kStep); maxInward = std::max(maxInward, centreInward(tr)); }
    CHECK(!tr.active());
    // Exact identity once settled.
    const Vec2 c = centreOf(tr);
    CHECK(c.x == 0.0 && c.y == 0.0);
    CHECK(tr.map({123.0, -45.0}, kCapture).x == 123.0 && tr.map({123.0, -45.0}, kCapture).y == -45.0);
    CHECK(maxInward > 5.0);                                                    // visible pop overshoot
}

// The frame's capture is taken when the open begins, before the shadow reports its extents; the live capture is
// larger. Once the spring is nearly settled, the window must fill the live capture along the normal, not the frame's.
void testOpenFillsLiveCaptureAlongNormal() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    constexpr Vec2 kLive{kCapture.x, kCapture.y + 24.0}; // Bottom edge: the normal is vertical
    double         worst   = 0.0;
    int            checked = 0;
    for (int i = 0; i < 960 && tr.active(); ++i) {
        tr.advance(kStep);
        if (i < 240 || !tr.active())
            continue; // from 1 s of 240 Hz steps the spring is within a fraction of a pixel
        const double far  = liquid_glass::edgeNormal(kQuarter, tr.map({0.0, -0.5 * kLive.y}, kLive));
        const double near = liquid_glass::edgeNormal(kQuarter, tr.map({0.0, 0.5 * kLive.y}, kLive));
        worst             = std::max(worst, std::abs((far - near) - kLive.y));
        ++checked;
    }
    CHECK(checked > 0);
    CHECK(worst < 1.0);
}

// The map never folds the image: every cell of a 33x33 grid keeps its positive orientation during the open.
void testBeadMapDoesNotFold() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    constexpr int kSide = 33;
    for (int frame = 0; frame < 240 && tr.active(); ++frame) {
        tr.advance(kStep);
        if (!tr.active())
            break;
        std::array<Vec2, kSide * kSide> mapped{};
        for (int j = 0; j < kSide; ++j)
            for (int i = 0; i < kSide; ++i) {
                const double u = static_cast<double>(i) / (kSide - 1), v = static_cast<double>(j) / (kSide - 1);
                mapped[j * kSide + i] = tr.map({(u - 0.5) * kCapture.x, (v - 0.5) * kCapture.y}, kCapture);
            }
        for (int j = 0; j + 1 < kSide; ++j)
            for (int i = 0; i + 1 < kSide; ++i) {
                const Vec2 a = mapped[j * kSide + i], b = mapped[j * kSide + i + 1], c = mapped[(j + 1) * kSide + i];
                const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                CHECK(area > 0.0);
            }
    }
}

void testOpenEdgesPointTheRightWay() {
    using liquid_glass::Edge;
    const std::array<std::pair<Edge, Vec2>, 4> cases{{{Edge::Bottom, {0, 1}}, {Edge::Top, {0, -1}}, {Edge::Left, {-1, 0}}, {Edge::Right, {1, 0}}}};
    for (const auto& [edge, sign] : cases) {
        EdgeTransition tr;
        tr.begin(Transition::Open, frameFor({24, 24, 1872, 1032}, edge, true), MaterialParams{}, kStrength);
        const Vec2 o = centreOf(tr);
        CHECK(o.x * sign.x + o.y * sign.y > 200.0); // starts hidden beyond that edge
    }
}


// Meniscus during open: the neck attaches, snaps once, and recoils.
void testOpenNeckAttachesSnapsOnceAndRecoils() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    int    snaps = 0;
    bool   tucked = false, sawZeroAfterSnap = false, secondLobe = false;
    double prevPeak = tr.meniscusPeak(), prevNear = nearSide(tr);
    for (int i = 0; i < 480 && tr.active(); ++i) {
        tr.advance(kStep);
        const double peak = tr.meniscusPeak(), n = nearSide(tr);
        CHECK(peak >= 0.0);
        // Tolerance 8 px: the recoil spring legitimately moves ~5 px per substep. Snaps to or from zero (>= Fs+Lp ~ 44 px) still fail.
        CHECK(std::abs(peak - prevPeak) <= std::abs(n - prevNear) + 8.0);       // continuous
        if (n >= 0.0 && peak > n + 1.0) tucked = true;                            // tip under the window
        if (tucked && peak < n - 1.0 && snaps == 0) ++snaps;                      // gap opened: snapped
        else if (snaps == 1 && peak > n + 1.0) ++snaps;                           // must never re-attach
        if (snaps == 1 && peak == 0.0) sawZeroAfterSnap = true;
        if (sawZeroAfterSnap && peak > 0.5) secondLobe = true;                    // the bar's small rebound bump
        prevPeak = peak; prevNear = n;
    }
    CHECK(tucked);
    CHECK(snaps == 1);
    CHECK(secondLobe);
    CHECK(tr.meniscusPeak() == 0.0);
}

void testMeniscusShapeBounds() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    for (int i = 0; i < 240 && tr.active(); ++i) {
        tr.advance(kStep);
        const double lo = tr.meniscusLow(), hi = tr.meniscusHigh();
        CHECK(lo <= hi);
        CHECK(tr.meniscus(lo - 1.0) == 0.0 && tr.meniscus(hi + 1.0) == 0.0);
        for (int s = 0; s <= 64; ++s)
            CHECK(tr.meniscus(lo + (hi - lo) * s / 64.0) <= tr.meniscusPeak() + 1e-9);
        CHECK(tr.meniscus(centreT(kQuarter)) == tr.meniscusPeak());               // peak at the window centre column
    }
}

// Refresh-rate agreement.
void testOpenAgreesAcrossRefreshRates() {
    EdgeTransition a, b;
    a.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    b.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    for (int k = 1; k <= 24; ++k) {                       // compare every 1/48 s for 0.5 s
        for (int i = 0; i < 5; ++i) a.advance(1.0 / 240.0); // 240 Hz-equivalent chunks (60 Hz = 4 of these)
        for (int i = 0; i < 3; ++i) b.advance(1.0 / 144.0);
        CHECK(std::abs(centreOf(a).y - centreOf(b).y) < 0.5);
        CHECK(std::abs(a.meniscusPeak() - b.meniscusPeak()) < 0.5);
    }
}

// Movement-field feed: summed travel over the open equals the centre's total inward displacement.
void testOpenTravelSumsToCentreDisplacement() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    const Vec2 inward = liquid_glass::inwardNormal(kQuarter.edge);
    const double start = centreInward(tr);
    double sum = 0.0;
    for (int i = 0; i < 960 && tr.active(); ++i) {
        tr.advance(kStep);
        const Vec2 t = tr.takeTravel();
        sum += t.x * inward.x + t.y * inward.y;
    }
    CHECK(std::abs(sum - (0.0 - start)) < 0.5); // from the start displacement back to rest
}

// Close.
// Substep of the drain from rest at defaults (0.46 s): no liquid before it.
constexpr int kDrainSubstep = 111;

void testCloseFromRestDrainsIntoTheEdge() {
    EdgeTransition tr;
    tr.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength);
    CHECK(tr.active() && std::abs(centreOf(tr).x) < 1e-9 && std::abs(centreOf(tr).y) < 1e-9);
    double prev = centreInward(tr);
    for (int i = 0; i < 480 && tr.active(); ++i) {
        tr.advance(kStep);
        if (!tr.active()) break;
        CHECK(centreInward(tr) <= prev + 1e-9); prev = centreInward(tr); // always toward the edge
    }
    CHECK(tr.closed());
    CHECK(tr.meniscusPeak() == 0.0);
}

// Shrinking into a bead must not brake to a hover before the dive.
void testCloseKeepsMovingThroughTheShrink() {
    for (double momentum : {0.0, 0.5, 1.0}) {
        EdgeTransition tr;
        tr.begin(Transition::Close, kQuarter, MaterialParams{.momentum = momentum}, kStrength, false);
        double previousTravel = 0.0;
        bool continuous = true;
        for (int i = 0; i < 100; ++i) {
            tr.advance(kStep);
            const Vec2 travel = tr.takeTravel();
            const Vec2 inward = liquid_glass::inwardNormal(kQuarter.edge);
            const double outward = -(travel.x * inward.x + travel.y * inward.y);
            if (i >= 24)
                continuous = continuous && outward > 0.0 && outward >= 0.95 * previousTravel;
            previousTravel = outward;
        }
        CHECK(continuous);
    }
}

void testCloseContinuesAnInterruptedOpen() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    for (int i = 0; i < 30; ++i) tr.advance(kStep);
    const Vec2 before = centreOf(tr);
    const double peakBefore = tr.meniscusPeak();
    const auto other = frameFor({24, 24, 900, 500}, liquid_glass::Edge::Left, true); // different edge: ignored
    tr.begin(Transition::Close, other, MaterialParams{}, kStrength);
    CHECK(tr.frame().edge == liquid_glass::Edge::Bottom);
    CHECK(std::abs(centreOf(tr).x - before.x) < 1e-9 && std::abs(centreOf(tr).y - before.y) < 1e-9);
    CHECK(std::abs(tr.meniscusPeak() - peakBefore) < 1e-9); // Open->Close begin is continuous
    for (int i = 0; i < 480 && tr.active(); ++i) tr.advance(kStep);
    CHECK(tr.closed());
}



// A close from rest shows no liquid until the drain (0.46 s at default elasticity = substep 111).
void testCloseFromRestHasNoLiquidBeforeDrain() {
    constexpr int kDrainStep = 111;
    EdgeTransition tr;
    tr.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength);
    for (int i = 1; i < kDrainStep && tr.active(); ++i) {
        tr.advance(kStep);
        CHECK(tr.meniscusPeak() == 0.0);
    }
}

// Close momentum changes the dive even when no opening was interrupted.
void testCloseMomentumChangesDive() {
    EdgeTransition low, high;
    low.begin(Transition::Close, kQuarter, MaterialParams{.momentum = 0.0}, kStrength, false);
    high.begin(Transition::Close, kQuarter, MaterialParams{.momentum = 1.0}, kStrength, false);
    for (int i = 0; i < 90; ++i) {
        low.advance(kStep);
        high.advance(kStep);
    }
    const double lowNormal = liquid_glass::edgeNormal(kQuarter, low.map({0, 0}, kCapture));
    const double highNormal = liquid_glass::edgeNormal(kQuarter, high.map({0, 0}, kCapture));
    CHECK(highNormal < lowNormal);
}

// A window whose real box follows the pop-out must sit exactly where the outline map puts the capture's extremes, on
// every step, and be the rest box once the open has settled.
void testBeadBoxMatchesMap() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    double worst = 0.0;
    for (int i = 0; i < 960 && tr.active(); ++i) {
        tr.advance(kStep);
        if (!tr.active())
            break;
        const Vec2 c = tr.beadCentre();
        const Vec2 h = tr.beadHalf(kCapture);
        const Vec2 top    = tr.map({0.0, -0.5 * kCapture.y}, kCapture);
        const Vec2 bottom = tr.map({0.0, 0.5 * kCapture.y}, kCapture);
        const Vec2 left   = tr.map({-0.5 * kCapture.x, 0.0}, kCapture);
        const Vec2 right  = tr.map({0.5 * kCapture.x, 0.0}, kCapture);
        worst = std::max({worst, std::abs(top.y - (c.y - h.y)), std::abs(bottom.y - (c.y + h.y)), std::abs(left.x - (c.x - h.x)),
                          std::abs(right.x - (c.x + h.x))});
    }
    CHECK(worst < 1e-6);
    const Vec2 c = tr.beadCentre();
    const Vec2 h = tr.beadHalf(kCapture);
    CHECK(c.x == 0.0 && c.y == 0.0);
    CHECK(h.x == 0.5 * kCapture.x && h.y == 0.5 * kCapture.y);
}

// Movement-field guard: idle and settled meshes give 1; a moving mesh gives a power-of-two gain in [0, 1].
void testDisplacementGainGuard() {
    EdgeTransition idle;
    LiquidMesh   fresh;
    CHECK(fresh.settled());
    CHECK(idle.displacementGain(fresh, kCapture, {1.0, 1.0}) == 1.0);

    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    CHECK(tr.displacementGain(fresh, kCapture, {1.0, 1.0}) == 1.0); // settled mesh: no gain reduction

    LiquidMesh moving;
    moving.recordTranslation({40.0, 0.0}, kCapture, kStrength);
    for (int i = 0; i < 24; ++i) moving.advance(kStep, kCapture);
    const double g = tr.displacementGain(moving, kCapture, {1.0, 1.0});
    CHECK(g >= 0.0 && g <= 1.0);
    if (g > 0.0) CHECK(std::abs(std::log2(g) - std::round(std::log2(g))) < 1e-9); // halvings of 1
}

// Smallest triangle-area ratio (moved / gain-0) over the renderer's 32x32 grid, composed exactly as the renderer does:
// map(((u-0.5)*S + g*d) * c, S*c) against map((u-0.5)*S * c, S*c). Independent of the guard implementation.
double minRendererRatio(const EdgeTransition& tr, const LiquidMesh& mesh, double g, Vec2 size, Vec2 c) {
    constexpr int kSide = 33;
    const Vec2 scaled{size.x * c.x, size.y * c.y};
    std::array<Vec2, kSide * kSide> base{}, moved{};
    for (int j = 0; j < kSide; ++j)
        for (int i = 0; i < kSide; ++i) {
            const double u = static_cast<double>(i) / (kSide - 1), v = static_cast<double>(j) / (kSide - 1);
            const auto   d = mesh.sampleDisplacement(u, v);
            const double bx = (u - 0.5) * size.x, by = (v - 0.5) * size.y;
            base[j * kSide + i]  = tr.map({bx * c.x, by * c.y}, scaled);
            moved[j * kSide + i] = tr.map({(bx + g * d.x) * c.x, (by + g * d.y) * c.y}, scaled);
        }
    const auto area = [](Vec2 a, Vec2 b, Vec2 q) { return (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x); };
    double worst = 1.0;
    for (int j = 0; j + 1 < kSide; ++j)
        for (int i = 0; i + 1 < kSide; ++i) {
            const int p00 = j * kSide + i, p10 = p00 + 1, p01 = p00 + kSide, p11 = p01 + 1;
            for (const auto& [a, b, q] : {std::array{p00, p10, p01}, std::array{p10, p11, p01}}) {
                const double ref = area(base[a], base[b], base[q]);
                if (std::abs(ref) < 1e-9) continue; // degenerate at rest: nothing to fold
                worst = std::min(worst, area(moved[a], moved[b], moved[q]) / ref);
            }
        }
    return worst;
}

// Regression for the renderer-order fold: the gain must keep every triangle >= 20% of its gain-0 area at any open scale.
void testDisplacementGainKeepsTrianglesWhenCompensated() {
    LiquidMesh   mesh;
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    for (int i = 0; i < 60 && tr.active(); ++i) tr.advance(kStep); // mid-grow
    for (int i = 0; i < 12; ++i) {
        mesh.recordTranslation({-60.0, 0.0}, kCapture, kStrength);
        mesh.advance(kStep, kCapture);
    }
    for (double c : {1.0, 1.5, 2.0}) {
        const Vec2   comp{c, c};
        const double g = tr.displacementGain(mesh, kCapture, comp);
        CHECK(g < 1.0);
        CHECK(minRendererRatio(tr, mesh, g, kCapture, comp) >= 0.2 - 1e-9);
    }
}


// Closing from rest returns the dive's final travel when it reaches Closed; nothing is returned afterwards.
void testCloseReturnsFinalTravel() {
    EdgeTransition tr;
    tr.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength, false);
    const Vec2 inward = liquid_glass::inwardNormal(kQuarter.edge);
    bool       sawClosed = false;
    double     closingTravel = 0.0;
    for (int i = 0; i < 480 && tr.active(); ++i) {
        tr.advance(kStep);
        const bool reached = tr.closed();
        const Vec2 t = tr.takeTravel();
        if (reached && !sawClosed) {
            sawClosed = true;
            closingTravel = t.x * inward.x + t.y * inward.y;
        }
    }
    CHECK(sawClosed);
    CHECK(std::abs(closingTravel) > 1e-3);
    const Vec2 after = tr.takeTravel();
    CHECK(after.x == 0.0 && after.y == 0.0);
}

// Close begun on the same substep as an open: the open's neck is still hidden, so there is no liquid until the drain.
void testCloseAfterOpenBeginHasNoLiquid() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    tr.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength);
    CHECK(tr.meniscusPeak() == 0.0);
    for (int i = 1; i < kDrainSubstep && tr.active(); ++i) {
        tr.advance(kStep);
        CHECK(tr.meniscusPeak() == 0.0);
    }
}

constexpr double kBeadRadius = 0.14 * 508.0; // kQuarter bead radius (px)

// Local maxima of the meniscus above 5% of `peak`, sampled every 0.02 r across +-2 r of the window centre `c`.
std::vector<double> lobeCentres(const EdgeTransition& tr, double c, double peak) {
    constexpr int kHalf = 100;
    std::vector<double> v(2 * kHalf + 1);
    for (int k = -kHalf; k <= kHalf; ++k)
        v[k + kHalf] = tr.meniscus(c + k * 0.02 * kBeadRadius);
    std::vector<double> centres;
    for (int k = 1; k + 1 < static_cast<int>(v.size()); ++k)
        if (v[k] > 0.05 * peak && v[k] > v[k - 1] && v[k] >= v[k + 1])
            centres.push_back(c + (k - kHalf) * 0.02 * kBeadRadius);
    return centres;
}

// After the drain the splash rises as a single bump centred where the window went under (no side ripples), then
// returns to exactly zero and the transition is closed.
void testCloseSplashAfterDrain() {
    EdgeTransition tr;
    tr.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength);
    const double c = centreT(kQuarter);
    double splashPeak = 0.0, prevPeak = 0.0, prevNear = nearSide(tr);
    int firstSplash = -1, lastSplash = -1;
    bool oneCentredLobe = false;
    for (int i = 1; i <= 600 && tr.active(); ++i) {
        tr.advance(kStep);
        const double peak = tr.meniscusPeak(), n = nearSide(tr);
        CHECK(std::abs(peak - prevPeak) <= std::abs(n - prevNear) + 8.0);
        prevPeak = peak;
        prevNear = n;
        if (peak > 0.0) {
            if (firstSplash < 0) firstSplash = i;
            lastSplash = i;
        }
        if (peak > splashPeak) {
            splashPeak = peak;
            const auto lobes = lobeCentres(tr, c, peak);
            oneCentredLobe = lobes.size() == 1 && std::abs(lobes[0] - c) < 0.1 * kBeadRadius;
        }
    }
    CHECK(splashPeak > 0.3 * kBeadRadius);
    CHECK(oneCentredLobe);
    CHECK(firstSplash >= kDrainSubstep);
    CHECK(lastSplash - firstSplash >= 95 && lastSplash - firstSplash <= 110); // 0.42 s = ~101 substeps
    CHECK(tr.meniscusPeak() == 0.0);
    CHECK(tr.closed());
}

// closeSplash = false: no liquid at any time, and the transition is closed by the drain.
void testCloseSplashOffLeavesNoLiquid() {
    EdgeTransition tr;
    tr.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength, false);
    int closedAt = -1;
    for (int i = 1; i <= 480 && tr.active(); ++i) {
        tr.advance(kStep);
        CHECK(tr.meniscusPeak() == 0.0);
        if (tr.closed() && closedAt < 0)
            closedAt = i;
    }
    CHECK(closedAt == kDrainSubstep);
}

// A close interrupting an opening with a visible neck: the liquid snaps into the recoil within the continuity
// tolerance, decays away, and only then does the splash rise.
void testCloseInterruptingOpenNeckDecaysThenSplashes() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    for (int i = 0; i < 240 && tr.active() && tr.meniscusPeak() < 5.0; ++i)
        tr.advance(kStep);
    CHECK(tr.meniscusPeak() >= 5.0);
    tr.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength);
    double prevPeak = tr.meniscusPeak(), prevNear = nearSide(tr);
    bool   sawQuiet = false, splashAfterQuiet = false;
    for (int i = 0; i < 960 && tr.active(); ++i) {
        tr.advance(kStep);
        const double peak = tr.meniscusPeak(), n = nearSide(tr);
        CHECK(std::abs(peak - prevPeak) <= std::abs(n - prevNear) + 8.0);
        prevPeak = peak;
        prevNear = n;
        if (peak <= 0.5)
            sawQuiet = true;
        else if (sawQuiet && peak > 0.3 * kBeadRadius)
            splashAfterQuiet = true;
    }
    CHECK(sawQuiet && splashAfterQuiet);
    CHECK(tr.closed());
}

// The splash agrees between 60 Hz and 144 Hz frame clocks within 0.5 px, sampled every 1/12 s.
void testCloseSplashAgreesAcrossRefreshRates() {
    EdgeTransition a, b;
    a.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength);
    b.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength);
    for (int k = 1; k <= 12; ++k) {
        for (int i = 0; i < 5; ++i) a.advance(1.0 / 60.0);
        for (int i = 0; i < 12; ++i) b.advance(1.0 / 144.0);
        CHECK(std::abs(a.meniscusPeak() - b.meniscusPeak()) < 0.5);
    }
}

// Invalid input.
void testInvalidInputs() {
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    EdgeTransition tr;
    tr.begin(Transition::Open, EdgeFrame{}, MaterialParams{}, kStrength);           CHECK(!tr.active());
    tr.begin(Transition::Open, kQuarter, MaterialParams{.elasticity = kNaN}, kStrength); CHECK(!tr.active());
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, 0.0);                    CHECK(!tr.active());
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    tr.advance(kNaN);                                                                CHECK(!tr.active() && !tr.closed());
    CHECK(tr.map({0.0, 0.0}, kCapture).x == 0.0 && tr.map({0.0, 0.0}, kCapture).y == 0.0);
    tr.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength);
    tr.advance(0.3);                                                                 CHECK(tr.closed());
    tr.begin(Transition::Close, EdgeFrame{}, MaterialParams{}, kStrength);          CHECK(!tr.active() && !tr.closed());
}

} // namespace

int main() {
    testEdgeFollowsDwindleSplits();
    testEdgeTiesAndValidity();
    testFrameGeometry();
    testOriginEdgeWhenValidElseNearest();
    testDwindleContactKeepsDwindleEdges();
    testScrollingColumnsEmergeAwayFromNeighbours();
    testInvalidContactCandidateFallsBackToNearest();
    testOnlyTouchingNeighboursCount();
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
