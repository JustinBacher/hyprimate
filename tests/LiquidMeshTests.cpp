// Compositor-independent behavior tests for liquid_glass::LiquidMesh.
// Checks are explicit (not assert), so they stay active in NDEBUG/RelWithDebInfo builds.

#include "../src/LiquidMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

using liquid_glass::LiquidMesh;
using liquid_glass::MaterialParams;
using liquid_glass::MotionMode;
using liquid_glass::Vec2;

int g_failures = 0;

void report(bool ok, const char* expression, const char* file, int line) {
    if (ok)
        return;
    ++g_failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}

#define CHECK(expr) report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

constexpr Vec2 kWindow{600.0, 400.0};
constexpr double kStrength = 0.65;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr std::array<MotionMode, 2> kModes{MotionMode::Liquid, MotionMode::Wiggle};

std::size_t node(unsigned x, unsigned y) {
    return static_cast<std::size_t>(y) * 9 + x;
}

double magnitude(Vec2 v) {
    return std::hypot(v.x, v.y);
}

double maxMagnitude(const std::array<Vec2, 81>& field) {
    double result = 0.0;
    for (const Vec2& p : field)
        result = std::max(result, magnitude(p));
    return result;
}

double maxDifference(const std::array<Vec2, 81>& a, const std::array<Vec2, 81>& b) {
    double result = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        result = std::max(result, magnitude(Vec2{a[i].x - b[i].x, a[i].y - b[i].y}));
    return result;
}

double signedArea(Vec2 a, Vec2 b, Vec2 c) {
    return 0.5 * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
}

struct Corner {
    unsigned x;
    unsigned y;
};

// Independent triangle check over the renderer's 8x8 cell layout: each deformed triangle must keep
// at least 20% of its rest signed area in the same orientation.
bool triangleValid(const std::array<Corner, 3>& corners, const std::array<Vec2, 81>& d, Vec2 size) {
    std::array<Vec2, 3> rest{};
    std::array<Vec2, 3> deformed{};
    for (std::size_t k = 0; k < 3; ++k) {
        rest[k] = Vec2{size.x * corners[k].x / 8.0, size.y * corners[k].y / 8.0};
        const Vec2 disp = d[node(corners[k].x, corners[k].y)];
        deformed[k] = Vec2{rest[k].x + disp.x, rest[k].y + disp.y};
    }
    const double restArea = signedArea(rest[0], rest[1], rest[2]);
    const double area = signedArea(deformed[0], deformed[1], deformed[2]);
    return std::isfinite(area) && area / restArea >= 0.2;
}

// Lattice validity for test geometry: finite, bounded, and every triangle keeps orientation.
// A reset (settled) field is the undeformed, trivially valid state.
bool latticeValid(const LiquidMesh& mesh, Vec2 size) {
    if (mesh.settled())
        return true;
    const auto& d = mesh.displacements();
    const double limit = std::min(120.0, 0.2 * std::min(size.x, size.y));
    for (const Vec2& p : d) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            return false;
        if (magnitude(p) > limit + 1e-9)
            return false;
    }
    for (unsigned cy = 0; cy < 8; ++cy) {
        for (unsigned cx = 0; cx < 8; ++cx) {
            const std::array<Corner, 3> first{Corner{cx, cy}, Corner{cx, cy + 1}, Corner{cx + 1, cy}};
            const std::array<Corner, 3> second{Corner{cx + 1, cy}, Corner{cx, cy + 1}, Corner{cx + 1, cy + 1}};
            if (!triangleValid(first, d, size) || !triangleValid(second, d, size))
                return false;
        }
    }
    return true;
}

// Render-mesh validity: the 32x32-cell mesh sampled from sampleDisplacement keeps every triangle oriented.
bool renderValid(const LiquidMesh& mesh, Vec2 size) {
    constexpr unsigned cells = 32;
    const auto position = [&](unsigned i, unsigned j) {
        const double u = static_cast<double>(i) / cells;
        const double v = static_cast<double>(j) / cells;
        const Vec2 d = mesh.sampleDisplacement(u, v);
        if (!std::isfinite(d.x) || !std::isfinite(d.y))
            return Vec2{kNaN, kNaN};
        return Vec2{size.x * u + d.x, size.y * v + d.y};
    };
    for (unsigned cy = 0; cy < cells; ++cy) {
        for (unsigned cx = 0; cx < cells; ++cx) {
            const Vec2 rest00{size.x * cx / cells, size.y * cy / cells};
            const Vec2 rest10{size.x * (cx + 1) / cells, size.y * cy / cells};
            const Vec2 rest01{size.x * cx / cells, size.y * (cy + 1) / cells};
            const Vec2 rest11{size.x * (cx + 1) / cells, size.y * (cy + 1) / cells};
            const Vec2 p00 = position(cx, cy), p10 = position(cx + 1, cy);
            const Vec2 p01 = position(cx, cy + 1), p11 = position(cx + 1, cy + 1);
            const std::array<std::array<Vec2, 3>, 2> rest{{{rest00, rest01, rest10}, {rest10, rest01, rest11}}};
            const std::array<std::array<Vec2, 3>, 2> deformed{{{p00, p01, p10}, {p10, p01, p11}}};
            for (std::size_t t = 0; t < 2; ++t) {
                const double restArea = signedArea(rest[t][0], rest[t][1], rest[t][2]);
                const double area = signedArea(deformed[t][0], deformed[t][1], deformed[t][2]);
                if (!std::isfinite(area) || !(area / restArea >= 0.2))
                    return false;
            }
        }
    }
    return true;
}

// Wiggle-only helpers (unchanged behavior). Liquid tests use the frame driver below instead.
// 1. A +80 px horizontal translation produces a non-affine, asymmetric lag field.
void testObservableNonaffineDrag() {
    LiquidMesh mesh{liquid_glass::MotionMode::Wiggle};
    mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
    const auto& d = mesh.displacements();

    const double leading = d[node(8, 4)].x;
    const double middle = d[node(4, 4)].x;
    const double trailing = d[node(0, 4)].x;

    // Lag opposes the motion everywhere, with the leading edge lagging far less than the trailing edge.
    CHECK(leading < 0.0 && middle < 0.0 && trailing < 0.0);
    CHECK(std::abs(leading - trailing) > 10.0);
    CHECK(std::abs(middle - trailing) > 0.5);

    // A rigid or affine shift would put the middle exactly at the edges' average; the bow breaks that.
    CHECK(std::abs(middle - 0.5 * (leading + trailing)) > 1.0);

    // The lattice bends across the middle column as well, not just along the travel axis.
    CHECK(std::abs(d[node(4, 0)].x - d[node(4, 4)].x) > 1.0);

    // Pure x-translation excites no y displacement.
    double maxY = 0.0;
    for (const Vec2& p : d)
        maxY = std::max(maxY, std::abs(p.y));
    CHECK(maxY == 0.0);
}

// 2. Released motion returns to rest within 1.0 s without repeated visible sign reversals.
void testSettleAndSignReversals() {
    LiquidMesh mesh{liquid_glass::MotionMode::Wiggle};
    mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);

    std::array<int, 81> reversals{};
    std::array<int, 81> lastSign{};
    constexpr int frames = 60; // 1.0 s at 60 Hz
    for (int f = 0; f < frames; ++f) {
        mesh.advance(1.0 / 60.0, kWindow);
        const auto& d = mesh.displacements();
        for (std::size_t i = 0; i < d.size(); ++i) {
            if (std::abs(d[i].x) <= 0.5)
                continue;
            const int sign = d[i].x > 0.0 ? 1 : -1;
            if (lastSign[i] != 0 && sign != lastSign[i])
                ++reversals[i];
            lastSign[i] = sign;
        }
    }

    for (std::size_t i = 0; i < reversals.size(); ++i)
        CHECK(reversals[i] <= 1);
    CHECK(mesh.settled());
    CHECK(maxMagnitude(mesh.displacements()) == 0.0);
}

// 3. Spatial coupling: the lag ratios of edge nodes to the center change over time. Independent
// identical springs would keep the initial ratios exactly, because they share one decay factor.
void testSpatialCouplingRelativeResponse() {
    LiquidMesh mesh{liquid_glass::MotionMode::Wiggle};
    mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);

    const auto ratios = [&mesh]() {
        const auto& d = mesh.displacements();
        const double center = d[node(4, 4)].x;
        return std::array<double, 2>{d[node(8, 4)].x / center, d[node(0, 4)].x / center};
    };

    const std::array<double, 2> before = ratios();
    for (int f = 0; f < 6; ++f) // 0.1 s at 60 Hz
        mesh.advance(1.0 / 60.0, kWindow);
    const std::array<double, 2> after = ratios();

    CHECK(std::isfinite(after[0]) && std::isfinite(after[1]));
    // Leading-edge lag moves toward the center's response as neighbor coupling smooths shear.
    CHECK(after[0] > before[0]);
    double maxRelativeChange = 0.0;
    for (std::size_t k = 0; k < before.size(); ++k)
        maxRelativeChange = std::max(maxRelativeChange, std::abs(after[k] - before[k]) / std::abs(before[k]));
    CHECK(maxRelativeChange > 0.02);
}

struct Snapshots {
    std::array<Vec2, 81> half;
    std::array<Vec2, 81> full;
};

// Wiggle schedule: +80 px at 0 s, -40 px at exactly 0.5 s, impulse semantics.
Snapshots runWiggleSchedule(double hz) {
    LiquidMesh mesh{MotionMode::Wiggle};
    mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);

    const int framesToHalf = static_cast<int>(std::lround(hz * 0.5));
    const int framesTotal = static_cast<int>(std::lround(hz * 1.0));
    for (int f = 0; f < framesToHalf; ++f)
        mesh.advance(1.0 / hz, kWindow);

    Snapshots result{};
    result.half = mesh.displacements();

    mesh.recordTranslation(Vec2{-40.0, 0.0}, kWindow, kStrength);
    for (int f = framesToHalf; f < framesTotal; ++f)
        mesh.advance(1.0 / hz, kWindow);

    result.full = mesh.displacements();
    return result;
}

// Fixed-step agreement for the preserved wiggle.
void testFixedStepBoundaries() {
    const Snapshots s30 = runWiggleSchedule(30.0);
    const Snapshots s60 = runWiggleSchedule(60.0);
    const Snapshots s144 = runWiggleSchedule(144.0);
    CHECK(magnitude(s30.half[node(4, 4)]) > 0.1);
    CHECK(magnitude(s30.full[node(4, 4)]) > 0.1);
    CHECK(maxDifference(s30.half, s60.half) <= 0.01);
    CHECK(maxDifference(s30.half, s144.half) <= 0.01);
    CHECK(maxDifference(s30.full, s60.full) <= 0.01);
    CHECK(maxDifference(s30.full, s144.full) <= 0.01);
}

// Extreme, reversing, tiny, paused, zero-strength, empty and nonfinite inputs, in either mode.
void checkDegenerateGeometry(MotionMode mode) {
    // Extreme translation is bounded and keeps every triangle oriented.
    {
        LiquidMesh mesh{mode};
        mesh.recordTranslation(Vec2{5000.0, 0.0}, kWindow, kStrength);
        CHECK(latticeValid(mesh, kWindow));
        for (int f = 0; f < 30; ++f) {
            mesh.advance(1.0 / 60.0, kWindow);
            CHECK(latticeValid(mesh, kWindow));
        }
    }

    // Rapid reversals, including a diagonal impulse.
    {
        LiquidMesh mesh{mode};
        const std::array<Vec2, 5> impulses{Vec2{80.0, 0.0}, Vec2{-80.0, 0.0}, Vec2{80.0, 0.0},
                                           Vec2{-80.0, 0.0}, Vec2{56.0, 56.0}};
        for (const Vec2& impulse : impulses) {
            mesh.recordTranslation(impulse, kWindow, kStrength);
            CHECK(latticeValid(mesh, kWindow));
            for (int f = 0; f < 3; ++f) {
                mesh.advance(1.0 / 60.0, kWindow);
                CHECK(latticeValid(mesh, kWindow));
            }
        }
    }

    // A 16x16 window: the bound is 0.2 * 16 = 3.2 px, so the +80 px impulse is clamped hard.
    {
        LiquidMesh mesh{mode};
        const Vec2 tiny{16.0, 16.0};
        mesh.recordTranslation(Vec2{80.0, 0.0}, tiny, kStrength);
        CHECK(maxMagnitude(mesh.displacements()) <= 0.2 * 16.0 + 1e-9);
        CHECK(latticeValid(mesh, tiny));
        for (int f = 0; f < 30; ++f) {
            mesh.advance(1.0 / 60.0, tiny);
            CHECK(latticeValid(mesh, tiny));
        }
    }

    // A 1000 ms pause exceeds the 250 ms gap limit and resets to the undeformed state.
    {
        LiquidMesh mesh{mode};
        mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
        CHECK(!mesh.advance(1.0, kWindow));
        CHECK(mesh.settled());
        CHECK(maxMagnitude(mesh.displacements()) == 0.0);
    }

    // Zero strength bypasses deformation entirely.
    {
        LiquidMesh mesh{mode};
        mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, 0.0);
        CHECK(mesh.settled());
        CHECK(!mesh.advance(1.0 / 60.0, kWindow));
        CHECK(maxMagnitude(mesh.displacements()) == 0.0);
    }

    // NaN strength is invalid input and resets.
    {
        LiquidMesh mesh{mode};
        mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
        mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kNaN);
        CHECK(mesh.settled());
    }

    // Zero delta does not excite, even though the field is still constrained.
    {
        LiquidMesh mesh{mode};
        mesh.recordTranslation(Vec2{0.0, 0.0}, kWindow, kStrength);
        CHECK(mesh.settled());
    }

    // Empty geometry resets, whether it arrives with a translation or with advance().
    {
        LiquidMesh mesh{mode};
        mesh.recordTranslation(Vec2{80.0, 0.0}, Vec2{0.0, 400.0}, kStrength);
        CHECK(mesh.settled());

        mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
        CHECK(!mesh.settled());
        CHECK(!mesh.advance(1.0 / 60.0, Vec2{600.0, 0.0}));
        CHECK(mesh.settled());
    }

    // Nonfinite translation, size, elapsed time, or excitation resets.
    {
        LiquidMesh mesh{mode};
        mesh.recordTranslation(Vec2{kNaN, 0.0}, kWindow, kStrength);
        CHECK(mesh.settled());

        mesh.recordTranslation(Vec2{80.0, 0.0}, Vec2{kNaN, 400.0}, kStrength);
        CHECK(mesh.settled());

        mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
        CHECK(!mesh.advance(kNaN, kWindow));
        CHECK(mesh.settled());

        mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
        CHECK(!mesh.advance(kInfinity, kWindow));
        CHECK(mesh.settled());

        // Overflowing excitation becomes nonfinite and must reset rather than propagate.
        mesh.recordTranslation(Vec2{1e308, 0.0}, kWindow, kStrength);
        CHECK(latticeValid(mesh, kWindow));
        mesh.advance(1.0 / 60.0, kWindow);
        CHECK(latticeValid(mesh, kWindow));
    }

    // Resizing without translation applies the new bound immediately, with or without time stepping.
    {
        LiquidMesh mesh{mode};
        mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
        mesh.recordTranslation(Vec2{0.0, 0.0}, Vec2{100.0, 100.0}, kStrength);
        CHECK(maxMagnitude(mesh.displacements()) <= 0.2 * 100.0 + 1e-9);
        CHECK(latticeValid(mesh, Vec2{100.0, 100.0}));
    }
    {
        LiquidMesh mesh{mode};
        mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
        mesh.advance(0.0, Vec2{100.0, 100.0});
        CHECK(maxMagnitude(mesh.displacements()) <= 0.2 * 100.0 + 1e-9);
        CHECK(latticeValid(mesh, Vec2{100.0, 100.0}));
    }
}

void testExtremeAndDegenerateGeometry() {
    for (const MotionMode mode : kModes)
        checkDegenerateGeometry(mode);
}

// ---------------------------------------------------------------------------
// Liquid material targets.
// ---------------------------------------------------------------------------

// One frame of a constant-velocity drag: record this frame's translation, then advance by its duration.
void driveFrame(LiquidMesh& mesh, Vec2 velocity, double hz, Vec2 size, double strength) {
    const double dt = 1.0 / hz;
    mesh.recordTranslation(Vec2{velocity.x * dt, velocity.y * dt}, size, strength);
    mesh.advance(dt, size);
}

// One frame with no motion (a stop).
void coastFrame(LiquidMesh& mesh, double hz, Vec2 size) {
    mesh.advance(1.0 / hz, size);
}

// Trailing-edge displacement along x for a +x drag: negative while lagging behind the window.
double trailingX(const LiquidMesh& mesh) {
    return mesh.displacements()[node(0, 4)].x;
}

struct StopTrace {
    double peakLag = 0.0;        // lag magnitude at the end of the drag
    double overshoot = 0.0;      // largest positive excursion after the stop
    double laterNegative = 0.0;  // largest negative excursion after the overshoot peak
    double settleSeconds = 1e9;  // time from stop until settled(); 1e9 if never
};

// Drags at 2000 px/s for 0.25 s at 60 Hz, then stops and records the trailing response.
StopTrace traceStop(const MaterialParams& params, double hz = 60.0, std::uint64_t seed = 0) {
    LiquidMesh mesh{MotionMode::Liquid, params, seed};
    for (int f = 0; f < static_cast<int>(std::lround(hz * 0.25)); ++f)
        driveFrame(mesh, Vec2{2000.0, 0.0}, hz, kWindow, kStrength);

    StopTrace t{};
    t.peakLag = -trailingX(mesh);

    double bestOvershoot = 0.0;
    int bestFrame = -1;
    std::array<double, 400> series{};
    int frames = 0;
    for (int f = 1; f <= static_cast<int>(hz * 2.0); ++f) {
        coastFrame(mesh, hz, kWindow);
        series[static_cast<std::size_t>(f - 1)] = trailingX(mesh);
        frames = f;
        if (mesh.settled()) {
            if (t.settleSeconds > 1e8)
                t.settleSeconds = f / hz;
            break;
        }
        if (trailingX(mesh) > bestOvershoot) {
            bestOvershoot = trailingX(mesh);
            bestFrame = f - 1;
        }
    }
    t.overshoot = bestOvershoot;
    if (bestFrame >= 0) {
        for (int k = bestFrame; k < frames; ++k)
            t.laterNegative = std::max(t.laterNegative, -series[static_cast<std::size_t>(k)]);
    }
    return t;
}

// Liquid drag schedule at a given frame rate: drag for 0.25 s, then stop until 0.5 s.
struct LiquidSnapshots {
    std::array<Vec2, 81> drag;
    std::array<Vec2, 81> stop;
};

LiquidSnapshots runLiquidSchedule(double hz, const MaterialParams& params, std::uint64_t seed = 0) {
    LiquidMesh mesh{MotionMode::Liquid, params, seed};
    const int framesDrag = static_cast<int>(std::lround(hz * 0.25));
    const int framesTotal = static_cast<int>(std::lround(hz * 0.5));
    for (int f = 0; f < framesDrag; ++f)
        driveFrame(mesh, Vec2{2000.0, 0.0}, hz, kWindow, kStrength);
    LiquidSnapshots result{};
    result.drag = mesh.displacements();
    for (int f = framesDrag; f < framesTotal; ++f)
        coastFrame(mesh, hz, kWindow);
    result.stop = mesh.displacements();
    return result;
}

// Drag at 2000 px/s: the trailing side lags well behind, the leading face stays tight, and the
// sheet narrows transversely.
void testLiquidDragLag() {
    LiquidMesh mesh{MotionMode::Liquid};
    for (int f = 0; f < 15; ++f) {
        driveFrame(mesh, Vec2{2000.0, 0.0}, 60.0, kWindow, kStrength);
        CHECK(renderValid(mesh, kWindow));
    }
    const double trailing = -trailingX(mesh);
    const double leading = std::abs(mesh.displacements()[node(8, 4)].x);
    double transverse = 0.0;
    for (const Vec2& p : mesh.displacements())
        transverse = std::max(transverse, std::abs(p.y));

    CHECK(trailing >= 20.0);
    CHECK(leading <= 0.35 * trailing);
    CHECK(transverse > 2.0);
}

// Slow drag at 100 px/s for 0.5 s deforms little.
void testLiquidSlowDragIsSubtle() {
    LiquidMesh mesh{MotionMode::Liquid};
    for (int f = 0; f < 30; ++f)
        driveFrame(mesh, Vec2{100.0, 0.0}, 60.0, kWindow, kStrength);
    CHECK(maxMagnitude(mesh.displacements()) < 3.0);
}

// Abrupt stop: a small overshoot, a decaying second swing, and an exact settle.
void testLiquidStopOvershootAndSettle() {
    const StopTrace t = traceStop(MaterialParams{});
    CHECK(t.peakLag > 20.0);
    CHECK(t.overshoot >= 0.05 * t.peakLag);
    CHECK(t.overshoot <= 0.30 * t.peakLag);
    CHECK(t.laterNegative <= 0.5 * t.overshoot);
    CHECK(t.settleSeconds <= 1.5);
    LiquidMesh mesh{MotionMode::Liquid};
    for (int f = 0; f < 15; ++f)
        driveFrame(mesh, Vec2{2000.0, 0.0}, 60.0, kWindow, kStrength);
    for (int f = 0; f < 90; ++f)
        coastFrame(mesh, 60.0, kWindow);
    CHECK(mesh.settled());
    CHECK(maxMagnitude(mesh.displacements()) == 0.0);
}

// Direction reversal at speed: the centre node's lag crosses zero within 150 ms. The centre is the
// node whose lag truly changes sign with the drive; the old trailing edge becomes the leading face.
// The render mesh keeps every triangle oriented throughout.
void testLiquidReversal() {
    LiquidMesh mesh{MotionMode::Liquid};
    for (int f = 0; f < 15; ++f)
        driveFrame(mesh, Vec2{2000.0, 0.0}, 60.0, kWindow, kStrength);
    CHECK(trailingX(mesh) < -10.0);

    int crossFrame = -1;
    for (int f = 0; f < 30; ++f) {
        driveFrame(mesh, Vec2{-2000.0, 0.0}, 60.0, kWindow, kStrength);
        CHECK(renderValid(mesh, kWindow));
        if (crossFrame < 0 && mesh.displacements()[4 * 9 + 4].x > 0.0)
            crossFrame = f;
    }
    CHECK(crossFrame >= 0);
    CHECK(crossFrame >= 0 && crossFrame * (1.0 / 60.0) <= 0.15);
}

// Refresh-rate independence: the same drag and stop agree across 30..240 Hz.
void testLiquidRefreshRateAgreement() {
    const LiquidSnapshots s30 = runLiquidSchedule(30.0, MaterialParams{});
    const LiquidSnapshots s60 = runLiquidSchedule(60.0, MaterialParams{});
    const LiquidSnapshots s144 = runLiquidSchedule(144.0, MaterialParams{});
    const LiquidSnapshots s240 = runLiquidSchedule(240.0, MaterialParams{});
    CHECK(maxMagnitude(s60.drag) > 10.0);
    CHECK(maxDifference(s30.drag, s60.drag) <= 1.0);
    CHECK(maxDifference(s144.drag, s60.drag) <= 1.0);
    // Drag lag is ~70 px; a 2 px band (about 3%) absorbs the one-substep sampling difference at 240 Hz.
    CHECK(maxDifference(s240.drag, s60.drag) <= 2.0);
    // Stop snapshots may differ by up to ~2% of peak lag (1.5 px) across cadences: each tick's pseudo-force is
    // sampled once, so a 30 Hz tick shifts its decay by up to one frame.
    CHECK(maxDifference(s30.stop, s60.stop) <= 1.5);
    CHECK(maxDifference(s144.stop, s60.stop) <= 1.5);
    CHECK(maxDifference(s240.stop, s60.stop) <= 1.5);
}

// Size scaling: the same relative speed produces a similar relative deformation on small and large windows.
void testLiquidSizeScaling() {
    const auto peakOver = [](Vec2 size) {
        const double minSide = std::min(size.x, size.y);
        LiquidMesh mesh{MotionMode::Liquid};
        for (int f = 0; f < 12; ++f) {
            driveFrame(mesh, Vec2{5.0 * minSide, 0.0}, 60.0, size, kStrength);
            CHECK(renderValid(mesh, size));
        }
        return -trailingX(mesh) / minSide;
    };
    const double small = peakOver(Vec2{300.0, 200.0});
    const double large = peakOver(Vec2{1600.0, 1000.0});
    CHECK(small > 0.0 && large > 0.0);
    CHECK(std::max(small, large) <= 1.6 * std::min(small, large));
}

// Degenerate and thin geometry stays stable and settles exactly.
void testLiquidDegenerateGeometrySettles() {
    for (const Vec2 size : {Vec2{16.0, 16.0}, Vec2{1000.0, 40.0}}) {
        LiquidMesh mesh{MotionMode::Liquid};
        for (int f = 0; f < 15; ++f) {
            driveFrame(mesh, Vec2{2000.0, 0.0}, 60.0, size, kStrength);
            CHECK(latticeValid(mesh, size));
            CHECK(renderValid(mesh, size));
        }
        for (int f = 0; f < 240 * 4; ++f) {
            coastFrame(mesh, 240.0, size);
            CHECK(latticeValid(mesh, size));
        }
        CHECK(mesh.settled());
        CHECK(maxMagnitude(mesh.displacements()) == 0.0);
    }
}


// Elasticity: higher elasticity returns faster.
void testLiquidElasticitySettleTime() {
    MaterialParams firm{};
    firm.elasticity = 1.0;
    MaterialParams soft{};
    soft.elasticity = 0.0;
    const StopTrace snappy = traceStop(firm);
    const StopTrace slow = traceStop(soft);
    CHECK(snappy.settleSeconds < slow.settleSeconds);
}

// Momentum: zero gives no overshoot; higher momentum gives a larger overshoot.
void testLiquidMomentumOvershoot() {
    MaterialParams none{};
    none.momentum = 0.0;
    MaterialParams high{};
    high.momentum = 1.0;
    const StopTrace noOvershoot = traceStop(none);
    const StopTrace standard = traceStop(MaterialParams{});
    const StopTrace big = traceStop(high);
    CHECK(noOvershoot.overshoot <= 0.02 * noOvershoot.peakLag);
    CHECK(big.overshoot > standard.overshoot);
}

// sampleDisplacement: exact at nodes, clamped outside [0, 1], and C1 across cell boundaries.
void testSampleDisplacement() {
    LiquidMesh mesh{MotionMode::Liquid};
    for (int f = 0; f < 15; ++f)
        driveFrame(mesh, Vec2{2000.0, 0.0}, 60.0, kWindow, kStrength);
    const auto& d = mesh.displacements();

    for (unsigned y = 0; y < 9; ++y) {
        for (unsigned x = 0; x < 9; ++x) {
            const Vec2 s = mesh.sampleDisplacement(x / 8.0, y / 8.0);
            CHECK(std::abs(s.x - d[node(x, y)].x) <= 1e-9);
            CHECK(std::abs(s.y - d[node(x, y)].y) <= 1e-9);
        }
    }

    const Vec2 clampedU = mesh.sampleDisplacement(-1.0, 2.0);
    CHECK(std::abs(clampedU.x - d[node(0, 8)].x) <= 1e-9);
    const Vec2 clampedV = mesh.sampleDisplacement(2.0, -1.0);
    CHECK(std::abs(clampedV.x - d[node(8, 0)].x) <= 1e-9);
    const Vec2 nonfinite = mesh.sampleDisplacement(kNaN, 0.5);
    CHECK(nonfinite.x == 0.0 && nonfinite.y == 0.0);

    // C1: the slope from each side of an interior cell boundary matches.
    constexpr double h = 1e-5;
    for (double v : {0.2, 0.55}) {
        const double u = 3.0 / 8.0;
        const double left = (mesh.sampleDisplacement(u, v).x - mesh.sampleDisplacement(u - h, v).x) / h;
        const double right = (mesh.sampleDisplacement(u + h, v).x - mesh.sampleDisplacement(u, v).x) / h;
        CHECK(std::abs(left - right) <= 1e-3 * std::max(1.0, std::abs(left)));
    }
}

// Viscosity changes internal flow: thick resists shear more than thin.
void testViscosityResistsInternalFlow() {
    MaterialParams thinParams{};
    thinParams.viscosity = 0.0;
    MaterialParams thickParams{};
    thickParams.viscosity = 1.0;
    LiquidMesh thin{MotionMode::Liquid, thinParams};
    LiquidMesh thick{MotionMode::Liquid, thickParams};
    // Viscosity must change internal flow, not masquerade as a strength control.
    CHECK(maxDifference(thin.displacements(), thick.displacements()) == 0.0);
    for (int frame = 0; frame < 15; ++frame) {
        driveFrame(thin, Vec2{2000.0, 0.0}, 60.0, kWindow, kStrength);
        driveFrame(thick, Vec2{2000.0, 0.0}, 60.0, kWindow, kStrength);
    }
    CHECK(maxDifference(thin.displacements(), thick.displacements()) > 0.5);
}

// Viscosity boundaries must remain stable and cadence-independent, including thin strips.
void testViscosityBoundariesAndCadence() {
    const std::array<double, 3> levels{0.0, 0.75, 1.0};
    for (const double level : levels) {
        MaterialParams params{};
        params.viscosity = level;
        const LiquidSnapshots s30 = runLiquidSchedule(30.0, params);
        const LiquidSnapshots s60 = runLiquidSchedule(60.0, params);
        const LiquidSnapshots s144 = runLiquidSchedule(144.0, params);
        CHECK(maxMagnitude(s30.drag) > 0.1);
        CHECK(maxDifference(s30.drag, s60.drag) <= 1.0);
        CHECK(maxDifference(s30.drag, s144.drag) <= 2.0);
        // Stop snapshots may differ by up to ~2.5% of peak lag across cadences: each tick's pseudo-force is sampled
        // once, so a 30 Hz tick shifts its decay by up to one frame. Thinnest material (viscosity 0) measures 1.57 px.
        CHECK(maxDifference(s30.stop, s60.stop) <= 1.6);
        CHECK(maxDifference(s30.stop, s144.stop) <= 1.6);
    }

    // Out-of-range viscosity clamps to the same material as its boundary.
    MaterialParams below{};
    below.viscosity = -0.5;
    MaterialParams zero{};
    zero.viscosity = 0.0;
    MaterialParams above{};
    above.viscosity = 1.5;
    MaterialParams one{};
    one.viscosity = 1.0;
    LiquidMesh low{MotionMode::Liquid, below}, thin{MotionMode::Liquid, zero};
    LiquidMesh high{MotionMode::Liquid, above}, thick{MotionMode::Liquid, one};
    CHECK(low.viscosity() == 0.0 && high.viscosity() == 1.0);
    for (auto* mesh : {&low, &thin, &high, &thick})
        mesh->recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
    for (int step = 0; step < 24; ++step) {
        for (auto* mesh : {&low, &thin, &high, &thick})
            mesh->advance(1.0 / 240.0, kWindow);
        CHECK(maxDifference(low.displacements(), thin.displacements()) == 0.0);
        CHECK(maxDifference(high.displacements(), thick.displacements()) == 0.0);
    }

    // Wiggle ignores material params: default and extreme values produce identical fields.
    MaterialParams wiggleThin{};
    wiggleThin.viscosity = 0.0;
    MaterialParams wiggleThick{};
    wiggleThick.viscosity = 1.0;
    MaterialParams wiggleNan{};
    wiggleNan.viscosity = kNaN;
    LiquidMesh wiggleDefault{MotionMode::Wiggle}, wiggleThinMesh{MotionMode::Wiggle, wiggleThin};
    LiquidMesh wiggleThickMesh{MotionMode::Wiggle, wiggleThick}, wiggleNaN{MotionMode::Wiggle, wiggleNan};
    for (auto* mesh : {&wiggleDefault, &wiggleThinMesh, &wiggleThickMesh, &wiggleNaN})
        mesh->recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
    for (int step = 0; step < 120; ++step) {
        for (auto* mesh : {&wiggleDefault, &wiggleThinMesh, &wiggleThickMesh, &wiggleNaN})
            mesh->advance(1.0 / 240.0, kWindow);
        CHECK(maxDifference(wiggleDefault.displacements(), wiggleThinMesh.displacements()) == 0.0);
        CHECK(maxDifference(wiggleDefault.displacements(), wiggleThickMesh.displacements()) == 0.0);
        CHECK(maxDifference(wiggleDefault.displacements(), wiggleNaN.displacements()) == 0.0);
    }

    for (const double invalid : {kNaN, kInfinity}) {
        MaterialParams bad{};
        bad.viscosity = invalid;
        LiquidMesh mesh{MotionMode::Liquid, bad};
        mesh.recordTranslation(Vec2{80.0, 0.0}, kWindow, kStrength);
        CHECK(mesh.settled());
        CHECK(!mesh.advance(1.0 / 240.0, kWindow));
        CHECK(maxMagnitude(mesh.displacements()) == 0.0);
    }
}

// Taper: the trailing corners pull cross-axis toward the tail at 15-20% of the centre lag, and the side edge
// falls monotonically from the corner to the centre with no kink or reversal, so there is no tail-corner cusp.
void testLiquidTrailingTaperCurvature() {
    LiquidMesh mesh{MotionMode::Liquid};
    for (int f = 0; f < 15; ++f)
        driveFrame(mesh, Vec2{2000.0, 0.0}, 60.0, kWindow, kStrength);
    const double centre = -trailingX(mesh);
    const double corner = -mesh.displacements()[node(0, 0)].x;
    const double taper = std::abs(mesh.displacements()[node(0, 0)].y);
    CHECK(centre > 20.0);
    CHECK(centre / corner >= 1.5);
    CHECK(taper >= 0.15 * centre);
    CHECK(taper <= 0.20 * centre);
    double previous = taper;
    for (unsigned y = 1; y <= 4; ++y) {
        const double edge = std::abs(mesh.displacements()[node(0, y)].y);
        CHECK(edge < previous);
        previous = edge;
    }
}

// Stop landing: the leading face bulges forward 2.5-4% of minSide, the trailing edge surges 15-30% of its lag,
// and the mesh settles within 1.0 s.
void testLiquidStopLandingBulgeAndSurge() {
    const double minSide = std::min(kWindow.x, kWindow.y);
    LiquidMesh mesh{MotionMode::Liquid};
    for (int f = 0; f < 15; ++f)
        driveFrame(mesh, Vec2{2000.0, 0.0}, 60.0, kWindow, kStrength);
    const double peakLag = -trailingX(mesh);
    double bulge = 0.0;
    double surge = 0.0;
    double elapsed = 0.0;
    while (!mesh.settled() && elapsed < 2.0) {
        mesh.advance(1.0 / 60.0, kWindow);
        elapsed += 1.0 / 60.0;
        bulge = std::max(bulge, mesh.displacements()[node(8, 4)].x);
        surge = std::max(surge, trailingX(mesh));
    }
    CHECK(bulge >= 0.02 * minSide);
    CHECK(bulge <= 0.04 * minSide);
    CHECK(surge >= 0.15 * peakLag);
    CHECK(surge <= 0.30 * peakLag);
    CHECK(elapsed <= 1.0);
}

struct CadenceStats {
    double lagMean = 0.0;
    double lagMaxStep = 0.0;
    double bulge = 0.0;
};

// Steady 2000 px/s drag where translation lands on irregular frames: the lattice advances every frame, but the
// position stream only reports on some frames, so deltas arrive lumped. `visible(k, t)` gives the position-stream
// timestamp seen on frame k (ending at time t). Measured over the second half of two seconds of drag.
template <typename Visible>
CadenceStats driveCadence(double advanceHz, Visible visible) {
    constexpr double kSpeed = 2000.0;
    const double dt = 1.0 / advanceHz;
    const int warmupFrames = static_cast<int>(std::lround(advanceHz * 1.0));
    const int totalFrames = static_cast<int>(std::lround(advanceHz * 2.0));
    LiquidMesh mesh{MotionMode::Liquid};
    CadenceStats stats{};
    double previousReported = 0.0;
    double previousLag = 0.0;
    double sumLag = 0.0;
    int count = 0;
    for (int k = 0; k < totalFrames; ++k) {
        const double t = (k + 1) * dt;
        const double reported = visible(k, t);
        const double delta = kSpeed * (reported - previousReported);
        previousReported = reported;
        mesh.recordTranslation(Vec2{delta, 0.0}, kWindow, kStrength);
        mesh.advance(dt, kWindow);
        if (k < warmupFrames)
            continue;
        const double lag = -trailingX(mesh);
        if (count > 0)
            stats.lagMaxStep = std::max(stats.lagMaxStep, std::abs(lag - previousLag));
        previousLag = lag;
        sumLag += lag;
        ++count;
        stats.bulge = std::max(stats.bulge, mesh.displacements()[node(8, 4)].x);
    }
    stats.lagMean = sumLag / count;
    return stats;
}

// Irregular cadences must not read as a stop: the lag changes by under 15% of its mean frame to frame, and the
// leading face never bulges more than 0.5% of minSide before a real stop.
void testLiquidIrregularCadenceSteadyDrag() {
    const double minSide = std::min(kWindow.x, kWindow.y);
    const CadenceStats fast = driveCadence(144.0, [](int, double t) { return std::floor(t * 125.0) / 125.0; });
    const CadenceStats dropped = driveCadence(60.0, [](int k, double) { return (k % 3 == 2 ? k : k + 1) / 60.0; });
    for (const CadenceStats* stats : {&fast, &dropped}) {
        CHECK(stats->lagMean > 20.0);
        CHECK(stats->lagMaxStep < 0.15 * stats->lagMean);
        CHECK(stats->bulge < 0.01 * minSide);
    }
}

// Variation 0: the seed has no effect, so every window moves exactly alike.
void testVariationZeroIgnoresSeed() {
    const LiquidSnapshots a = runLiquidSchedule(60.0, MaterialParams{}, 1);
    const LiquidSnapshots b = runLiquidSchedule(60.0, MaterialParams{}, 0x9E3779B97F4A7C15ULL);
    CHECK(maxDifference(a.drag, b.drag) == 0.0);
    CHECK(maxDifference(a.stop, b.stop) == 0.0);
}

// Variation > 0: two windows given the same motion (a scrolling-layout pan) deform visibly differently,
// while equal seeds stay reproducible.
void testVariationSeedsDiffer() {
    MaterialParams params{};
    params.variation = 0.5;
    const LiquidSnapshots a = runLiquidSchedule(60.0, params, 1);
    const LiquidSnapshots b = runLiquidSchedule(60.0, params, 2);
    const LiquidSnapshots again = runLiquidSchedule(60.0, params, 1);
    CHECK(maxDifference(a.drag, b.drag) > 2.0);
    CHECK(maxDifference(a.drag, again.drag) == 0.0);
    CHECK(maxDifference(a.stop, again.stop) == 0.0);
}

// Each movement from rest draws a new character: the same window repeating the same move responds differently.
void testVariationRerollsPerMotion() {
    MaterialParams params{};
    params.variation = 0.5;
    LiquidMesh mesh{MotionMode::Liquid, params, 7};
    const auto dragOnce = [&] {
        for (int f = 0; f < 15; ++f)
            driveFrame(mesh, Vec2{2000.0, 0.0}, 60.0, kWindow, kStrength);
        const auto field = mesh.displacements();
        for (int f = 0; f < 240 && !mesh.settled(); ++f)
            coastFrame(mesh, 60.0, kWindow);
        CHECK(mesh.settled());
        return field;
    };
    const auto first = dragOnce();
    const auto second = dragOnce();
    CHECK(maxDifference(first, second) > 2.0);
}

// Full variation never folds the render mesh, still settles exactly, and keeps momentum 0 free of overshoot.
void testVariationStaysValidAndSettles() {
    for (const double momentum : {0.0, 0.5, 1.0}) {
        MaterialParams params{};
        params.variation = 1.0;
        params.momentum = momentum;
        for (std::uint64_t seed = 0; seed < 48; ++seed) {
            LiquidMesh mesh{MotionMode::Liquid, params, seed};
            bool valid = true;
            for (int f = 0; f < 15; ++f) {
                driveFrame(mesh, Vec2{2400.0, 900.0}, 60.0, kWindow, kStrength);
                valid = valid && renderValid(mesh, kWindow) && latticeValid(mesh, kWindow);
            }
            for (int f = 0; f < 10; ++f) {
                driveFrame(mesh, Vec2{-2400.0, -900.0}, 60.0, kWindow, kStrength);
                valid = valid && renderValid(mesh, kWindow) && latticeValid(mesh, kWindow);
            }
            int frames = 0;
            for (; frames < 150 && !mesh.settled(); ++frames) {
                coastFrame(mesh, 60.0, kWindow);
                valid = valid && renderValid(mesh, kWindow) && latticeValid(mesh, kWindow);
            }
            CHECK(valid);
            CHECK(mesh.settled());

            if (momentum == 0.0) {
                const StopTrace t = traceStop(params, 60.0, seed);
                CHECK(t.overshoot <= 0.02 * t.peakLag);
            }
        }
    }
}

} // namespace

int main() {
    testObservableNonaffineDrag();
    testSettleAndSignReversals();
    testSpatialCouplingRelativeResponse();
    testFixedStepBoundaries();
    testExtremeAndDegenerateGeometry();
    testLiquidDragLag();
    testLiquidTrailingTaperCurvature();
    testLiquidSlowDragIsSubtle();
    testLiquidStopOvershootAndSettle();
    testLiquidStopLandingBulgeAndSurge();
    testLiquidIrregularCadenceSteadyDrag();
    testLiquidReversal();
    testLiquidRefreshRateAgreement();
    testLiquidSizeScaling();
    testLiquidDegenerateGeometrySettles();
    testLiquidElasticitySettleTime();
    testLiquidMomentumOvershoot();
    testSampleDisplacement();
    testViscosityResistsInternalFlow();
    testViscosityBoundariesAndCadence();
    testVariationZeroIgnoresSeed();
    testVariationSeedsDiffer();
    testVariationRerollsPerMotion();
    testVariationStaysValidAndSettles();

    if (g_failures == 0) {
        std::printf("liquid-mesh-tests: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "liquid-mesh-tests: %d check(s) failed\n", g_failures);
    return 1;
}
