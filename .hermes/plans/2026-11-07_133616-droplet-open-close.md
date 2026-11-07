# Surface-Tension Droplet Open/Close Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Replace the Liquid mode's open/close "pillow" pose with a bold, immersive surface-tension droplet. **Open:** a small round bead swells, squares its corners and lands with one soft rebound. **Close:** the corners round off, the window beads up, swells briefly and collapses to a point.

**Architecture:** A new pure class `liquid_glass::DropletTransition` produces a closed-form outline map in logical px. It is radially homogeneous with a positive gain, so it can never fold the image. The renderer applies it on top of the existing movement lattice: `displayed = droplet(compensation ⊙ (rest + latticeDisplacement − centre))`. Because the droplet is separate from the lattice, it escapes the lattice's 120 px / 20% displacement bound and its 20%-area triangle guard, which are why the current effect barely shows. The plugin also takes over geometry during transitions. On open, a size compensation cancels Hyprland's native `windowsIn` popin scale so the two don't stack. On close, the plugin owns placement scale, alpha and timing, and keeps the snapshot framebuffer alive. That way the pop always completes, however short or springy the user's native `fadeOut` is.

**Tech Stack:** C++26, Hyprland 0.57.0 plugin API (pinned `0aa6745f`), CMake/Ninja, CTest with the repo's own `CHECK` harness, nested Hyprland for visual verification.

---

## Context and evidence (read before starting)

- User feedback: the current open/close is **too subtle / invisible** and **reads as lens distortion**. They want something more immersive. Chosen direction: **surface-tension droplet**.
- The current implementation is `LiquidMesh::recordTransition` in `src/LiquidMesh.cpp:375-433`. It builds a static symmetric pose (`shape = 0.4 + 0.6·nx²·ny²`, `kLiquidTransitionScale = 0.20`) and hands it to the lattice springs. Effective amplitude is only about 13% of the shorter side at strength 0.65, and it is soft-saturated by `displacementLimit` (`src/LiquidMesh.cpp:133`). Close holds the pose (`m_target`, `holdingTarget`) while the native fade runs. Evidence: `.project-manager/evidence/fluid/final-open-sheet.png`, `final-close-rest.png`.
- Native interference:
  - Open: the transformer deforms `context.currentBox`, which Hyprland is already scaling with `windowsIn` popin. The fixture uses `popin 87%` (`tests/nested/hyprland.lua:29`); the user's config uses `popin 92%` (`~/.config/hypr/animations.lua:16`).
  - Close: the mesh is projected onto `fadeout->renderBox()` (`src/LiquidCloseRenderer.cpp:362`), which also shrinks with the `windowsOut` popin. Alpha is `fadeout->alpha()`. The user's `fadeOut` uses a **spring** (`md3_spatial_default`), so alpha may be non-monotonic, and its duration is unrelated to any droplet timeline.
- Host facts verified against pinned sources:
  - `IHyprRenderer::renderFadeouts` (5-arg) is called **unconditionally per plane** every frame (`src/render/Renderer.cpp:390,408,473,529,570,1327…`), so the existing hook can keep drawing a record after its native fadeout is gone.
  - `CWindowFadeout::renderBox()` = monitor-sized snapshot placed so the window's source position follows `m_realPosition`, scaled by `m_realSize/m_sourceSize` (`src/desktop/state/WindowFadeout.cpp`).
  - `geometricBox(GEOMETRIC_GOAL)` exists (`desktop/view/types/Geometric.hpp:14`).
  - `presentation().animatingIn()` exists.
- No Git repository; initialising one has not been authorised (`.project-manager/progress.md:4`). **Skip all commit steps**; checkpoint by running the tests.
- Primary desktop configuration and plugin loading must stay untouched. Do all visual work in the nested compositor (README "Isolated nested verification").

## Design decisions

| Decision | Choice | Why |
|---|---|---|
| Where the droplet lives | New `DropletTransition`, separate from `LiquidMesh` | Movement physics stays bounded and untouched; the droplet needs much larger, time-driven, analytic shapes |
| Outline shape | Superellipse with exponent `n = 2 / roundness` (n=2 circle, n→∞ rectangle), applied radially: `q · k(θ)` with `k = 1 / ‖q/‖q‖∞‖ₙ` | Smooth rounded corners (no pointy radial blend); `k > 0` and degree-1 homogeneity give Jacobian `k² > 0`, so it never folds |
| Open dynamics | Two damped springs (x, y spread), y at 0.86× frequency, ζ from `momentum`, ω from `elasticity`; one decaying 4-lobe surface ripple when the corners square off | Bead → transient oval → rectangle; "one soft rebound" |
| Close dynamics | Fixed timeline (≈0.42 s × 2^(0.5−elasticity)): bead up (0–55%), swell +12% (55–78%), collapse (78–100%); alpha fades over 70–100% | Deterministic, completes regardless of native fade |
| Bead size | `max(0.12, 1 − 1.08·strength)` of the half shorter side (≈0.30 at default 0.65) | Bold by default; `strength` keeps meaning "intensity" |
| Native popin (open) | Compensate `goal/current` size about the centre while `animatingIn()` and an open droplet began | Stops the doubled/mushy scale; slide styles still slide (c≈1) |
| Native close | Owned records: own alpha (`sourceAlpha · droplet.alpha()`), own scale (initial render box re-centred on the native centre), snapshot retained after native end | A spring or very short `fadeOut` can no longer truncate or distort the pop |
| Wiggle | Unchanged: no droplet, native close path with lattice only | Existing contract |
| New config knobs | None | YAGNI: `strength`, `elasticity`, `momentum`, `transitions` cover it |

**Default assumptions (change before implementing if wrong):**

1. Window content is squeezed into the bead (no circular mask/crop; that would need a shader).
2. Close ends by collapsing to a point, not an outward burst.
3. Native `windowsIn`/`windowsOut` *scaling* is neutralised during Liquid transitions; native *translation* (slide) is kept.
4. Native `fadeOut` no longer controls Liquid close alpha or duration.

## Files

- Create: `src/DropletTransition.hpp`, `src/DropletTransition.cpp`, `tests/DropletTransitionTests.cpp`
- Modify: `CMakeLists.txt`, `src/LiquidMesh.hpp`, `src/LiquidMesh.cpp`, `tests/LiquidMeshTests.cpp`, `src/LiquidGlassTransformer.hpp`, `src/LiquidGlassTransformer.cpp`, `src/PluginRuntime.hpp`, `src/PluginRuntime.cpp`, `src/LiquidCloseRenderer.hpp`, `src/LiquidCloseRenderer.cpp`, `README.md`, `.project-manager/progress.md`

Build/test commands used throughout:

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/c++ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## Phase A — Droplet model (pure, unit-tested)

### Task 1: Register the new sources and test target

**Objective:** Make the droplet compile into the plugin and its own CTest executable.

**Files:** Modify `CMakeLists.txt:13-31`

**Step 1:** Add `src/DropletTransition.cpp` to `add_library(liquid-glass SHARED …)` after `src/LiquidMesh.cpp`.

**Step 2:** Inside `if(BUILD_TESTING)` add:

```cmake
    add_executable(droplet-transition-tests tests/DropletTransitionTests.cpp src/DropletTransition.cpp)
    target_include_directories(droplet-transition-tests PRIVATE src)
    target_compile_options(droplet-transition-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME droplet-transition-tests COMMAND droplet-transition-tests)
```

(The build will fail until Tasks 2–4 exist; that's expected.)

### Task 2: Declare `DropletTransition` and move `Transition` into it

**Objective:** Pin down the public API the tests and renderers use.

**Files:** Create `src/DropletTransition.hpp`; modify `src/LiquidMesh.hpp:24` (delete `enum class Transition { Open, Close };`; it moves here).

```cpp
#pragma once

#include "LiquidMesh.hpp"

namespace liquid_glass {

enum class Transition { Open, Close };

// Surface-tension droplet lifecycle, drawn on top of the movement field. Open swells a round bead into the
// rest rectangle with one soft rebound; Close rounds the corners, beads up, swells and collapses to a point.
// The outline map is radially homogeneous with a positive gain, so it never folds the image, and it is not
// bounded by the movement field's displacement limit. Logical pixels; no compositor dependency.
class DropletTransition {
  public:
    // Starts a transition. Invalid size, parameters or non-positive strength leave it idle (identity).
    void begin(Transition kind, Vec2 size, const MaterialParams& params, double strength) noexcept;
    // Fixed 1/240 s substeps. Nonfinite, negative or > 0.25 s steps abort: Open returns to idle, Close to closed.
    bool advance(double elapsedSeconds, Vec2 size) noexcept;
    void reset() noexcept;
    // Opening or Closing.
    bool active() const noexcept;
    // A close that finished or aborted: draw nothing.
    bool closed() const noexcept;
    // Opacity multiplier: 1 while idle/opening, fades during the close collapse, 0 once closed.
    double alpha() const noexcept;
    // Maps a point given relative to the window centre (logical px) to its displayed position relative to the
    // centre. Exact identity unless active.
    Vec2 map(Vec2 fromCentre, Vec2 size) const noexcept;

  private:
    enum class Phase { Idle, Opening, Closing, Closed };
    struct Spring {
        double value    = 0.0;
        double velocity = 0.0;
    };
    struct Outline {
        double spreadX, spreadY; // 0 = bead extent, 1 = rest extent (may overshoot)
        double scale;            // uniform multiplier (close swell/collapse)
        double roundness;        // 1 = superellipse n=2 (circle), 0 = rectangle
        double ripple;           // signed 4-lobe outline wave amplitude
    };

    Outline outline() const noexcept;
    Vec2    beadExtent(Vec2 half) const noexcept;
    void    step(Vec2 size) noexcept;
    void    abort() noexcept;

    Phase  m_phase       = Phase::Idle;
    double m_bead        = 1.0; // bead radius / half shorter side
    double m_omega       = 0.0;
    double m_zeta        = 1.0;
    Spring m_spreadX{};
    Spring m_spreadY{};
    double m_rippleGain  = 0.0;
    double m_rippleTime  = -1.0; // < 0: not excited yet
    double m_time        = 0.0;  // Close: elapsed seconds
    double m_duration    = 0.0;  // Close: total seconds
    double m_accumulator = 0.0;
};

} // namespace liquid_glass
```

### Task 3: Write the failing droplet tests

**Objective:** Turn the design (bold bead, one rebound, beading close, no folding, cadence independence) into checks.

**Files:** Create `tests/DropletTransitionTests.cpp`

```cpp
#include "DropletTransition.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using liquid_glass::DropletTransition;
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

constexpr Vec2   kWindow{800.0, 500.0};
constexpr double kStrength = 0.65;
constexpr double kNaN      = std::numeric_limits<double>::quiet_NaN();

double halfShort(Vec2 s) { return 0.5 * std::min(s.x, s.y); }
double length(Vec2 v) { return std::hypot(v.x, v.y); }
Vec2   corner(const DropletTransition& d, Vec2 s) { return d.map({0.5 * s.x, 0.5 * s.y}, s); }
Vec2   edgeX(const DropletTransition& d, Vec2 s) { return d.map({0.5 * s.x, 0.0}, s); }
Vec2   edgeY(const DropletTransition& d, Vec2 s) { return d.map({0.0, 0.5 * s.y}, s); }

// Every triangle of a 32x32 grid over the window keeps positive orientation: the image never folds.
bool oriented(const DropletTransition& d, Vec2 s) {
    constexpr int N = 32;
    const auto at = [&](int i, int j) { return d.map({s.x * (double(i) / N - 0.5), s.y * (double(j) / N - 0.5)}, s); };
    const auto cross = [](Vec2 a, Vec2 b, Vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            const Vec2 a = at(i, j), b = at(i + 1, j), c = at(i, j + 1), e = at(i + 1, j + 1);
            if (!(cross(a, b, c) > 0.0) || !(cross(b, e, c) > 0.0))
                return false;
        }
    }
    return true;
}

// Open starts as a small round bead, not a scaled rectangle.
void testOpenStartsAsRoundBead() {
    DropletTransition d;
    d.begin(Transition::Open, kWindow, MaterialParams{}, kStrength);
    CHECK(d.active());
    const double rc = length(corner(d, kWindow));
    const double rx = length(edgeX(d, kWindow));
    const double ry = length(edgeY(d, kWindow));
    CHECK(std::abs(rc - rx) <= 0.01 * rx);
    CHECK(std::abs(rc - ry) <= 0.01 * ry);
    CHECK(rx >= 0.2 * halfShort(kWindow));
    CHECK(rx <= 0.35 * halfShort(kWindow));
    CHECK(d.alpha() == 1.0);
}

// Open swells to the rectangle with exactly one soft rebound past rest, then settles to exact identity.
void testOpenSwellsWithOneSoftRebound() {
    DropletTransition d;
    d.begin(Transition::Open, kWindow, MaterialParams{}, kStrength);
    const double half    = 0.5 * kWindow.x;
    double       peak    = 0.0;
    double       elapsed = 0.0;
    int          rebounds = 0;
    bool         above    = false;
    while (d.active() && elapsed < 2.0) {
        d.advance(1.0 / 60.0, kWindow);
        elapsed += 1.0 / 60.0;
        if (!d.active())
            break;
        CHECK(oriented(d, kWindow));
        // Corner and edge midpoint cancel the 4-lobe ripple once the corners are square.
        const double reach   = 0.5 * (corner(d, kWindow).x + edgeX(d, kWindow).x);
        peak                 = std::max(peak, reach);
        const bool nowAbove  = reach > 1.01 * half;
        rebounds += nowAbove && !above;
        above = nowAbove;
    }
    CHECK(peak >= 1.03 * half);
    CHECK(peak <= 1.10 * half);
    CHECK(rebounds == 1);
    CHECK(elapsed <= 0.8);
    CHECK(!d.active());
    const Vec2 rest = corner(d, kWindow);
    CHECK(rest.x == 0.5 * kWindow.x && rest.y == 0.5 * kWindow.y);
}

void testOpenRefreshRateAgreement() {
    const auto sampleAt = [](double hz) {
        DropletTransition d;
        d.begin(Transition::Open, kWindow, MaterialParams{}, kStrength);
        const long frames = std::lround(hz / 6.0); // 1/6 s, an integer frame count at 30/60/144/240 Hz
        for (long f = 0; f < frames; ++f)
            d.advance(1.0 / hz, kWindow);
        return corner(d, kWindow);
    };
    const Vec2 reference = sampleAt(240.0);
    for (double hz : {30.0, 60.0, 144.0}) {
        const Vec2 c = sampleAt(hz);
        CHECK(length({c.x - reference.x, c.y - reference.y}) <= 0.02 * halfShort(kWindow));
    }
}

void testStrengthScalesBead() {
    const auto beadAt = [](double strength) {
        DropletTransition d;
        d.begin(Transition::Open, kWindow, MaterialParams{}, strength);
        return length(edgeX(d, kWindow));
    };
    CHECK(beadAt(0.3) > beadAt(0.65));
    CHECK(beadAt(0.65) > beadAt(1.0));
    DropletTransition none;
    none.begin(Transition::Open, kWindow, MaterialParams{}, 0.0);
    CHECK(!none.active());
    const Vec2 p{123.0, -45.0};
    CHECK(none.map(p, kWindow).x == p.x && none.map(p, kWindow).y == p.y);
}

// Close starts at rest, beads into a round drop while opaque, then collapses toward a point as it fades.
void testCloseBeadsUpAndCollapses() {
    double total = 0.0;
    {
        DropletTransition probe;
        probe.begin(Transition::Close, kWindow, MaterialParams{}, kStrength);
        while (probe.active() && total < 2.0) {
            probe.advance(1.0 / 240.0, kWindow);
            total += 1.0 / 240.0;
        }
    }
    CHECK(total >= 0.38 && total <= 0.46);

    DropletTransition d;
    d.begin(Transition::Close, kWindow, MaterialParams{}, kStrength);
    const Vec2 start = corner(d, kWindow);
    CHECK(length({start.x - 0.5 * kWindow.x, start.y - 0.5 * kWindow.y}) <= 0.5);
    double elapsed = 0.0, lastAlpha = 1.0;
    bool   checkedBead = false;
    while (d.active()) {
        d.advance(1.0 / 240.0, kWindow);
        elapsed += 1.0 / 240.0;
        if (!d.active())
            break;
        CHECK(oriented(d, kWindow));
        CHECK(d.alpha() <= lastAlpha);
        lastAlpha = d.alpha();
        if (elapsed < 0.65 * total)
            CHECK(d.alpha() == 1.0);
        if (!checkedBead && elapsed >= 0.6 * total) {
            checkedBead     = true;
            const double rc = length(corner(d, kWindow));
            const double rx = length(edgeX(d, kWindow));
            const double ry = length(edgeY(d, kWindow));
            CHECK(std::abs(rc - rx) <= 0.02 * rx && std::abs(rc - ry) <= 0.02 * ry);
            CHECK(rx <= 0.4 * halfShort(kWindow));
        }
        if (elapsed >= 0.98 * total)
            CHECK(length(corner(d, kWindow)) <= 0.1 * halfShort(kWindow));
    }
    CHECK(checkedBead);
    CHECK(d.closed());
    CHECK(d.alpha() == 0.0);
}

void testCloseCadenceIndependent() {
    const auto durationAt = [](double hz) {
        DropletTransition d;
        d.begin(Transition::Close, kWindow, MaterialParams{}, kStrength);
        double elapsed = 0.0;
        while (d.active() && elapsed < 2.0) {
            d.advance(1.0 / hz, kWindow);
            elapsed += 1.0 / hz;
        }
        return elapsed;
    };
    const double reference = durationAt(240.0);
    for (double hz : {30.0, 60.0, 144.0})
        CHECK(std::abs(durationAt(hz) - reference) <= 1.0 / hz + 1e-6);
}

void testExtremeSizesStayOriented() {
    for (Vec2 size : {Vec2{2000.0, 60.0}, Vec2{60.0, 2000.0}, Vec2{8.0, 8.0}, Vec2{1.0, 1.0}}) {
        for (Transition kind : {Transition::Open, Transition::Close}) {
            DropletTransition d;
            d.begin(kind, size, MaterialParams{}, 1.5);
            double elapsed = 0.0;
            while (d.active() && elapsed < 2.0) {
                CHECK(oriented(d, size));
                d.advance(1.0 / 60.0, size);
                elapsed += 1.0 / 60.0;
            }
            CHECK(!d.active());
        }
    }
}

void testNonfiniteAndGaps() {
    DropletTransition d;
    d.begin(Transition::Open, Vec2{kNaN, 100.0}, MaterialParams{}, kStrength);
    CHECK(!d.active());
    d.begin(Transition::Open, kWindow, MaterialParams{}, kNaN);
    CHECK(!d.active());
    MaterialParams bad{};
    bad.elasticity = kNaN;
    d.begin(Transition::Open, kWindow, bad, kStrength);
    CHECK(!d.active());

    d.begin(Transition::Open, kWindow, MaterialParams{}, kStrength);
    d.advance(kNaN, kWindow);
    CHECK(!d.active());
    CHECK(!d.closed());
    const Vec2 p{10.0, 20.0};
    CHECK(d.map(p, kWindow).x == p.x && d.map(p, kWindow).y == p.y);

    d.begin(Transition::Close, kWindow, MaterialParams{}, kStrength);
    d.advance(1.0, kWindow); // longer than the gap limit
    CHECK(!d.active());
    CHECK(d.closed());
    CHECK(d.alpha() == 0.0);
}

} // namespace

int main() {
    testOpenStartsAsRoundBead();
    testOpenSwellsWithOneSoftRebound();
    testOpenRefreshRateAgreement();
    testStrengthScalesBead();
    testCloseBeadsUpAndCollapses();
    testCloseCadenceIndependent();
    testExtremeSizesStayOriented();
    testNonfiniteAndGaps();
    if (g_failures == 0) {
        std::printf("droplet-transition-tests: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "droplet-transition-tests: %d check(s) failed\n", g_failures);
    return 1;
}
```

**Run:** `cmake --build build --target droplet-transition-tests`
**Expected:** link failure (`DropletTransition::begin` undefined). That's the red state.

### Task 4: Implement `DropletTransition`

**Objective:** Make Task 3 pass.

**Files:** Create `src/DropletTransition.cpp`

```cpp
#include "DropletTransition.hpp"

#include <algorithm>
#include <cmath>

namespace liquid_glass {
namespace {

constexpr double kSubstepSeconds       = 1.0 / 240.0;
constexpr double kAccumulatorTolerance = 1e-9;
constexpr double kGapSeconds           = 0.25; // matches the movement field's gap reset

// Open: spread springs (0 = bead, 1 = rest). y spreads later than x, so the bead passes through an oval.
constexpr double kOpenOmega       = 20.0; // rad/s at elasticity 0.5, scaled by 2^(elasticity - 0.5)
constexpr double kOpenOmegaYRatio = 0.86;
constexpr double kOpenZetaStiff   = 0.85; // momentum 0
constexpr double kOpenZetaLoose   = 0.45; // momentum 1
// Bead radius as a fraction of the half shorter side.
constexpr double kBeadPerStrength = 1.08;
constexpr double kBeadMin         = 0.12;
// Surface-tension ripple: decaying 4-lobe outline wave, excited once as the corners square off.
constexpr double kRippleTrigger    = 0.97;
constexpr double kRippleAmplitude  = 0.03; // at momentum 0.5; scales linearly with momentum
constexpr double kRippleOmegaRatio = 2.2;
constexpr double kRippleSeconds    = 0.12;
// Close timeline, as fractions of the duration.
constexpr double kCloseSeconds        = 0.42; // at elasticity 0.5, scaled by 2^(0.5 - elasticity)
constexpr double kCloseBeadEnd        = 0.55;
constexpr double kCloseLagY           = 0.06;
constexpr double kCloseSwellEnd       = 0.78;
constexpr double kCloseSwell          = 0.12;
constexpr double kCloseFadeStart      = 0.7;
// Below this roundness the outline is the rectangle itself (superellipse exponent 2 / roundness > 1e4).
constexpr double kSquareRoundness = 2e-4;
// Settling: the movement field's pixel thresholds.
constexpr double kQuietPixels          = 0.15;
constexpr double kQuietPixelsPerSecond = 2.0;

double smooth(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double easeInOutCubic(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

bool validSize(Vec2 s) {
    return std::isfinite(s.x) && std::isfinite(s.y) && s.x > 0.0 && s.y > 0.0;
}

} // namespace

void DropletTransition::begin(Transition kind, Vec2 size, const MaterialParams& params, double strength) noexcept {
    reset();
    if (!validSize(size) || !std::isfinite(params.elasticity) || !std::isfinite(params.momentum) || !std::isfinite(strength) || strength <= 0.0)
        return;

    const double elasticity = std::clamp(params.elasticity, 0.0, 1.0);
    const double momentum   = std::clamp(params.momentum, 0.0, 1.0);
    m_bead                  = std::max(kBeadMin, 1.0 - kBeadPerStrength * std::min(strength, 1.5));

    if (kind == Transition::Open) {
        m_omega      = kOpenOmega * std::exp2(elasticity - 0.5);
        m_zeta       = kOpenZetaStiff + (kOpenZetaLoose - kOpenZetaStiff) * momentum;
        m_rippleGain = kRippleAmplitude * 2.0 * momentum;
        m_phase      = Phase::Opening;
    } else {
        m_duration = kCloseSeconds * std::exp2(0.5 - elasticity);
        m_phase    = Phase::Closing;
    }
}

void DropletTransition::reset() noexcept {
    *this = DropletTransition{};
}

void DropletTransition::abort() noexcept {
    const bool closing = m_phase == Phase::Closing;
    reset();
    if (closing)
        m_phase = Phase::Closed;
}

bool DropletTransition::active() const noexcept {
    return m_phase == Phase::Opening || m_phase == Phase::Closing;
}

bool DropletTransition::closed() const noexcept {
    return m_phase == Phase::Closed;
}

double DropletTransition::alpha() const noexcept {
    if (m_phase == Phase::Closed)
        return 0.0;
    if (m_phase != Phase::Closing)
        return 1.0;
    const double p = std::clamp(m_time / m_duration, 0.0, 1.0);
    return 1.0 - smooth(kCloseFadeStart, 1.0, p);
}

bool DropletTransition::advance(double elapsedSeconds, Vec2 size) noexcept {
    if (!active())
        return false;
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 || elapsedSeconds > kGapSeconds || !validSize(size)) {
        abort();
        return false;
    }
    m_accumulator += elapsedSeconds;
    while (active() && m_accumulator >= kSubstepSeconds - kAccumulatorTolerance) {
        m_accumulator -= kSubstepSeconds;
        step(size);
    }
    return active();
}

Vec2 DropletTransition::beadExtent(Vec2 half) const noexcept {
    const double radius = m_bead * std::min(half.x, half.y);
    return {radius, radius};
}

void DropletTransition::step(Vec2 size) noexcept {
    if (m_phase == Phase::Closing) {
        m_time += kSubstepSeconds;
        if (m_time >= m_duration - kAccumulatorTolerance)
            m_phase = Phase::Closed;
        return;
    }

    // Semi-implicit Euler damped springs toward 1 (stable: omega * dt <= 0.12).
    const auto spring = [this](Spring& s, double omega) {
        const double accel = omega * omega * (1.0 - s.value) - 2.0 * m_zeta * omega * s.velocity;
        s.velocity += accel * kSubstepSeconds;
        s.value += s.velocity * kSubstepSeconds;
    };
    spring(m_spreadX, m_omega);
    spring(m_spreadY, m_omega * kOpenOmegaYRatio);
    if (m_rippleTime >= 0.0)
        m_rippleTime += kSubstepSeconds;
    else if (m_rippleGain > 0.0 && std::min(m_spreadX.value, m_spreadY.value) >= kRippleTrigger)
        m_rippleTime = 0.0;

    const Vec2   half{0.5 * size.x, 0.5 * size.y};
    const Vec2   bead   = beadExtent(half);
    const Vec2   travel{half.x - bead.x, half.y - bead.y};
    const double offset = std::max(travel.x * std::abs(1.0 - m_spreadX.value), travel.y * std::abs(1.0 - m_spreadY.value));
    const double speed  = std::max(travel.x * std::abs(m_spreadX.velocity), travel.y * std::abs(m_spreadY.velocity));
    const double ripple = m_rippleTime >= 0.0 ? std::max(half.x, half.y) * m_rippleGain * std::exp(-m_rippleTime / kRippleSeconds) : 0.0;
    if (offset < kQuietPixels && speed < kQuietPixelsPerSecond && ripple < kQuietPixels)
        reset();
}

DropletTransition::Outline DropletTransition::outline() const noexcept {
    if (m_phase == Phase::Opening) {
        const double spread    = std::clamp(std::min(m_spreadX.value, m_spreadY.value), 0.0, 1.0);
        const double roundness = std::pow(1.0 - spread, 1.5);
        const double ripple    = m_rippleTime >= 0.0
               ? m_rippleGain * std::exp(-m_rippleTime / kRippleSeconds) * std::cos(kRippleOmegaRatio * m_omega * m_rippleTime)
               : 0.0;
        return {m_spreadX.value, m_spreadY.value, 1.0, roundness, ripple};
    }

    const double p = std::clamp(m_time / m_duration, 0.0, 1.0);
    const double beadX    = easeInOutCubic(p / kCloseBeadEnd);
    const double beadY    = easeInOutCubic((p - kCloseLagY) / kCloseBeadEnd);
    const double swell    = 1.0 + kCloseSwell * smooth(kCloseBeadEnd, kCloseSwellEnd, p);
    const double collapse = std::clamp((p - kCloseSwellEnd) / (1.0 - kCloseSwellEnd), 0.0, 1.0);
    return {1.0 - beadX, 1.0 - beadY, swell * (1.0 - collapse * collapse), beadX, 0.0};
}

Vec2 DropletTransition::map(Vec2 p, Vec2 size) const noexcept {
    if (!active() || !validSize(size))
        return p;

    const Outline o = outline();
    const Vec2    half{0.5 * size.x, 0.5 * size.y};
    const Vec2    bead = beadExtent(half);
    const Vec2    extent{o.scale * (bead.x + (half.x - bead.x) * o.spreadX), o.scale * (bead.y + (half.y - bead.y) * o.spreadY)};

    const Vec2   q{p.x / half.x, p.y / half.y};
    const double ax   = std::abs(q.x);
    const double ay   = std::abs(q.y);
    const double linf = std::max(ax, ay);
    if (linf == 0.0)
        return {0.0, 0.0};

    // Radial gain taking the unit square onto the superellipse |x|^n + |y|^n = 1.
    double k = 1.0;
    if (o.roundness > kSquareRoundness) {
        const double n = 2.0 / o.roundness;
        k              = 1.0 / std::pow(std::pow(ax / linf, n) + std::pow(ay / linf, n), 1.0 / n);
    }
    if (o.ripple != 0.0) {
        const double c2 = ax * ax / (ax * ax + ay * ay); // cos^2 of the direction
        k *= 1.0 + o.ripple * (8.0 * c2 * c2 - 8.0 * c2 + 1.0); // cos(4θ); |ripple| <= 0.06 keeps k > 0
    }
    return {extent.x * q.x * k, extent.y * q.y * k};
}

} // namespace liquid_glass
```

**Run:** `cmake --build build --target droplet-transition-tests && build/droplet-transition-tests`
**Expected:** `droplet-transition-tests: all checks passed`.

**Tuning rule:** if a check fails, adjust the **constants** (not the test bounds). The bounds encode the design: bead 20–35% of the half shorter side, rebound 3–10%, exactly one rebound, open settles ≤ 0.8 s, close 0.38–0.46 s, opaque until 65%. Expected values at defaults: bead ≈ 0.298·250 ≈ 75 px; spread overshoot ≈ 7.5% of travel (ζ ≈ 0.635); open quiet ≈ 0.68 s; close 0.42 s.

---

## Phase B — Remove the old pose machinery from `LiquidMesh` (clean cutover)

### Task 5: Delete the old transition tests

**Objective:** Stop pinning the removed pillow pose.

**Files:** Modify `tests/LiquidMeshTests.cpp`

- Delete `testLiquidOpenTransition` (`:628-649`), `testLiquidCloseTransition` (`:651-686`), `testLiquidOpenOvershootWindow` (`:906-933`) and their calls in `main` (`:956-958`).
- Delete `using liquid_glass::Transition;` (`:18`).

### Task 6: Strip `recordTransition`, hold and target state

**Objective:** `LiquidMesh` becomes movement-only.

**Files:** Modify `src/LiquidMesh.hpp`, `src/LiquidMesh.cpp`

- `LiquidMesh.hpp`: remove `recordTransition` (`:35-36`), `holdingTarget` (`:39-40`), `snapToTarget` (`:49`), `m_shearScale` (`:55-56`), `m_target` (`:66-67`), `m_holdSteps` and `m_holdReference` (`:87-90`).
- `LiquidMesh.cpp`:
  - Delete `kLiquidOpenShearScale` (`:54-55`), `kLiquidTransitionScale` (`:71-72`), `recordTransition` (`:375-433`), `snapToTarget` (`:435-439`), `holdingTarget` (`:686-688`).
  - In `reset()` and `recordTranslation()` delete the `m_target.fill`, `m_shearScale = 1.0`, `m_holdSteps = 0` lines (`:306,316,320,365,368`).
  - `integrateLiquidSubstep`: the relative field `e` was `d − m_target`; replace `:464` with `e[i] = d[i];`, and `:489` becomes `const double shearDamping = m_shearDamping;`.
  - `advance()`: delete the `holdCandidate` block (`:624-626`, `:636-648`). Compute the quiet check on `m_displacements` directly (replace `:652-655` with `quiet = isQuiet(m_displacements, m_velocities);`, shared by both modes). Replace the snap/reset branch (`:662-668`) with `reset(); return false;`.
  - `settled()` (`:681-682`): `return isZero(m_windowVelocity) && isZero(m_pendingDelta);` for Liquid.
  - Update the comment above `integrateLiquidSubstep` (`:441-446`) to drop "target".

**Run:** `cmake --build build --target liquid-mesh-tests && build/liquid-mesh-tests`
**Expected:** `liquid-mesh-tests: all checks passed`. Movement behaviour is unchanged because `m_target` was zero outside transitions.

---

## Phase C — Open: transformer renders the droplet and owns popin scale

### Task 7: Shared render pose

**Objective:** One code path composes lattice → open compensation → droplet for both the transformer and the close renderer.

**Files:** Modify `src/LiquidGlassTransformer.hpp:18-26`, `src/LiquidGlassTransformer.cpp:29-79`

Header:

```cpp
#include "DropletTransition.hpp"

// What the render mesh draws: the movement field, the lifecycle droplet, and an output-size compensation about
// the box centre (cancels native open scaling while the plugin owns the open geometry).
struct SLiquidPose {
    const liquid_glass::LiquidMesh&        mesh;
    const liquid_glass::DropletTransition& droplet;
    liquid_glass::Vec2                     compensation{1.0, 1.0};
};

CBox liquidRenderExtents(const SLiquidPose& pose, const CBox& logicalBox);
void fillLiquidRenderMesh(const SLiquidPose& pose, const CBox& sourceDevice, double scale, const CBox& outputDevice,
                          liquid_glass::Vec2 textureOrigin, liquid_glass::Vec2 textureSize, std::vector<SMeshRenderVertex>& out);
```

Source: add to the anonymous namespace:

```cpp
// Displayed offset of rest sample (u, v) from the box centre, logical px.
Vec2 posedOffset(const SLiquidPose& pose, double u, double v, Vec2 size) {
    const Vec2 d = pose.mesh.sampleDisplacement(u, v);
    const Vec2 c = pose.compensation;
    return pose.droplet.map({((u - 0.5) * size.x + d.x) * c.x, ((v - 0.5) * size.y + d.y) * c.y}, {size.x * c.x, size.y * c.y});
}
```

- `liquidRenderExtents`: `size = {w, h}`, `centre = box centre`; each sample point becomes `centre + posedOffset(pose, su, sv, size)` (keep the union with the undeformed box).
- `fillLiquidRenderMesh`: `size = sourceDevice.size / scale`, `centre = sourceDevice centre`; `posX = centre.x + o.x * scale`, `posY = centre.y + o.y * scale`. `u`/`v` keep using `restX`/`restY`. With an idle droplet and `c = {1,1}` this is the old formula exactly (`restX + d.x·scale`).

### Task 8: Transformer owns the droplet and open compensation

**Files:** Modify `src/LiquidGlassTransformer.hpp:44-55`, `src/LiquidGlassTransformer.cpp:84-147`

Header additions:

```cpp
    liquid_glass::DropletTransition&       droplet() noexcept { return m_droplet; }
    const liquid_glass::DropletTransition& droplet() const noexcept { return m_droplet; }
    // Starts the open droplet and takes over native open scaling while the window animates in.
    void beginOpen(liquid_glass::Vec2 size, const liquid_glass::MaterialParams& params, double strength);

    void markDetached() noexcept { m_detached = true; m_mesh.reset(); m_droplet.reset(); m_openGeometry = false; }

  private:
    liquid_glass::Vec2 openCompensation(const PHLWINDOW& window) const;
    SLiquidPose        pose() const;
    liquid_glass::DropletTransition m_droplet;
    bool                            m_openGeometry = false;
```

Source:

```cpp
void CLiquidGlassTransformer::beginOpen(Vec2 size, const liquid_glass::MaterialParams& params, double strength) {
    m_droplet.begin(liquid_glass::Transition::Open, size, params, strength);
    m_openGeometry = m_droplet.active();
}

// Native popin animates the current size toward the goal about the centre; scaling by goal/current cancels it.
Vec2 CLiquidGlassTransformer::openCompensation(const PHLWINDOW& window) const {
    if (!m_openGeometry || !window->presentation().animatingIn())
        return {1.0, 1.0};
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox goal    = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL);
    if (!(current.w > 0.0 && current.h > 0.0 && std::isfinite(goal.w) && std::isfinite(goal.h) && goal.w > 0.0 && goal.h > 0.0))
        return {1.0, 1.0};
    return {std::clamp(goal.w / current.w, 0.25, 4.0), std::clamp(goal.h / current.h, 0.25, 4.0)};
}

SLiquidPose CLiquidGlassTransformer::pose() const {
    const auto window = m_window.lock();
    return SLiquidPose{m_mesh, m_droplet, window ? openCompensation(window) : Vec2{1.0, 1.0}};
}
```

- `active()` (`:84-90`): replace `m_mesh.settled()` in the first guard with a separate check after the window/fullscreen guard:
  `if (m_mesh.settled() && !m_droplet.active() && !(m_openGeometry && window->presentation().animatingIn())) return false;`
- `transformedExtents` (`:104`): `liquidRenderExtents(pose(), currentBox)`.
- `transform` (`:147`): `fillLiquidRenderMesh(pose(), SOURCEBOX, …)`.

### Task 9: Runtime starts and advances the open droplet

**Files:** Modify `src/PluginRuntime.cpp:360-366`, `:419-420`

- `beginOpening`:

```cpp
    if (!transitionsEnabled() || effectiveMode() != liquid_glass::MotionMode::Liquid)
        return;

    auto* transformer = acquireTransformer(window);
    transformer->beginOpen({captureSize.x, captureSize.y}, effectiveMaterialParams(), effectiveStrength());
```

- `advanceWindow` (`:419`):

```cpp
    const liquid_glass::Vec2 size{capture.w, capture.h};
    transformer->mesh().advance(elapsed, size);
    transformer->droplet().advance(elapsed, size);
    const bool stillActive = transformer->active();
```

Check `PluginRuntime.hpp` for any `Transition` include or usage that now needs `DropletTransition.hpp`.

**Run:** `cmake --build build`
**Expected:** links with no warnings in the changed files. (The close renderer still calls `recordTransition`; finish Task 10 before this build passes.)

---

## Phase D — Close: owned droplet that outlives the native fade

### Task 10: Close record state

**Objective:** Records carry everything needed to keep drawing after the native fadeout ends.

**Files:** Modify `src/LiquidCloseRenderer.hpp:32-43,79-99`

```cpp
    struct SClosing {
        WP<Desktop::IFadeout>           fadeout;
        SP<Render::IFramebuffer>        snapshot;   // retained: an owned close may outlive the native fadeout
        PHLMONITORREF                   monitor;
        PHLWORKSPACEREF                 workspace;  // verify the alias against fadeout->workspace()'s return type
        Desktop::eFadeoutPlane          plane = Desktop::FADEOUT_PLANE_WINDOW_TILED; // MONITOR-mode scene plane
        int                             zIndex = 0;
        bool                            noScreenShare = false;
        liquid_glass::LiquidMesh        mesh;
        liquid_glass::DropletTransition droplet;
        bool                            owned = false; // droplet drives shape, alpha, timing and placement scale
        float                           sourceAlpha = 1.F;
        CBox                            sourceDevice;
        double                          scale = 1.0;
        liquid_glass::Vec2              captureLogical{};
        Vector2D                        snapshotSize{};
        CBox                            initialRenderBox; // native render box at creation, device px
        CBox                            renderBox;        // owned placement: native scaling removed, centre follows native
        CBox                            extentsDevice;
        std::vector<SMeshRenderVertex>  vertices;
        clock::time_point               lastAdvance{};
    };
```

Batch scratch:

```cpp
    struct SBatchItem {
        SP<Desktop::IFadeout> fadeout; // null for an owned record drawn after its native fadeout ended
        SClosing*             record = nullptr;
        int                   zIndex = 0;
    };
    std::vector<SBatchItem> m_batch;
```

Update the `tick` doc comment: "Returns true while any record still animates." Delete the "holding their final pose" sentence.

### Task 11: Create owned records

**Files:** Modify `src/LiquidCloseRenderer.cpp:203-245` (and add a helper to the anonymous namespace near `projectToOutput`, `:60`)

Helper:

```cpp
// Native placement with windowsOut scaling removed: the creation-time render box, translated so the window
// centre follows the native (possibly sliding) centre.
CBox ownedRenderBox(const CBox& initial, const CBox& current, const CBox& sourceDevice, const Vector2D& snapshotSize) {
    const Vector2D mid = sourceDevice.middle();
    const Vector2D now{current.x + mid.x * current.w / snapshotSize.x, current.y + mid.y * current.h / snapshotSize.y};
    const Vector2D was{initial.x + mid.x * initial.w / snapshotSize.x, initial.y + mid.y * initial.h / snapshotSize.y};
    return initial.copy().translate(now - was);
}
```

In `onFadeoutCreated`, after the existing validation:

```cpp
    if (m_records.size() >= m_batch.capacity())
        return; // bounds retained snapshots; extra closes draw natively
    …
    record->snapshot         = snapshot;
    record->workspace        = created->workspace();
    record->plane            = Render::sceneFadeoutPlane(Render::eSceneMode::MONITOR, created->plane(), created->source().pinned);
    record->zIndex           = created->zIndex();
    record->noScreenShare    = created->source().noScreenShare;
    record->sourceAlpha      = created->alpha();
    record->initialRenderBox = created->renderBox();
    record->renderBox        = record->initialRenderBox;
    if (record->mesh.mode() == liquid_glass::MotionMode::Liquid && !created->effects().textureBlur.enabled)
        record->droplet.begin(Transition::Close, pending.captureLogical, record->mesh.params(), pending.strength);
    record->owned = record->droplet.active() && !record->initialRenderBox.empty() && record->sourceAlpha > 0.F;
```

Delete `record->mesh.recordTransition(…)` (`:231`). In `fillVertices` build `const SLiquidPose pose{record.mesh, record.droplet};` and pass it to both helpers.

### Task 12: Tick owned records to completion

**Files:** Modify `src/LiquidCloseRenderer.cpp:247-272`

Replace the `erase_if` body:

```cpp
        const auto fadeout     = record->fadeout.lock();
        const bool nativeAlive = fadeout && !fadeout->done();
        const auto monitor     = record->monitor.lock();
        if (!monitor || monitor->m_scale != record->scale)
            return true;

        if (record->droplet.closed())
            return !nativeAlive; // finished: keep suppressing the native fade until it ends, then drop
        if (!record->owned) {
            if (!nativeAlive)
                return true;
            if (record->mesh.settled())
                return false;
        } else if (nativeAlive) {
            record->renderBox = ownedRenderBox(record->initialRenderBox, fadeout->renderBox(), record->sourceDevice, record->snapshotSize);
        }

        const double elapsed = std::clamp(std::chrono::duration<double>(now - record->lastAdvance).count(), 0.0, MAX_GAP_SECONDS);
        record->mesh.advance(elapsed, record->captureLogical);
        record->droplet.advance(elapsed, record->captureLogical);
        record->lastAdvance = now;
        fillVertices(*record);
        activeMonitors.push_back(PHLMONITORREF{monitor}); // also damages the frame where the droplet closes
        return false;
```

The tick in which the droplet becomes closed still damages the monitor, so the next frame draws nothing and clears the last pixels. The next tick drops the record, or keeps it as an invisible native suppressor while the native fade continues.

### Task 13: Draw owned records, including orphans

**Files:** Modify `src/LiquidCloseRenderer.cpp:282-387`, plus `m_batch.reserve(16)` at `:133` (unchanged capacity, new element type)

- `hasRecordFor`: match on captured fields:

```cpp
    for (const auto& record : m_records) {
        if (!sameMonitor(record->monitor, monitor) || record->plane != plane)
            continue;
        const auto ws = record->workspace.lock();
        if (ws && ws != workspace)
            continue;
        return true;
    }
    return false;
```

- `renderBatch` collection:
  1. Native fadeouts, as today, push `{fadeout, recordFor(fadeout), fadeout->zIndex()}`.
  2. Then, for each record with `owned && !droplet.closed()` whose fadeout is expired or `done()`, matching monitor/plane/workspace and `ctx.shouldRenderContent(record->noScreenShare)`, push `{nullptr, record.get(), record->zIndex}`.
  3. Keep the capacity guard on every push (clear, return false).
  4. Sort by `zIndex`.
- Drawing loop per item:

```cpp
        SClosing* record = item.record;
        if (record && record->owned && sameMonitor(record->monitor, monitor)) {
            if (record->droplet.closed())
                continue; // finished: draw nothing and keep the native fade suppressed
            if (item.fadeout) {
                // dim-around and pre-blur exactly as native (existing code), only while the native fadeout lives
            }
            CLiquidClosePassElement::SData data;
            data.tex      = record->snapshot->getTexture();
            data.box      = projectToOutput(record->extentsDevice, record->renderBox, record->snapshotSize);
            data.vertices = &record->vertices;
            data.alpha    = static_cast<float>(record->sourceAlpha * record->droplet.alpha());
            data.scale    = record->scale;
            data.damage   = fakeDamage;
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CLiquidClosePassElement>(std::move(data)));
            continue;
        }
        // non-owned: existing code (wiggle mesh with native renderBox/alpha, or native texture draw)
```

  The existing `FB`/texture-size guard applies to native items; for owned items, guard `record->snapshot && record->snapshot->getTexture()` and the size match.

**Run:** `cmake --build build && ctest --test-dir build --output-on-failure`
**Expected:** both test executables pass and the plugin links. `grep -n "recordTransition\|holdingTarget\|m_target" src tests -r` returns nothing.

---

## Phase E — Visual verification (nested only)

### Task 14: Launch the nested fixture and load the plugin

Follow README "Isolated nested verification" exactly:

1. Launch `Hyprland --config tests/nested/hyprland.lua` with the `env -u …` prefix.
2. Pick the instance by exact PID and set `NEST_SIGNATURE`.
3. `hyprctl -i "$NEST_SIGNATURE" plugin load /home/justin/projects/hypr-bubble/build/liquid-glass.so`.
4. Enable the fixture's commented settings block and reload.

Approve plugin permission only for this nested instance.

Capture with `grim` or `wf-recorder` against the nested Wayland socket, after verifying the socket's peer PID is the launched compositor (`ss -xp` or an SO_PEERCRED check, as in earlier sessions). Save into `.project-manager/evidence/droplet/`.

### Task 15: Exercise every changed path

Record real-time plus quarter-speed frame sheets for each scenario:

| Scenario | Command / action | Expected |
|---|---|---|
| Open, fixture `popin 87%` | Alt+Return | Round bead (~30% of half short side) swells to **full** size (not 87%), oval transient, one soft rebound, square rest; no double scaling |
| Open, slide style | `hyprctl -i … keyword animation windowsIn,1,4,default,slide` then open | Bead swells while sliding in |
| Close at rest | Alt+C | Corners round → round bead → slight swell → collapse to point while fading; never a frozen blob |
| Close mid-drag | Alt+drag, then Alt+C during motion | Drag bow carried into the beading |
| Short native fade | `keyword animation fadeOut,1,1,default` | Pop still completes (orphan path), no abrupt cut |
| Long native fade | `keyword animation fadeOut,1,15,default` | Droplet finishes; native rectangle never reappears |
| Transitions off | `plugin:liquid-glass:transitions false` | Native popin/fade untouched |
| Wiggle | `plugin:liquid-glass:mode wiggle` | No droplet; native close |
| Overcapacity | Open 18 kitties, close all | Native fades for the overflow; compositor alive |
| Unload mid-close | Close, then immediately `plugin unload` ×3 | No crash, no mapping left in `/proc/<pid>/maps` |
| Idle | Wait 6 s after all motion | No ticks, transforms or close draws (the idle counters used in earlier evidence) |

Restore fixture animation defaults after the keyword overrides (`hyprctl -i … reload`).

If the result is visually off, tune only constants in `src/DropletTransition.cpp` and keep the Task 3 bounds green.

---

## Phase F — Docs and cleanup

### Task 16: README

**Files:** Modify `README.md`

- `:3` intro: replace "inflates on open and contracts like a collapsing bubble on close" with the droplet description.
- `:80-81` Open/Close bullets: describe the bead (`max(0.12, 1 − 1.08·strength)` of the half shorter side), superellipse rounding, one rebound + surface ripple, close timeline (0.42 s × 2^(0.5−elasticity)), and the fade over the final 30%. State that native `windowsIn`/`windowsOut` **scaling** is neutralised while native translation is kept, and that Liquid close alpha/timing are plugin-owned (native `fadeOut` no longer governs them).
- `:58`: config/mode changes still drop pending closes. A dropped owned close whose native fade already ended disappears immediately.
- Closing seam (`:88`): the hook now also draws owned records after their native fadeout is gone (retained snapshot, captured plane/workspace/z-order); records are capped at 16. Known limitation: dim-around and pre-blur stop when the native fadeout ends; windows with texture blur keep the native close.
- `:84`: mention `tests/DropletTransitionTests.cpp`.

### Task 17: Progress log and cleanup

- Append a dated entry to `.project-manager/progress.md` covering: the feedback, the chosen direction, the design, the files touched, test results and the evidence paths.
- Delete any throwaway capture scripts in `/tmp`.
- Leave the nested compositor in the state the user expects, or stop only the instance you launched.

---

## Risks and tradeoffs

- **Content squeeze in the bead:** text is heavily distorted for the first ~150 ms. Native fade-in alpha softens it. A circular mask would need a custom shader (out of scope).
- **Ownership diverges from user animation config:** popin percentages stop having a visible effect for Liquid windows. Slide still works. If unwanted, revert Tasks 8 and 11's ownership and keep only the droplet.
- **Orphan drawing:**
  - Z-order is by the captured `zIndex`, so windows opened during the ≤ 0.42 s pop may stack slightly wrong.
  - A workspace switch hides the orphan (workspace mismatch); it finishes invisibly.
  - Holding a monitor-sized snapshot (~33 MB at 4K) for an extra ≤ 0.42 s per close is capped at 16 records.
- **Over-capacity frames** (> 16 fadeouts + orphans on one plane) fall back to native for that frame; orphans skip that frame.
- **Hook surface unchanged:** no new symbols; same ABI-hash guard and all-or-nothing install.
- **Constants are perceptual:** expect one or two tuning passes from real captures (Task 15). Never relax the unit-test bounds to fit.

## Open questions (defaults assumed above)

1. Bead content: squeeze (default) or a circular mask/crop?
2. Close finale: collapse to a point (default) or an outward burst?
3. Neutralise native popin scale (default) or let it stack?
4. Close duration: fixed and elasticity-scaled (default) or derived from the user's `windowsOut` speed?
