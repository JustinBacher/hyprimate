# Edge Pop-out Transitions Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Replace the Liquid-mode droplet open/close completely. Each window pops out of a screen edge, the edge it is appearing from, or a user-configured edge if it is the first window on its workspace. It drains back into an edge when it closes. A black edge liquid behaves like the white bar in the reference clip.

**Architecture:** A new pure model, `liquid_glass::EdgeTransition` in `src/EdgeTransition.{hpp,cpp}`, replaces `DropletTransition`. It owns three things: the chosen edge, a damped travel offset that moves the window from fully behind the edge to its rest box, and a 1-D height profile for the black "meniscus" anchored to the edge. The meniscus has crossing fillets, an attached neck, a snap and a recoil. The window mesh becomes `rest + movement-field displacement + translation`, so the droplet's radial outline map and its triangle-area guard are deleted. Travel is fed to the existing `LiquidMesh` as a drag, which gives the body the same gel response as any moved Liquid window. The meniscus is a 96-column strip, drawn with a 4×4 opaque black texture before the window, in the same buffer (open) or pass element (close).

**Tech Stack:** C++26, GCC 16, Hyprland 0.57.0 plugin API (pinned `0aa6745f…`), CMake/Ninja, CTest with the repo's self-contained `CHECK` harness, nested Hyprland and `grim` for visual verification.

---

## 0. Reference analysis (what we are copying)

Source: `https://cdn.dribbble.com/userupload/40432364/file/large-e7d1621bf6d6bffa6ddbc09cc57cf505.mp4` (800×600, 60 fps, 5.5 s). Frames were inspected at 30 fps around 1.15–1.95 s (pop-out) and 4.3–5.5 s (return).

| Moment (30 fps frames) | White bar behaviour | What we copy |
|---|---|---|
| 1.15 s | Concave cradle under the circle | **Not copied.** The user excluded it: windows start behind the screen edge, so there is no cradle |
| 1.18 s (1 frame) | Circle pops up. The bar's top edge lifts into a peak whose tip stays attached under the circle | The meniscus tip stays tucked under the window's near side (`tuck = Fs`) |
| 1.18–1.45 s | The peak stretches and narrows into a neck as the circle rises | Plateau shrinks to 0 and the skirt narrows from `σc` to `σn` while the near side moves away |
| ≈1.48 s | Neck snaps. The peak drops below the rest line (dent) | Snap → damped recoil spring on the peak height. The negative lobe is invisible against a zero-thickness edge |
| 1.50–1.60 s | Small secondary bump, then flat | Recoil underdamped (`ζ≈0.4` at default momentum), so the second positive lobe shows |
| 4.6–5.0 s (return) | The bar rises a little to meet the incoming circle, then absorbs it | Close: "reach" peak rises within `2.5·Lp` of the edge, attaches, the window drains past the edge, and the meniscus recoils |

The user asked for the bar to be **black instead of white**, so it reads as the screen bezel bulging. Colour is a constant, opaque black.

## 1. Behaviour contract

**Edge choice (`resolveEdgeFrame`)**
- Screen = the monitor's **logical box** (`CMonitor::logicalBox()`), not the work area. Windows come out of the physical side of the screen. A top layer such as a bar stays above them, so they appear from under it.
- An edge is valid only if the decorated window (`GEOMETRIC_GOAL` on open, `GEOMETRIC_CURRENT` on close) is inside the screen along that edge's normal (gap ≥ −0.5 px).
- **First window on its workspace** (no other mapped, non-hidden window on the same workspace): use `plugin:liquid-glass:first_window_side` (default `bottom`) if it is valid.
- **Otherwise:** pick the valid edge nearest the window centre. Ties within 1 px go to `first_window_side`, then to Bottom, Right, Left, Top. Dwindle examples on 1920×1080 with gaps 24/8:
  - second window, right half → Right
  - third window, bottom-right quarter → Bottom
  - fourth window, right eighth → Right
- **No valid edge** (window crosses every candidate edge): no transition. The native open/close runs with the movement field only.
- **Close** drains into the nearest valid edge, using the same tie rule (no first-window preference). A close that interrupts an opening keeps the opening's edge.

**Open (default elasticity 0.5, momentum 0.5)**
- Starts fully hidden: every capture texel is at `n ≤ 0`, behind the edge line. Travel is a damped spring to rest: ω = 15·2^(e−0.5) rad/s, ζ = lerp(0.85, 0.5, momentum). About 6% overshoot past rest gives the "pop". Settles in ≈0.45 s.
- Meniscus, in edge-frame coordinates (`n` = px into the screen, `t` = tangential):
  1. **Crossing** (near side still behind the edge): fillets of height `Fs` hug both sides of the window where it passes through the edge. Plateau = window half-width, skirt `σc = 2.5·Fs`. The height ramps from 0 over the first `2·Fs` of emergence.
  2. **Attached** (`0 ≤ n_near`): height = `n_near + Fs`, so the tip stays tucked `Fs` under the near side. Plateau shrinks `halfW·(1−q)` and the skirt narrows `σc→σn`, with `q = smoothstep(0, Lp, n_near)`.
  3. **Snap** at the first of: `n_near ≥ Lp`, the travel apex (velocity crosses ≤ 0), or travel at rest. After the snap the peak height follows a recoil spring to 0: ω = 28·2^(e−0.5), ζ = lerp(0.6, 0.2, momentum). The plateau decays (τ 50 ms) and the skirt widens by 60% (τ 60 ms), so the falling peak spreads like the bar does.
- Ends with an exact reset: offset `{0,0}`, no meniscus, transformer pruned once the movement field settles.
- Constants:
  - `Fs = clamp(0.035·bodyWidth, 6, 20)·k`
  - `Lp = clamp(0.18·bodyDepth, 24, 96)·k`
  - `σn = clamp(0.10·bodyWidth, 16, 64)`
  - `k = clamp(strength/0.65, 0.25, 1.5)`

**Close**
- Timeline travel: `s = s_from + (s_end − s_from)·easeInCubic(p)`, where `s_end = −captureFar` (fully behind the edge) and duration = 0.38·2^(0.5−e) s.
- Meniscus:
  - **reach**: `(Lp+Fs)·smoothstep(2.5Lp → Lp, n_near)`, plateau 0, skirt `σn`
  - **attached**: same formula as open
  - **crossing**: constant `Fs`
  - **drained** (window fully hidden): recoil spring from `Fs` to rest, then `closed()`
- An owned close keeps the native snapshot's alpha (`sourceAlpha`) throughout. The edge does the disappearing, so there is no plugin fade.

**Wiggle mode, `transitions = false`, texture-blurred closes:** unchanged (native).

**Window body:** each step's travel delta × `LIQUID_EMERGE_MESH_DRIVE` (0.5, tuned in Task 19) goes to `LiquidMesh::recordTranslation`. The trailing near side therefore bows back toward the edge while the neck holds it, and the landing gets the material's usual surge. The initial jump to the hidden position is never reported as motion.

**Invariants (tested):**
- identity when idle
- start fully hidden
- meniscus ≥ 0 and zero outside `[low, high]`
- meniscus continuity (|ΔH| ≤ |Δn_near| + 1 px per substep)
- exactly one snap per open
- the same result at 60 Hz and 144 Hz
- close continuity from an interrupted open
- invalid input → open idles, close goes to `closed()`

## 2. Files

| Action | Path |
|---|---|
| Create | `src/EdgeTransition.hpp`, `src/EdgeTransition.cpp` (pure; no compositor includes) |
| Create | `src/SolidFill.hpp`, `src/SolidFill.cpp` (lazy opaque black texture) |
| Create | `tests/EdgeTransitionTests.cpp` |
| Modify | `src/LiquidGlassTransformer.hpp/.cpp`: pose, extents, meniscus mesh, edge clip, transform |
| Modify | `src/LiquidCloseRenderer.hpp/.cpp`: record transition, travel→mesh, meniscus draw, owned alpha |
| Modify | `src/PluginRuntime.hpp/.cpp`: `first_window_side`, edge resolution, open/close wiring, solid fill ownership |
| Modify | `CMakeLists.txt`, `README.md`, `tests/nested/hyprland.lua` (settings comment only) |
| Delete | `src/DropletTransition.hpp`, `src/DropletTransition.cpp`, `tests/DropletTransitionTests.cpp` |

The workspace is **not a git repository**. Every "Checkpoint" step records the source fingerprint used in `.project-manager/progress.md` instead of committing:

```bash
sha256sum CMakeLists.txt hyprpm.toml src/* tests/*.cpp tests/nested/hyprland.lua README.md | sha256sum
```

Build commands (from README):

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/c++ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## Phase A: Pure model (TDD)

### Task 1: Header and test target

**Objective:** Declare the public model and add a test executable that compiles. The plugin keeps building because nothing references the header yet.

**Files:** Create `src/EdgeTransition.hpp`, `src/EdgeTransition.cpp` (empty namespace for now), `tests/EdgeTransitionTests.cpp`. Modify `CMakeLists.txt`.

**Step 1: Write `src/EdgeTransition.hpp`**

```cpp
#pragma once

#include "LiquidMesh.hpp"

#include <optional>

namespace liquid_glass {

enum class Transition { Open, Close };
enum class Edge { Left, Right, Top, Bottom };

// Logical pixels, y down.
struct Box {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};

// Rest geometry in the edge frame. n = distance from the screen edge into the screen; t = tangential coordinate.
// Every value is for points given relative to the capture (decorated + shadow) centre at rest.
struct EdgeFrame {
    Edge   edge         = Edge::Bottom;
    double edgeDistance = 0.0; // n of the capture centre (> 0)
    double captureFar   = 0.0; // n of the capture side farthest from the edge
    double bodyNear     = 0.0; // n of the decorated window side nearest the edge (>= 0)
    double bodyFar      = 0.0; // n of the decorated window side farthest from the edge
    double bodyT0       = 0.0; // tangential span of the decorated window
    double bodyT1       = 0.0;
};

// Chooses the edge a window emerges from or drains into. An edge is valid when the decorated body lies inside the
// screen along its normal. preferFallback (first window on its workspace) picks `fallback` when valid; otherwise
// the valid edge nearest the body centre, ties within 1 px going to `fallback`, then Bottom, Right, Left, Top.
// nullopt for unusable boxes or when no edge is valid.
std::optional<EdgeFrame> resolveEdgeFrame(const Box& body, const Box& capture, const Box& screen, Edge fallback, bool preferFallback) noexcept;

// Edge frame <-> point relative to the capture centre.
Vec2   edgePoint(const EdgeFrame& frame, double t, double n) noexcept;
double edgeNormal(const EdgeFrame& frame, Vec2 p) noexcept;
double edgeTangent(const EdgeFrame& frame, Vec2 p) noexcept;
// Unit vector pointing from the edge into the screen.
Vec2   inwardNormal(Edge edge) noexcept;

// Pop-out lifecycle: the window travels out from behind a screen edge (Open) or back behind it (Close) while a
// black edge liquid fillets, necks, snaps and recoils. Fixed 1/240 s substeps; no compositor dependency.
class EdgeTransition {
  public:
    // Invalid frame, parameters or non-positive strength leave it idle. A Close begun during an Open continues
    // from the current travel on the Open's edge and ignores `frame`.
    void begin(Transition kind, const EdgeFrame& frame, const MaterialParams& params, double strength) noexcept;
    // Nonfinite, negative or > 0.25 s steps abort: Open returns to idle, Close to closed.
    bool advance(double elapsedSeconds) noexcept;
    void reset() noexcept;
    bool active() const noexcept;
    bool closed() const noexcept;
    const EdgeFrame& frame() const noexcept { return m_frame; }

    // Window translation in logical px; exactly {0, 0} unless active.
    Vec2 offset() const noexcept;
    // Translation since the previous call (begin() does not count as travel). Feeds the movement field.
    Vec2 takeTravel() noexcept;

    // Edge liquid height in px into the screen at tangential coordinate t. 0 unless active; never negative.
    double meniscus(double t) const noexcept;
    // meniscus() is 0 outside [meniscusLow, meniscusHigh]; empty (low == high) when there is none.
    double meniscusLow() const noexcept;
    double meniscusHigh() const noexcept;
    double meniscusPeak() const noexcept;

  private:
    enum class Phase { Idle, Opening, Closing, Closed };
    struct Spring {
        double value    = 0.0;
        double velocity = 0.0;
    };
    struct Profile {
        double height  = 0.0;
        double plateau = 0.0; // half-width of the flat top
        double skirt   = 1.0; // falloff width beyond the plateau
    };

    void    step() noexcept;
    void    abort() noexcept;
    void    startRecoil() noexcept;
    void    advanceRecoil() noexcept;
    bool    quiet() const noexcept;
    double  nearSide() const noexcept;
    Profile liveProfile() const noexcept;
    Profile recoilProfile() const noexcept;
    void    refreshProfile() noexcept;

    Phase     m_phase    = Phase::Idle;
    EdgeFrame m_frame{};
    double    m_tuck      = 0.0; // Fs
    double    m_pinch     = 0.0; // Lp
    double    m_neckSkirt = 0.0; // sigma_n
    Spring    m_travel{};        // value: travel along the inward normal; 0 = rest, negative = toward/behind the edge
    double    m_travelOmega = 0.0, m_travelZeta = 1.0;
    double    m_taken       = 0.0; // travel value at the last takeTravel()
    bool      m_recoiling   = false;
    Spring    m_recoil{};
    double    m_recoilOmega = 0.0, m_recoilZeta = 1.0, m_recoilTime = 0.0;
    Profile   m_snap{};    // profile at the snap
    Profile   m_profile{}; // cached profile for the current state
    double    m_time = 0.0, m_duration = 0.0, m_from = 0.0, m_to = 0.0; // Close timeline
    bool      m_drained     = false;
    double    m_accumulator = 0.0;
};

} // namespace liquid_glass
```

**Step 2:** `src/EdgeTransition.cpp` contains only `#include "EdgeTransition.hpp"`. The test file contains the harness block copied verbatim from `tests/DropletTransitionTests.cpp:1-27` (with `#include "EdgeTransition.hpp"`), plus `int main() { return g_failures == 0 ? 0 : 1; }`.

**Step 3: CMake.** Add the test target after the droplet test (it is removed in Task 12):

```cmake
    add_executable(edge-transition-tests tests/EdgeTransitionTests.cpp src/EdgeTransition.cpp)
    target_include_directories(edge-transition-tests PRIVATE src)
    target_compile_options(edge-transition-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME edge-transition-tests COMMAND edge-transition-tests)
```

`EdgeTransition.hpp` and `DropletTransition.hpp` both declare `enum class Transition`. Do **not** include both in one translation unit until Task 12. The test only includes the new header.

**Step 4:** Run `cmake --build build --target edge-transition-tests && ctest --test-dir build -R edge-transition --output-on-failure`. Expected: build fails with undefined references only once tests call functions; for now it passes trivially.

**Checkpoint:** fingerprint.

### Task 2: Edge selection (`resolveEdgeFrame`)

**Objective:** Pick the edge exactly as section 1 specifies, and fill the frame.

**Step 1: Failing tests** (add them to `tests/EdgeTransitionTests.cpp` and call them from `main`):

```cpp
constexpr liquid_glass::Box kScreen{0.0, 0.0, 1920.0, 1080.0};
liquid_glass::Box withShadow(liquid_glass::Box b) { return {b.x - 12.0, b.y - 12.0, b.w + 24.0, b.h + 24.0}; }
std::optional<liquid_glass::Edge> pick(liquid_glass::Box body, liquid_glass::Edge fallback, bool first, liquid_glass::Box screen = kScreen) {
    const auto f = liquid_glass::resolveEdgeFrame(body, withShadow(body), screen, fallback, first);
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
    const auto f = liquid_glass::resolveEdgeFrame(body, withShadow(body), kScreen, liquid_glass::Edge::Bottom, false);
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
```

**Step 2:** Run. Expected: link errors (functions not defined).

**Step 3: Implement** in `src/EdgeTransition.cpp`:

```cpp
#include "EdgeTransition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace liquid_glass {
namespace {

constexpr double kEdgeTolerance = 0.5; // px a body may overhang an edge and still use it
constexpr double kTieTolerance  = 1.0; // px

bool usable(const Box& b) {
    return std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.w) && std::isfinite(b.h) && b.w > 0.0 && b.h > 0.0;
}

bool runsHorizontally(Edge e) {
    return e == Edge::Top || e == Edge::Bottom;
}

} // namespace

Vec2 inwardNormal(Edge edge) noexcept {
    switch (edge) {
        case Edge::Left: return {1.0, 0.0};
        case Edge::Right: return {-1.0, 0.0};
        case Edge::Top: return {0.0, 1.0};
        case Edge::Bottom: return {0.0, -1.0};
    }
    return {0.0, -1.0};
}

Vec2 edgePoint(const EdgeFrame& f, double t, double n) noexcept {
    const double d = f.edgeDistance;
    switch (f.edge) {
        case Edge::Left: return {n - d, t};
        case Edge::Right: return {d - n, t};
        case Edge::Top: return {t, n - d};
        case Edge::Bottom: return {t, d - n};
    }
    return {t, d - n};
}

double edgeNormal(const EdgeFrame& f, Vec2 p) noexcept {
    const double d = f.edgeDistance;
    switch (f.edge) {
        case Edge::Left: return d + p.x;
        case Edge::Right: return d - p.x;
        case Edge::Top: return d + p.y;
        case Edge::Bottom: return d - p.y;
    }
    return d - p.y;
}

double edgeTangent(const EdgeFrame& f, Vec2 p) noexcept {
    return runsHorizontally(f.edge) ? p.x : p.y;
}

std::optional<EdgeFrame> resolveEdgeFrame(const Box& body, const Box& capture, const Box& screen, Edge fallback, bool preferFallback) noexcept {
    if (!usable(body) || !usable(capture) || !usable(screen))
        return std::nullopt;

    constexpr std::array<Edge, 4> kOrder{Edge::Bottom, Edge::Right, Edge::Left, Edge::Top};
    const double right = screen.x + screen.w, bottom = screen.y + screen.h;
    const double cx = body.x + 0.5 * body.w, cy = body.y + 0.5 * body.h;
    const auto gap = [&](Edge e) {
        switch (e) {
            case Edge::Left: return body.x - screen.x;
            case Edge::Right: return right - (body.x + body.w);
            case Edge::Top: return body.y - screen.y;
            case Edge::Bottom: return bottom - (body.y + body.h);
        }
        return -1.0;
    };
    const auto reach = [&](Edge e) {
        switch (e) {
            case Edge::Left: return cx - screen.x;
            case Edge::Right: return right - cx;
            case Edge::Top: return cy - screen.y;
            case Edge::Bottom: return bottom - cy;
        }
        return std::numeric_limits<double>::infinity();
    };
    const auto valid = [&](Edge e) { return gap(e) >= -kEdgeTolerance; };

    std::optional<Edge> chosen;
    if (preferFallback && valid(fallback))
        chosen = fallback;
    else {
        double best = std::numeric_limits<double>::infinity();
        for (Edge e : kOrder)
            if (valid(e))
                best = std::min(best, reach(e));
        if (valid(fallback) && reach(fallback) <= best + kTieTolerance)
            chosen = fallback;
        else
            for (Edge e : kOrder)
                if (valid(e) && reach(e) <= best + kTieTolerance) {
                    chosen = e;
                    break;
                }
    }
    if (!chosen)
        return std::nullopt;

    const Edge   e   = *chosen;
    const bool   hz  = runsHorizontally(e);
    const double ccx = capture.x + 0.5 * capture.w, ccy = capture.y + 0.5 * capture.h;
    EdgeFrame    f;
    f.edge = e;
    switch (e) {
        case Edge::Left: f.edgeDistance = ccx - screen.x; break;
        case Edge::Right: f.edgeDistance = right - ccx; break;
        case Edge::Top: f.edgeDistance = ccy - screen.y; break;
        case Edge::Bottom: f.edgeDistance = bottom - ccy; break;
    }
    f.captureFar = f.edgeDistance + 0.5 * (hz ? capture.h : capture.w);
    f.bodyNear   = std::max(0.0, gap(e));
    f.bodyFar    = f.bodyNear + (hz ? body.h : body.w);
    f.bodyT0     = hz ? body.x - ccx : body.y - ccy;
    f.bodyT1     = f.bodyT0 + (hz ? body.w : body.h);
    return f;
}

} // namespace liquid_glass
```

**Step 4:** Run `ctest --test-dir build -R edge-transition --output-on-failure`. Expected: PASS. **Checkpoint.**

### Task 3: Open travel (hidden start → pop → exact rest)

**Objective:** Begin/advance/reset/offset/takeTravel for Open, without the meniscus.

**Step 1: Failing tests**

```cpp
using liquid_glass::EdgeFrame; using liquid_glass::EdgeTransition; using liquid_glass::MaterialParams;
using liquid_glass::Transition; using liquid_glass::Vec2;
constexpr double kStep = 1.0 / 240.0;

EdgeFrame frameFor(liquid_glass::Box body, liquid_glass::Edge fallback = liquid_glass::Edge::Bottom, bool first = false) {
    return *liquid_glass::resolveEdgeFrame(body, withShadow(body), kScreen, fallback, first);
}
const EdgeFrame kQuarter = frameFor({972, 548, 924, 508});          // Bottom edge
double nearSide(const EdgeTransition& tr) {                           // n of the body near side, displayed
    const EdgeFrame& f = tr.frame();
    const Vec2 o = tr.offset(), p = liquid_glass::edgePoint(f, 0.5 * (f.bodyT0 + f.bodyT1), f.bodyNear);
    return liquid_glass::edgeNormal(f, {p.x + o.x, p.y + o.y});
}
double travel(const EdgeTransition& tr) { return nearSide(tr) - tr.frame().bodyNear; } // 0 at rest

void testOpenStartsHiddenAndSettlesExactly() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    CHECK(tr.active());
    const Vec2 o = tr.offset();
    for (double sx : {-1.0, 1.0})
        for (double sy : {-1.0, 1.0})
            CHECK(liquid_glass::edgeNormal(kQuarter, {sx * 474.0 + o.x, sy * 266.0 + o.y}) <= 1e-9); // capture corners
    const Vec2 first = tr.takeTravel();
    CHECK(first.x == 0.0 && first.y == 0.0);                                                       // begin is not travel
    double maxTravel = -1e9;
    for (int i = 0; i < 480 && tr.active(); ++i) { tr.advance(kStep); maxTravel = std::max(maxTravel, travel(tr)); }
    CHECK(!tr.active());
    CHECK(tr.offset().x == 0.0 && tr.offset().y == 0.0);
    CHECK(maxTravel > 5.0);                                                                        // visible pop overshoot
}

void testOpenEdgesPointTheRightWay() {
    using liquid_glass::Edge;
    const std::array<std::pair<Edge, Vec2>, 4> cases{{{Edge::Bottom, {0, 1}}, {Edge::Top, {0, -1}}, {Edge::Left, {-1, 0}}, {Edge::Right, {1, 0}}}};
    for (const auto& [edge, sign] : cases) {
        EdgeTransition tr;
        tr.begin(Transition::Open, frameFor({24, 24, 1872, 1032}, edge, true), MaterialParams{}, kStrength);
        const Vec2 o = tr.offset();
        CHECK(o.x * sign.x + o.y * sign.y > 500.0);                                                // pushed out through that edge
    }
}

```

**Step 2:** Run → FAIL (undefined `begin`…).

**Step 3: Implement** the travel portion. Add to the anonymous namespace:

```cpp
constexpr double kSubstepSeconds       = 1.0 / 240.0;
constexpr double kAccumulatorTolerance = 1e-9;
constexpr double kGapSeconds           = 0.25;
constexpr double kReferenceStrength    = 0.65;
// Travel spring (window pop).
constexpr double kTravelOmega     = 15.0; // rad/s at elasticity 0.5, scaled by 2^(elasticity - 0.5)
constexpr double kTravelZetaStiff = 0.85; // momentum 0
constexpr double kTravelZetaLoose = 0.5;  // momentum 1
// Black edge liquid.
constexpr double kTuckPerWidth = 0.035, kTuckMin = 6.0, kTuckMax = 20.0;
constexpr double kPinchPerDepth = 0.18, kPinchMin = 24.0, kPinchMax = 96.0;
constexpr double kNeckPerWidth = 0.10, kNeckMin = 16.0, kNeckMax = 64.0;
constexpr double kCrossSkirtPerTuck = 2.5;
constexpr double kReachFactor       = 2.5;
// Recoil after the snap.
constexpr double kRecoilOmega       = 28.0;
constexpr double kRecoilZetaStiff   = 0.6, kRecoilZetaLoose = 0.2;
constexpr double kRecoilSkirtGrowth = 0.6, kRecoilSkirtSeconds = 0.06, kRecoilPlateauSeconds = 0.05;
// Snap fallbacks: travel apex or travel at rest.
constexpr double kRestTravelPixels = 0.5, kRestTravelPixelsPerSecond = 5.0;
// Close.
constexpr double kCloseSeconds = 0.38;
// Settling (movement field thresholds).
constexpr double kQuietPixels = 0.15, kQuietPixelsPerSecond = 2.0;

double smooth(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
double lerp(double a, double b, double t) { return a + (b - a) * t; }
double easeInCubic(double t) { t = std::clamp(t, 0.0, 1.0); return t * t * t; }
void   spring(auto& s, double omega, double zeta) { // toward 0; semi-implicit Euler, omega*dt <= 0.12
    const double accel = -omega * omega * s.value - 2.0 * zeta * omega * s.velocity;
    s.velocity += accel * kSubstepSeconds;
    s.value += s.velocity * kSubstepSeconds;
}
bool validFrame(const EdgeFrame& f) {
    const double v[] = {f.edgeDistance, f.captureFar, f.bodyNear, f.bodyFar, f.bodyT0, f.bodyT1};
    for (double x : v) if (!std::isfinite(x)) return false;
    return f.edgeDistance > 0.0 && f.captureFar > 0.0 && f.bodyNear >= 0.0 && f.bodyFar > f.bodyNear && f.bodyT1 > f.bodyT0;
}
```

Members (open part):

```cpp
void EdgeTransition::begin(Transition kind, const EdgeFrame& frame, const MaterialParams& params, double strength) noexcept {
    const bool           continuing = kind == Transition::Close && m_phase == Phase::Opening;
    const EdgeTransition previous   = *this;
    reset();
    const EdgeFrame& chosen = continuing ? previous.m_frame : frame;
    if (!validFrame(chosen) || !std::isfinite(params.elasticity) || !std::isfinite(params.momentum) ||
        !std::isfinite(params.viscosity) || !std::isfinite(strength) || strength <= 0.0)
        return;

    m_frame               = chosen;
    const double e        = std::clamp(params.elasticity, 0.0, 1.0);
    const double m        = std::clamp(params.momentum, 0.0, 1.0);
    const double k        = std::clamp(strength / kReferenceStrength, 0.25, 1.5);
    const double width    = m_frame.bodyT1 - m_frame.bodyT0;
    const double depth    = m_frame.bodyFar - m_frame.bodyNear;
    m_tuck                = std::clamp(kTuckPerWidth * width, kTuckMin, kTuckMax) * k;
    m_pinch               = std::clamp(kPinchPerDepth * depth, kPinchMin, kPinchMax) * k;
    m_neckSkirt           = std::clamp(kNeckPerWidth * width, kNeckMin, kNeckMax);
    m_recoilOmega         = kRecoilOmega * std::exp2(e - 0.5);
    m_recoilZeta          = lerp(kRecoilZetaStiff, kRecoilZetaLoose, m);

    if (kind == Transition::Open) {
        m_travelOmega = kTravelOmega * std::exp2(e - 0.5);
        m_travelZeta  = lerp(kTravelZetaStiff, kTravelZetaLoose, m);
        m_travel      = {-m_frame.captureFar, 0.0};
        m_phase       = Phase::Opening;
    } else {
        m_duration = kCloseSeconds * std::exp2(0.5 - e);
        m_from     = continuing ? previous.m_travel.value : 0.0;
        m_to       = -m_frame.captureFar;
        m_travel   = {m_from, 0.0};
        if (continuing && previous.m_recoiling) {
            m_recoiling  = true;
            m_recoil     = previous.m_recoil;
            m_snap       = previous.m_snap;
            m_recoilTime = previous.m_recoilTime;
        }
        m_phase = Phase::Closing;
    }
    m_taken = m_travel.value;
    refreshProfile();
}

void EdgeTransition::reset() noexcept { *this = EdgeTransition{}; }

void EdgeTransition::abort() noexcept {
    const bool closing = m_phase == Phase::Closing;
    reset();
    if (closing)
        m_phase = Phase::Closed;
}

bool EdgeTransition::active() const noexcept { return m_phase == Phase::Opening || m_phase == Phase::Closing; }
bool EdgeTransition::closed() const noexcept { return m_phase == Phase::Closed; }

Vec2 EdgeTransition::offset() const noexcept {
    if (!active())
        return {0.0, 0.0};
    const Vec2 n = inwardNormal(m_frame.edge);
    return {n.x * m_travel.value, n.y * m_travel.value};
}

Vec2 EdgeTransition::takeTravel() noexcept {
    const double delta = active() ? m_travel.value - m_taken : 0.0;
    m_taken            = m_travel.value;
    const Vec2 n       = inwardNormal(m_frame.edge);
    return {n.x * delta, n.y * delta};
}

double EdgeTransition::nearSide() const noexcept { return m_frame.bodyNear + m_travel.value; }

bool EdgeTransition::advance(double elapsedSeconds) noexcept {
    if (!active())
        return false;
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 || elapsedSeconds > kGapSeconds) {
        abort();
        return false;
    }
    m_accumulator += elapsedSeconds;
    while (active() && m_accumulator >= kSubstepSeconds - kAccumulatorTolerance) {
        m_accumulator -= kSubstepSeconds;
        step();
    }
    refreshProfile();
    return active();
}

bool EdgeTransition::quiet() const noexcept {
    const bool travelQuiet = std::abs(m_travel.value) < kQuietPixels && std::abs(m_travel.velocity) < kQuietPixelsPerSecond;
    const bool liquidQuiet = m_recoiling && std::abs(m_recoil.value) < kQuietPixels && std::abs(m_recoil.velocity) < kQuietPixelsPerSecond;
    return travelQuiet && liquidQuiet;
}

void EdgeTransition::step() noexcept {
    if (m_phase == Phase::Opening) {
        const double before = m_travel.velocity;
        spring(m_travel, m_travelOmega, m_travelZeta);
        if (m_recoiling)
            advanceRecoil();
        else if (nearSide() >= 0.0) {
            const bool apex    = before > 0.0 && m_travel.velocity <= 0.0;
            const bool resting = std::abs(m_travel.value) < kRestTravelPixels && std::abs(m_travel.velocity) < kRestTravelPixelsPerSecond;
            if (nearSide() >= m_pinch || apex || resting) {
                refreshProfile();
                startRecoil();
            }
        }
        if (quiet())
            reset();
        return;
    }
    // Closing: Task 6.
}
```

Leave `startRecoil`, `advanceRecoil`, `refreshProfile`, `liveProfile`, `recoilProfile` and the `meniscus*` accessors as minimal stubs (`m_profile = {}`; `meniscus*` return 0; `startRecoil` sets `m_recoiling = true` with `m_recoil = {}`). Task 4 replaces them.

**Step 4:** Run → PASS. **Checkpoint.**

### Task 4: Meniscus during open (crossing → attached → snap → recoil)

**Objective:** Implement the black edge-liquid profile and its snap/recoil.

**Step 1: Failing tests**

```cpp
double centreT(const EdgeFrame& f) { return 0.5 * (f.bodyT0 + f.bodyT1); }

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
        CHECK(std::abs(peak - prevPeak) <= std::abs(n - prevNear) + 1.0);       // continuous
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
```

**Step 2:** Run → FAIL (stubs).

**Step 3: Implement**

```cpp
EdgeTransition::Profile EdgeTransition::liveProfile() const noexcept {
    const double n          = nearSide();
    const double halfWidth  = 0.5 * (m_frame.bodyT1 - m_frame.bodyT0);
    const double crossSkirt = kCrossSkirtPerTuck * m_tuck;
    if (n < 0.0) {
        // Crossing: fillets hug the window sides where it passes through the edge. Opening ramps them in as the
        // far side breaks the surface; closing holds them until the window is gone.
        const double rise = m_phase == Phase::Opening ? smooth(0.0, 2.0 * m_tuck, m_frame.bodyFar + m_travel.value) : 1.0;
        return {m_tuck * rise, halfWidth, crossSkirt};
    }
    if (n <= m_pinch) {
        // Attached: the tip stays tucked under the near side while the contact patch necks down.
        const double q = smooth(0.0, m_pinch, n);
        return {n + m_tuck, halfWidth * (1.0 - q), lerp(crossSkirt, m_neckSkirt, q)};
    }
    if (m_phase == Phase::Closing && n <= kReachFactor * m_pinch)
        // Reach: the edge rises to meet an incoming window.
        return {(m_pinch + m_tuck) * smooth(kReachFactor * m_pinch, m_pinch, n), 0.0, m_neckSkirt};
    return {};
}

EdgeTransition::Profile EdgeTransition::recoilProfile() const noexcept {
    const double t = m_recoilTime;
    return {std::max(0.0, m_recoil.value), m_snap.plateau * std::exp(-t / kRecoilPlateauSeconds),
            m_snap.skirt * (1.0 + kRecoilSkirtGrowth * (1.0 - std::exp(-t / kRecoilSkirtSeconds)))};
}

void EdgeTransition::refreshProfile() noexcept {
    if (!active()) {
        m_profile = {};
        return;
    }
    const bool    liveOver = (m_phase == Phase::Opening && m_recoiling) || (m_phase == Phase::Closing && m_drained);
    const Profile live     = liveOver ? Profile{} : liveProfile();
    const Profile residual = m_recoiling ? recoilProfile() : Profile{};
    m_profile              = residual.height > live.height ? residual : live;
}

void EdgeTransition::startRecoil() noexcept {
    m_snap       = m_profile;
    m_recoil     = {m_snap.height, 0.0};
    m_recoilTime = 0.0;
    m_recoiling  = true;
}

void EdgeTransition::advanceRecoil() noexcept {
    m_recoilTime += kSubstepSeconds;
    spring(m_recoil, m_recoilOmega, m_recoilZeta);
}

double EdgeTransition::meniscus(double t) const noexcept {
    const Profile& p = m_profile;
    if (!(p.height > 0.0))
        return 0.0;
    const double x = std::abs(t - 0.5 * (m_frame.bodyT0 + m_frame.bodyT1)) - p.plateau;
    if (x <= 0.0)
        return p.height;
    const double u = x / p.skirt;
    if (u >= 1.0)
        return 0.0;
    const double w = 1.0 - u * u;
    return p.height * w * w; // tangent to the edge at the skirt's foot, like the bar's concave flanks
}

double EdgeTransition::meniscusLow() const noexcept {
    const double c = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1);
    return m_profile.height > 0.0 ? c - m_profile.plateau - m_profile.skirt : c;
}
double EdgeTransition::meniscusHigh() const noexcept {
    const double c = 0.5 * (m_frame.bodyT0 + m_frame.bodyT1);
    return m_profile.height > 0.0 ? c + m_profile.plateau + m_profile.skirt : c;
}
double EdgeTransition::meniscusPeak() const noexcept { return m_profile.height; }
```

**Step 4:** Run → PASS. If `secondLobe` fails at the defaults, it means the recoil overshoot is < 0.5 px for the 24 px gap fixture. Report it rather than weakening the test: the reference clearly shows the bump. **Checkpoint.**

### Task 5: Refresh-rate agreement

**Step 1: Failing test** (it should already pass; it pins the substep accumulator):

```cpp
void testOpenAgreesAcrossRefreshRates() {
    EdgeTransition a, b;
    a.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    b.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    for (int k = 1; k <= 24; ++k) {                       // compare every 1/48 s for 0.5 s
        for (int i = 0; i < 5; ++i) a.advance(1.0 / 240.0); // 240 Hz-equivalent chunks (60 Hz = 4 of these)
        for (int i = 0; i < 3; ++i) b.advance(1.0 / 144.0);
        CHECK(std::abs(a.offset().y - b.offset().y) < 0.5);
        CHECK(std::abs(a.meniscusPeak() - b.meniscusPeak()) < 0.5);
    }
}
```

**Step 2:** Run. Expected PASS. If it fails, fix the accumulator rather than the tolerance. **Checkpoint.**

### Task 6: Close (reach → attach → cross → drained recoil → closed)

**Step 1: Failing tests**

```cpp
void testCloseFromRestDrainsIntoTheEdge() {
    EdgeTransition tr;
    tr.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength);
    CHECK(tr.active() && tr.offset().x == 0.0 && tr.offset().y == 0.0);
    double prev = travel(tr);
    bool   reached = false;
    for (int i = 0; i < 480 && tr.active(); ++i) {
        tr.advance(kStep);
        if (!tr.active()) break;
        CHECK(travel(tr) <= prev + 1e-9); prev = travel(tr);                       // always toward the edge
        if (tr.meniscusPeak() > 0.0 && nearSide(tr) > 0.0) reached = true;          // edge rose before contact
    }
    CHECK(reached);
    CHECK(tr.closed());
    CHECK(tr.meniscusPeak() == 0.0);
}

void testCloseContinuesAnInterruptedOpen() {
    EdgeTransition tr;
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    for (int i = 0; i < 30; ++i) tr.advance(kStep);
    const Vec2 before = tr.offset();
    const auto other = frameFor({24, 24, 900, 500}, liquid_glass::Edge::Left, true); // different edge: ignored
    tr.begin(Transition::Close, other, MaterialParams{}, kStrength);
    CHECK(tr.frame().edge == liquid_glass::Edge::Bottom);
    CHECK(std::abs(tr.offset().x - before.x) < 1e-9 && std::abs(tr.offset().y - before.y) < 1e-9);
    for (int i = 0; i < 480 && tr.active(); ++i) tr.advance(kStep);
    CHECK(tr.closed());
}

```

**Step 2:** Run → FAIL (the close branch is empty).

**Step 3: Implement** by replacing the `// Closing: Task 6.` comment in `step()`:

```cpp
    if (!m_drained) {
        m_time += kSubstepSeconds;
        m_travel.value = lerp(m_from, m_to, easeInCubic(m_time / m_duration));
        if (m_recoiling)
            advanceRecoil(); // residual recoil from an interrupted opening keeps falling
        if (m_time >= m_duration - kAccumulatorTolerance) {
            refreshProfile(); // crossing fillets at the moment the far side passes the edge
            startRecoil();
            m_drained = true;
        }
        return;
    }
    advanceRecoil();
    if (std::abs(m_recoil.value) < kQuietPixels && std::abs(m_recoil.velocity) < kQuietPixelsPerSecond)
        m_phase = Phase::Closed;
```

**Step 4:** Run → PASS. **Checkpoint.**

### Task 7: Invalid input

**Step 1: Failing test**

```cpp
void testInvalidInputs() {
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    EdgeTransition tr;
    tr.begin(Transition::Open, EdgeFrame{}, MaterialParams{}, kStrength);           CHECK(!tr.active());
    tr.begin(Transition::Open, kQuarter, MaterialParams{.elasticity = kNaN}, kStrength); CHECK(!tr.active());
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, 0.0);                    CHECK(!tr.active());
    tr.begin(Transition::Open, kQuarter, MaterialParams{}, kStrength);
    tr.advance(kNaN);                                                                CHECK(!tr.active() && !tr.closed());
    CHECK(tr.offset().x == 0.0 && tr.offset().y == 0.0);
    tr.begin(Transition::Close, kQuarter, MaterialParams{}, kStrength);
    tr.advance(0.3);                                                                 CHECK(tr.closed());
    tr.begin(Transition::Close, EdgeFrame{}, MaterialParams{}, kStrength);          CHECK(!tr.active() && !tr.closed());
}
```

**Step 2–4:** Run. It should already pass. If not, fix `begin`/`advance`. **Checkpoint.**

---

## Phase B: Compositor integration

These tasks do not use unit tests: they are compositor wiring, verified by build plus nested runs (Phase C). Each task ends with a successful `cmake --build build`.

### Task 8: Black texture holder

**Files:** Create `src/SolidFill.hpp`, `src/SolidFill.cpp`. Add `src/SolidFill.cpp` to `add_library` in `CMakeLists.txt`.

```cpp
// src/SolidFill.hpp
#pragma once

#include <hyprland/src/render/Texture.hpp>

// Lazily created 4x4 opaque black texture for the edge liquid. Create and use only while rendering (GL current).
class CSolidFill {
  public:
    SP<Render::ITexture> texture();

  private:
    SP<Render::ITexture> m_texture;
};
```

```cpp
// src/SolidFill.cpp
#include "SolidFill.hpp"

#include <hyprland/src/render/Renderer.hpp>

#include <array>
#include <cstdint>
#include <drm_fourcc.h>

SP<Render::ITexture> CSolidFill::texture() {
    if (!m_texture && g_pHyprRenderer) {
        // XRGB with opaque = true: black and fully opaque whatever the channel order.
        std::array<uint8_t, 4 * 4 * 4> pixels{};
        m_texture = g_pHyprRenderer->createTexture(DRM_FORMAT_XRGB8888, pixels.data(), 4 * 4, Vector2D{4.0, 4.0}, false, true);
    }
    return m_texture;
}
```

Verify the `createTexture(uint32_t, uint8_t*, uint32_t, const Vector2D&, bool, bool)` overload in `/usr/include/hyprland/src/render/Renderer.hpp:192`. Build. **Checkpoint.**

### Task 9: Pose and render helpers in the transformer

**Files:** `src/LiquidGlassTransformer.hpp`, `src/LiquidGlassTransformer.cpp`.

**Step 1: Header.** Replace `#include "DropletTransition.hpp"` with `#include "EdgeTransition.hpp"`, add `#include "SolidFill.hpp"` and `<optional>`. Replace `SLiquidPose` and add the new declarations:

```cpp
inline constexpr int         LIQUID_MENISCUS_COLUMNS      = 96;
inline constexpr std::size_t LIQUID_MENISCUS_VERTEX_COUNT = static_cast<std::size_t>(LIQUID_MENISCUS_COLUMNS) * 6;
// Share of the pop-out travel fed to the movement field as a drag (0 = rigid pop). Tuned against the reference.
inline constexpr double      LIQUID_EMERGE_MESH_DRIVE     = 0.5;

// One pose for both live-window transforms and retained closing snapshots.
struct SLiquidPose {
    const liquid_glass::LiquidMesh&     mesh;
    const liquid_glass::EdgeTransition& transition;
    liquid_glass::Vec2                  compensation{1.0, 1.0};
    liquid_glass::Vec2                  openOffset{0.0, 0.0}; // neutralises native windowsIn translation, logical px
};

// Advances the transition, feeds its travel into the mesh as a drag, then advances the mesh.
void advanceLiquidPose(liquid_glass::LiquidMesh& mesh, liquid_glass::EdgeTransition& transition, double elapsed,
                       liquid_glass::Vec2 size, double strength);
// Window samples and edge liquid, clipped to the visible side of the edge. `logicalBox` is the capture box.
// Never empty: a fully hidden pose yields a 1 px box on the edge line.
CBox liquidRenderExtents(const SLiquidPose& pose, const CBox& logicalBox);
// Visible half-plane of an active transition's edge, as a (very large) box in logicalBox's space.
std::optional<CBox> liquidVisibleBox(const SLiquidPose& pose, const CBox& logicalBox);
// Logical bounding box of the edge liquid, or nullopt when there is none.
std::optional<CBox> liquidMeniscusExtents(const SLiquidPose& pose, const CBox& logicalBox);
// Strip of LIQUID_MENISCUS_COLUMNS columns from just behind the edge up to the liquid surface; UVs sample the
// solid texture's centre. Positions are normalised to `outputDevice`.
void fillLiquidMeniscusMesh(const SLiquidPose& pose, const CBox& sourceDevice, double scale, const CBox& outputDevice,
                            std::vector<SMeshRenderVertex>& out);
```

Update the transformer class:
- constructor `(PHLWINDOWREF, MotionMode, MaterialParams, SP<CSolidFill> solid)`
- `transition()` accessors replace `droplet()`
- `void beginOpen(const liquid_glass::EdgeFrame& frame, const liquid_glass::MaterialParams& params, double strength);`
- delete `refreshPose` and `m_displacementGain`
- `cancelOpening()` → `m_transition.reset(); m_openGeometry = false;`
- members: `liquid_glass::EdgeTransition m_transition; SP<CSolidFill> m_solid; std::vector<SMeshRenderVertex> m_meniscusVertices;` (sized `LIQUID_MENISCUS_VERTEX_COUNT` in the constructor)
- `liquid_glass::Vec2 openOffset(const PHLWINDOW&) const;` next to `openCompensation`

**Step 2: Implementation.** In the anonymous namespace:

```cpp
    constexpr double BEHIND_EDGE = 2.0; // liquid starts 2 px behind the edge line so no seam shows at the bezel
    constexpr double HUGE_SPAN   = 1.0e6;

    Vec2 poseOffset(const SLiquidPose& pose) {
        const Vec2 t = pose.transition.offset();
        return {t.x + pose.openOffset.x, t.y + pose.openOffset.y};
    }

    Vec2 posedOffset(const SLiquidPose& pose, double u, double v, Vec2 size) {
        const Vec2 d = pose.mesh.sampleDisplacement(u, v);
        const Vec2 c = pose.compensation;
        const Vec2 o = poseOffset(pose);
        return {((u - 0.5) * size.x + d.x) * c.x + o.x, ((v - 0.5) * size.y + d.y) * c.y + o.y};
    }

    // Liquid point (t, n) relative to the capture centre: anchored to the edge, so only the native-open offset applies.
    Vec2 liquidPoint(const SLiquidPose& pose, double t, double n) {
        const Vec2 p = liquid_glass::edgePoint(pose.transition.frame(), t, n);
        return {p.x + pose.openOffset.x, p.y + pose.openOffset.y};
    }
```

Implement:
- `advanceLiquidPose` as specified in the header.
- `liquidVisibleBox`: if the transition is inactive → `nullopt`. Otherwise take the edge-line point `e = centre(logicalBox) + liquidPoint(pose, 0, 0)` and return, for example for Bottom, `{e.x - HUGE_SPAN, e.y - HUGE_SPAN, 2*HUGE_SPAN, HUGE_SPAN}` (visible side only). Mirror it for the other three edges.
- `liquidMeniscusExtents`: `nullopt` if `meniscusPeak() <= 0`. Otherwise use the bounding box of `centre + liquidPoint(t, n)` for `t ∈ {low, high}` and `n ∈ {−BEHIND_EDGE, peak}`.
- `liquidRenderExtents`: the existing window-sample loop using the new `posedOffset` (no gain), unioned with the meniscus extents. If active, intersect with the visible box. If the result is empty, return a 1×1 box at `centre + liquidPoint(centreT, 0)`, moved 1 px to the visible side.
- `fillLiquidMeniscusMesh`: for `i = 0..COLUMNS`, `t_i = lerp(low, high, i/COLUMNS)` and `h_i = transition.meniscus(t_i)`. Each column contributes two triangles with corners `liquidPoint(t_i, −BEHIND_EDGE)`, `liquidPoint(t_{i+1}, −BEHIND_EDGE)`, `liquidPoint(t_i, h_i)` and `liquidPoint(t_{i+1}, h_{i+1})`. Convert to device as `(centreDevice + p*scale − outputDevice.pos) / outputDevice.size`, where `centreDevice` is the centre of `sourceDevice`. Set `u = v = 0.5`. Resize `out` only when its size differs.

Delete the droplet-specific code: the `gain` usage in `liquidRenderExtents`/`fillLiquidRenderMesh` and the `pose.displacementGain` references.

`openOffset(window)`: `{0,0}` unless `m_openGeometry && window->presentation().animatingIn()`. Otherwise `goalCentre − currentCentre` of `GEOMETRIC_GOAL`/`GEOMETRIC_CURRENT` (finite checks as in `openCompensation`). `renderPose()` returns `{m_mesh, m_transition, openCompensation(w), openOffset(w)}`.

`beginOpen(frame, params, strength)`: `m_transition.begin(Transition::Open, frame, params, strength); m_openGeometry = m_transition.active();`.

`active()`: replace `m_droplet.active()` with `m_transition.active()`.

`transformedExtents`: unchanged in shape. It now calls the new `liquidRenderExtents`, which already includes the liquid and the clip. Expand by `EXTENTS_PADDING`, then intersect again with `liquidVisibleBox` so the padding never crosses the edge.

**Step 3: `transform()`.** After `fillLiquidRenderMesh(...)` and the clear draw:

```cpp
    const SLiquidPose pose = renderPose();
    // Nothing past the emerging screen edge is drawn, even where a neighbouring output would sample this buffer.
    if (const auto visible = liquidVisibleBox(pose, context.currentBox))
        renderData.damage.intersect(pixelBoxForLogical(*visible, SCALE).translate(-OUTPUTCANVAS.pos()));

    GL::CHyprOpenGLImpl::STextureRenderData textureData{};
    textureData.damage        = &renderData.damage;
    textureData.a             = 1.F;
    textureData.allowCustomUV = true;
    if (const auto liquid = liquidMeniscusExtents(pose, context.currentBox); liquid && m_solid) {
        if (const auto solid = m_solid->texture()) {
            const CBox LIQUIDBOX = liquid->copy().scale(SCALE);
            fillLiquidMeniscusMesh(pose, SOURCEBOX, SCALE, LIQUIDBOX, m_meniscusVertices);
            GL::g_pHyprOpenGL->renderTextureMesh(ctx, solid, LIQUIDBOX.copy().translate(-OUTPUTCANVAS.pos()), m_meniscusVertices, textureData);
        }
    }
    GL::g_pHyprOpenGL->renderTextureMesh(ctx, in.framebuffer->getTexture(), LOCALOUTPUTBOX, m_vertices, textureData);
```

Use the same `pose` for `fillLiquidRenderMesh`. Check the `CRegion::intersect` overload in `hyprutils/math/Region.hpp`; if it only takes a region, use `intersect(CRegion{box})`. If the nested check in Task 17 shows `renderTextureMesh` ignoring `textureData.damage`, wrap both draws in `g_pHyprOpenGL->scissor(ctx, clipBox)` / `disableScissor()`.

**Step 4:** The plugin will not build until Tasks 10–12 land. Build after Task 12. **Checkpoint** after Task 12.

### Task 10: Runtime: config, edge resolution, open wiring, mesh drive

**Files:** `src/PluginRuntime.hpp`, `src/PluginRuntime.cpp`.

**Step 1: Config.** Next to `m_mode`:

```cpp
    m_firstWindowSide = makeShared<Config::Values::CStringValue>(
        "plugin:liquid-glass:first_window_side", "Screen edge the first window on a workspace pops out of", "bottom",
        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {
            if (value == "bottom" || value == "top" || value == "left" || value == "right")
                return {};
            return std::unexpected(std::string("plugin:liquid-glass:first_window_side must be \"bottom\", \"top\", \"left\" or \"right\""));
        }});
```

Register it in the `addConfigValueV2` chain and add the member `SP<Config::Values::CStringValue> m_firstWindowSide;`.

**Step 2: Helpers** (declare them in the header under "Config and wobble conflict" / "Tracking"):

```cpp
liquid_glass::Edge CPluginRuntime::firstWindowEdge() const {
    const auto& side = m_firstWindowSide->value();
    if (side == "top") return liquid_glass::Edge::Top;
    if (side == "left") return liquid_glass::Edge::Left;
    if (side == "right") return liquid_glass::Edge::Right;
    return liquid_glass::Edge::Bottom;
}

bool CPluginRuntime::workspaceHasOtherWindows(const PHLWINDOW& window) const {
    for (const auto& other : Desktop::windowState()->windows()) {
        if (other && other.get() != window.get() && other->m_workspace == window->m_workspace && other->mapped() && !other->isHidden())
            return true;
    }
    return false;
}

std::optional<liquid_glass::EdgeFrame> CPluginRuntime::edgeFrameFor(const PHLWINDOW& window, bool opening) const {
    const auto monitor = window->m_monitor.lock();
    if (!monitor)
        return std::nullopt;
    const CBox current = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const CBox body    = opening ? window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_GOAL) : current;
    const CBox capture = window->getFullWindowBoundingBox();
    // Decoration margins (border, shadow) do not scale with native popin, so they carry over to the rest box.
    const CBox rest{body.x + (capture.x - current.x), body.y + (capture.y - current.y), body.w + (capture.w - current.w),
                    body.h + (capture.h - current.h)};
    const auto box = [](const CBox& b) { return liquid_glass::Box{b.x, b.y, b.w, b.h}; };
    return liquid_glass::resolveEdgeFrame(box(body), box(rest), box(monitor->logicalBox()), firstWindowEdge(),
                                          opening && !workspaceHasOtherWindows(window));
}
```

**Step 3: Open.** In `beginOpening`, after the mode/transitions guard:

```cpp
    const auto frame = edgeFrameFor(window, true);
    if (!frame)
        return; // no usable edge: the native opening runs, movement deformation unaffected
    auto* transformer = acquireTransformer(window);
    transformer->beginOpen(*frame, effectiveMaterialParams(), effectiveStrength());
```

Drop the `captureSize` parameter from `beginOpening` (declaration and both call sites).

**Step 4: Advance.** In `advanceWindow`, replace the three lines `mesh().advance`, `droplet().advance` and `refreshPose` with:

```cpp
    advanceLiquidPose(transformer->mesh(), transformer->transition(), elapsed, size, effectiveStrength());
```

In `sampleWindow`, delete `transformer->refreshPose({captureSize.x, captureSize.y});`.

**Step 5: Solid fill.** Add member `SP<CSolidFill> m_solid = makeShared<CSolidFill>();`, constructed before `m_close`. Pass it to `CLiquidCloseRenderer`'s constructor and to `emplace<CLiquidGlassTransformer>(window->m_self, effectiveMode(), effectiveMaterialParams(), m_solid)`. In the destructor, reset `m_solid` after `detachAll()`.

**Step 6: Close listener.**

```cpp
            if (transitionsEnabled() && eligible(window)) {
                auto* transformer      = transformerOf(window);
                const bool continuing  = transformer && transformer->transition().active();
                const auto closeFrame  = continuing ? std::optional<liquid_glass::EdgeFrame>{} : edgeFrameFor(window, false);
                if (transformer && transformer->active())
                    m_close->stage(window, transformer->renderPose(), effectiveStrength(), closeFrame);
                else {
                    const liquid_glass::LiquidMesh       mesh{effectiveMode(), effectiveMaterialParams()};
                    const liquid_glass::EdgeTransition   transition;
                    m_close->stage(window, SLiquidPose{mesh, transition}, effectiveStrength(), closeFrame);
                }
            }
```

### Task 11: Close renderer cutover

**Files:** `src/LiquidCloseRenderer.hpp`, `src/LiquidCloseRenderer.cpp`.

**Header**
- `#include "EdgeTransition.hpp"` replaces the droplet include. Add `#include "SolidFill.hpp"`.
- `SClosing`: replace `droplet` with `liquid_glass::EdgeTransition transition;`. Add:
  - `double strength = 0.0;`
  - `bool hasLiquid = false;`
  - `CBox liquidDevice;` (snapshot device px)
  - `std::vector<SMeshRenderVertex> liquidVertices;`

  Update the `owned` comment to: "transition drives shape and placement".
- `SPending`: replace `droplet` with `transition`. Add `std::optional<liquid_glass::EdgeFrame> closeFrame;`.
- `stage(const PHLWINDOW&, const SLiquidPose&, double strength, std::optional<liquid_glass::EdgeFrame> closeFrame);`
- Constructor `CLiquidCloseRenderer(HANDLE, SP<CSolidFill> solid)`, member `SP<CSolidFill> m_solid;`.
- `CLiquidClosePassElement::SData`: add `SP<Render::ITexture> solid; const std::vector<SMeshRenderVertex>* liquid = nullptr; CBox liquidBox;`.

**Implementation**
- `dropletSize()`: delete.
- `stage`: store `.transition = pose.transition` and `.closeFrame = closeFrame`.
- `onFadeoutCreated`, owned block:

  ```cpp
  if (liquid && !textureBlur) {
      record->transition = pending.transition;
      // Continuing an opening keeps its edge; otherwise the staged frame (invalid when none, which leaves it idle).
      record->transition.begin(Transition::Close, pending.closeFrame.value_or(record->transition.frame()), record->mesh.params(), pending.strength);
  }
  record->strength = pending.strength;
  record->owned    = record->transition.active() && record->sourceAlpha > 0.F;
  ```

- `fillVertices`:

  ```cpp
  const SLiquidPose pose{record.mesh, record.transition, record.compensation};
  const CBox        logical = record.sourceDevice.copy().scale(1.0 / record.scale);
  CBox              extents = liquidRenderExtents(pose, logical);
  extents.expand(EXTENTS_PADDING);
  record.extentsDevice = extents.copy().scale(record.scale);
  fillLiquidRenderMesh(pose, record.sourceDevice, record.scale, record.extentsDevice, Vec2{0.0, 0.0},
                       Vec2{record.snapshotSize.x, record.snapshotSize.y}, record.vertices);
  const auto liquid = liquidMeniscusExtents(pose, logical);
  record.hasLiquid  = liquid.has_value();
  if (liquid) {
      record.liquidDevice = liquid->copy().scale(record.scale);
      fillLiquidMeniscusMesh(pose, record.sourceDevice, record.scale, record.liquidDevice, record.liquidVertices);
  }
  ```

  The close does not clip explicitly: it draws only into its own monitor's framebuffer, which ends at the edge.
- `tick` owned branch: `if (record->transition.closed()) …` replaces the droplet check. Replace the two advance lines with `advanceLiquidPose(record->mesh, record->transition, elapsed, record->captureLogical, record->strength);`. The non-owned branch is unchanged.
- `renderBatch`, owned items:
  - `closed()` check on `transition`
  - `alpha = record->sourceAlpha`
  - `data.solid = m_solid ? m_solid->texture() : nullptr`
  - `data.liquid = record->hasLiquid ? &record->liquidVertices : nullptr`
  - `data.liquidBox = projectToOutput(record->liquidDevice, record->renderBox, record->snapshotSize)`

  The orphan pass uses `record->transition.closed()`.
- `CLiquidClosePassElement::draw`: before the window mesh, draw `if (m_data.liquid && m_data.solid) renderTextureMesh(ctx, m_data.solid, m_data.liquidBox, *m_data.liquid, textureData);`. `boundingBox` returns the union of `box` and `liquidBox` (when `liquid` is set), scaled and rounded as now.

### Task 12: Remove the droplet

**Step 1:** Delete `src/DropletTransition.hpp`, `src/DropletTransition.cpp` and `tests/DropletTransitionTests.cpp`. In `CMakeLists.txt`:
- replace `src/DropletTransition.cpp` with `src/EdgeTransition.cpp` in `add_library`
- remove the whole `droplet-transition-tests` block

**Step 2:** Confirm there are no references left:

```
grep pattern: DropletTransition|droplet|displacementGain|refreshPose|beadExtent   path: src tests CMakeLists.txt
```

Expected: no matches.

**Step 3:** `cmake --build build && ctest --test-dir build --output-on-failure`. Expected: no warnings, `liquid-mesh-tests` and `edge-transition-tests` pass (2/2). **Checkpoint.**

---

## Phase C: Nested verification and tuning (real compositor)

Use the README procedure. Never touch the primary instance.

### Task 13: Launch and load

```bash
env -u HYPRLAND_INSTANCE_SIGNATURE HYPRLAND_NO_SD_VARS=1 HYPRLAND_NO_SD_NOTIFY=1 \
  Hyprland --config /home/justin/projects/hypr-bubble/tests/nested/hyprland.lua &
hyprctl instances -j   # pick the entry whose pid == $! ; export NEST_SIGNATURE=<signature>
hyprctl -i "$NEST_SIGNATURE" plugin load /home/justin/projects/hypr-bubble/build/liquid-glass.so
hyprctl -i "$NEST_SIGNATURE" plugin list
```

Find the nested Wayland socket for capture: `ss -xlp | grep wayland-` and pick the socket owned by that PID. Then `export NEST_WL=wayland-N`. Capture bursts with `for i in $(seq -w 0 59); do WAYLAND_DISPLAY=$NEST_WL grim /tmp/edge/f$i.png; done` and build contact sheets with `ffmpeg -pattern_type glob -i '/tmp/edge/f*.png' -vf tile=6x10 sheet.png`. Store sheets in `.project-manager/evidence/edge/`. Dispatch opens and closes with the `hyprctl -i "$NEST_SIGNATURE" eval …` form recorded in `.project-manager/progress.md`. Test the exact syntax with a harmless call first.

### Task 14: Open-edge scenarios (dwindle fixture, `gaps_out = 24`)

Expected and observed per scenario, one contact sheet each:
1. Empty workspace, default `first_window_side = bottom`: the first kitty rises out of the bottom edge. Black fillets hug its sides, then a black neck stays under its bottom edge, snaps, and the residual peak sinks with one small bump. The window rests undeformed.
2. Second kitty: comes from the **right**. The first window retiles left at the same time.
3. Third kitty: comes from the **bottom** (bottom-right quarter).
4. Close all, set `first_window_side = "left"` (`hyprctl -i … keyword`, or a Lua `hl.config` eval), open one: comes from the **left**.
5. Floating centred window (temporary `eval` window rule for a class `floatme`, then `kitty --class floatme`): comes from the nearest edge on the tie rule (bottom on 16:9). It travels across the screen and the neck snaps at `Lp`, not at rest.
6. Confirm `GEOMETRIC_GOAL` is final at the opening edge: in scenario 2 the window must land exactly in the right half with no jump at the end. If it lands wrong, defer `beginOpening` by one tick or re-resolve on the first advance, and record the evidence.

### Task 15: Close scenarios

1. Close the right-half window: it drains into the **right** edge. The edge rises to meet it, the black hugs it while it crosses, then recoils.
2. Close during an opening (`Alt+C` within ~100 ms of opening): it continues back into the same edge without a jump.
3. Close while dragging a floating window: the drag bow carries into the drain.
4. `fadeOut`/`windowsOut` at 100 ms and at 1.5 s: the owned close always finishes its drain, and the native fade never reappears (same checks as `evidence/droplet/short-native-close-sheet.png` / `long-native-close-sheet.png`).

### Task 16: Modes and safety

1. `transitions = false`: native open/close.
2. `mode = "wiggle"`: native.
3. Scale 1.25 and 2.0 (fixture monitor scale): the black is flush with the screen edge, with no gap or seam, and no window texels show beyond the edge.
4. Unload during an opening and during a close, three times each: the compositor stays alive and nothing is left drawn.
5. Idle 6 s after everything settles: no plugin ticks, transforms or close draws, using the same counters as the previous acceptance where available.

### Task 17: Clip proof

In scenario 1 at the first captured frames, sample pixels in the bottom 2 rows. They must be black or wallpaper, never kitty colours, while the window is still mostly hidden. In the nested single output this mostly proves geometry. If a second nested output is available (`hl.monitor` with two headless outputs), place the window on the lower output's top edge and check that the upper output shows nothing of it. If damage clipping is ignored, apply the scissor fallback from Task 9.

### Task 18: Side-by-side against the reference

Extract the reference pop-out at 30 fps (`ffmpeg -ss 1.15 -t 0.8 -i ref.mp4 -vf fps=30,crop=360:220:220:170,tile=4x6`). Extract the matching nested frames of scenario 2, rotated so the edge is at the bottom (`transpose`). Put them next to each other in `evidence/edge/reference-vs-open.png`.

### Task 19: Tune (named constants only)

Adjust only the constants in `EdgeTransition.cpp`'s anonymous namespace and `LIQUID_EMERGE_MESH_DRIVE`. Targets from section 0:
- neck visible ≥ 80 ms
- snap → flat ≈ 150 ms, with one visible secondary bump
- tip never visibly detaches before the snap
- body bow subtle, not rubbery (if the trailing side bows > ~15% of the window depth, lower `LIQUID_EMERGE_MESH_DRIVE`)

After each change, rerun `ctest` (the invariants must hold) and recapture scenario 2. **Checkpoint.**

---

## Phase D: Docs and cleanup

### Task 20: README and fixture

- Intro paragraph (line 3): replace the droplet sentence with: windows pop out of a screen edge through a black edge liquid and drain back into one.
- Lua example (lines 33–43): add `first_window_side = "bottom"`.
- Config table: add the row ``| `plugin:liquid-glass:first_window_side` | string / `bottom` | Edge (`bottom`, `top`, `left`, `right`) the first window on a workspace pops out of; later windows use the nearest screen edge |``. Update the `transitions` row.
- `### Liquid material`:
  - rewrite the **Open** and **Close** bullets from section 1 (edge rule, travel spring, fillet/neck/snap/recoil, constants, monitor-edge choice)
  - state that native `windowsIn` scale and translation are neutralised during the pop-out
  - state that native `fadeIn` alpha still applies (see the open questions)
- Test sentence (line 84): `tests/EdgeTransitionTests.cpp` covers edge choice, hidden start, pop overshoot and exact rest, neck attach/snap/recoil, liquid bounds and continuity, refresh-rate agreement, close reach/drain/continuity, invalid input. Evidence lives in `.project-manager/evidence/edge/`.
- `### Closing seam`: owned closes keep the snapshot alpha and disappear through the edge. The drain uses the edge chosen at close.
- `tests/nested/hyprland.lua`: add `first_window_side = "bottom"` to the commented plugin settings block. No other changes.
- Remove `/tmp/edge` captures and any throwaway helpers. Append the progress entry to `.project-manager/progress.md` with the build/CTest result, the evidence list and the final fingerprint.

---

## Risks and tradeoffs

- **Native `fadeIn`** still fades the window and its liquid in from alpha 0 during the pop, because a window transformer cannot override presentation alpha. Mitigation: document that `fadeIn` should be disabled for this mode. A plugin-owned alpha would need another hook (not planned).
- **Translucent windows** (`inactive_opacity < 1`) show the tucked black tip through their near edge. Keep the tuck ≤ 20 px. Revisit only if it is visible in Task 14.
- **Monitor edge vs work area:** windows emerge under bars and docks on that edge (layers stay on top). This is intentional ("out of the screen"). Using `logicalBoxMinusReserved()` instead is a one-line change in `edgeFrameFor`.
- **Goal geometry timing** at `openLate` is assumed final. Task 14.6 checks this.
- **Sliding `windowsOut` styles** move an owned close because its placement still rides the native fadeout centre (existing behaviour). Popin, the fixture default, has no translation.
- **Wide skirts at screen corners** extend along the edge past the monitor corner. They are clipped by the framebuffer, and the liquid is tangentially centred on the window.

## Open questions (defaults chosen; change before implementation if wrong)

1. **Close side:** the nearest edge at close time (chosen), not the edge the window opened from.
2. **Liquid colour:** a constant opaque black (chosen), per the request. It is not configurable.
3. **Body drag during the pop:** on, at 0.5 drive (chosen). Set `LIQUID_EMERGE_MESH_DRIVE = 0` for a rigid pop.

---

## Addendum (2026-11-07, user approval): defaults become user configuration

The user approved the plan and asked that every default chosen above be configurable. These settings join `first_window_side`; all are registered with `HyprlandAPI::addConfigValueV2` beside the existing values and documented in the README config table and Lua example.

| Key | Type / default | Behaviour |
|---|---|---|
| `plugin:liquid-glass:edge_bounds` | string / `screen` | `screen`: edges are `CMonitor::logicalBox()` (windows come out from under bars). `work_area`: `CMonitor::logicalBoxMinusReserved()`. Validator rejects anything else |
| `plugin:liquid-glass:close_side` | string / `nearest` | `nearest`: drain into the nearest valid edge. `origin`: drain into the edge the window opened from (remembered per tracked window when its opening began), via `resolveEdgeFrame(..., fallback = originEdge, preferFallback = true)`; unknown or no-longer-valid origin falls back to nearest. A close interrupting an opening always continues on the opening's edge |
| `plugin:liquid-glass:liquid_color` | color / `0xff000000` (opaque black) | Colour of the edge liquid, `CColorValue`. Alpha 0 draws no liquid. `CSolidFill` builds its 4x4 texture from the colour as premultiplied `DRM_FORMAT_ARGB8888` (memory order B,G,R,A), `opaque = (alpha == 255)`, and rebuilds it only when the configured colour changes |
| `plugin:liquid-glass:emerge_drag` | float / `0.5`, range 0–1 | Share of pop-out travel fed to the movement field (replaces the constant `LIQUID_EMERGE_MESH_DRIVE`). `0` = rigid pop. `advanceLiquidPose` takes it as a parameter; a closing record stores the value captured at staging |

Not configurable, because it is native: `fadeIn` alpha (README note stays).

---

## Addendum 2 (2026-11-07, user feedback after first nested run): the window mimics the purple circle

The user's feedback: "The windows are popping out at their full size. They should pop out relatively the same size as the pop animation then grow … I'd actually like the windows to mimic the purple circle."

The reference circle pops out of the bar at a small size and hovers just above it, then widens along the bar into a pill (the menu). On return it shrinks back to a circle and drops into the bar. The window now follows that choreography. Its content is squeezed into the bead by a radially homogeneous outline map, the same technique the removed droplet used, so the image never folds. The edge-liquid model (fillet → neck → snap → recoil, reach/attach/crossing on close) stays. Its sizes now scale with the bead, not the window.

### Contract changes (`src/EdgeTransition.hpp`, PM-owned public section)
- `Vec2 offset()` is replaced by `Vec2 map(Vec2 fromCentre, Vec2 size)`. This is the displayed position relative to the rest capture centre. `size` is the capture size (× compensation when opening, as before). Exact identity when idle.
- `double displacementGain(const LiquidMesh&, Vec2 size)` is reinstated. Use the removed `DropletTransition::displacementGain` algorithm, with `map` in place of the droplet map: 33×33 grid, keep ≥ 20% of the map-only area, gains 1, 1/2, … over 8 halvings, else 0, and 1 when inactive or the mesh is settled.
- `takeTravel()`: the delta of the displayed centre along the inward normal since the last call. `begin()` does not count as travel.
- The meniscus API and `EdgeFrame`/`resolveEdgeFrame` are unchanged.

### Geometry (all logical px, edge frame n/t)
- `halfT`/`halfN`: capture half-extents along the tangent and the normal. `D = frame.edgeDistance`: n of the rest capture centre. `c = (bodyT0+bodyT1)/2`: the window's tangential centre. The bead is always centred tangentially at `c`.
- Bead radius `r = clamp(0.14 · min(bodyWidth, bodyDepth), 20, 80) · k`, with `k = clamp(strength/0.65, 0.25, 1.5)`. Also clamp `r ≤ 0.95·min(halfT, halfN)`.
- Liquid sizes scale with the bead: `Fs = clamp(0.35 r, 5, 20)`, `Lp = clamp(1.1 r, 16, 72)`, `σn = clamp(0.9 r, 12, 64)`, `σc = 2.5 Fs`. During crossing the plateau equals the displayed tangential half-extent (the bead radius), not the window width.
- Displayed shape:
  - centre n position `nc`
  - extents `eT = r + (halfT − r)·sT` and `eN = r + (halfN − r)·sN`, where the spreads `sT`/`sN` run 0 at the bead to 1 at rest and may overshoot
  - `roundness = (1 − clamp(min(sT, sN), 0, 1))^1.5`
  - map: the rest point normalised by the half-extents → superellipse radial gain (n = 2/roundness; gain 1 at roundness 0) → scaled by (eT, eN). Then translate along the inward normal by `nc − D`. The tangential translation is 0, because the bead is centred at the window's tangential centre and the rest centre is the capture centre.
- Bead near side, used by the liquid: `nNear = nc − eN`. Bead far side: `nc + eN`.
- Hover point: `nh = r + 1.25·Lp`, the bead centre while it hovers. The neck therefore stretches past `Lp` and snaps during the hop.

### Open choreography (default elasticity 0.5, momentum 0.5)
1. **Hop.** `nc` follows a damped spring from `−r` (bead fully hidden) to `nh`: ω = 22·2^(e−0.5) rad/s, ζ = lerp(0.8, 0.45, momentum). `sT = sN = 0`. The liquid follows the previous rules (crossing fillets ramp in as the bead's far side breaks the surface; attached neck tucked `Fs` under the bead; snap at `nNear ≥ Lp`, at the apex or at rest; recoil spring).
2. **Grow** starts at the snap. The spreads follow springs to 1: `sT` uses ω = 15·2^(e−0.5) and `sN` uses ω·0.75, with ζ = lerp(0.85, 0.5, momentum). The bead therefore widens along the edge first, like the circle becoming a pill, then grows away from the edge. The centre `nc = hop + (D − nh)·sN` (unclamped, so it carries the overshoot). The hop spring keeps settling to `nh`.
3. **Settle.** When the hop, both spreads and the recoil are quiet (0.15 px / 2 px/s on the displayed centre and extents), `reset()` gives exact identity.

### Close choreography (duration `T = 0.46·2^(0.5−e)`)
Start from the current state, so the pose is continuous. From idle that is `sT = sN = 1` and `nc = D`; an interrupted open keeps its spreads, `nc` and edge. Let `p = t/T`.
- **Shrink**, using `easeInOutCubic`: `sN(p) = sN0·(1 − e(p/0.45))`, `sT(p) = sT0·(1 − e((p − 0.05)/0.5))`. Normal first, then tangential: the pill folds back into a circle.
- **Centre:** `nc(p) = lerp(nc0, nh, e(p/0.5))` while shrinking, then the dive `nc = lerp(nh, −eN − 2, easeInCubic((p − 0.45)/0.55))` for p ≥ 0.45. Compose these so the result is continuous at p = 0.45.
- **Liquid:** the same reach/attach/crossing rules, evaluated on the bead's `nNear`, with the 0.08 s engage ramp. A close that continues an opening starts its liquid from the current peak, not from the close target (review finding F1). When the drain completes, the recoil follows; `closed()` once quiet.
- Owned closes keep the snapshot's native alpha.

### Movement field (`emerge_drag`)
`takeTravel()` × drive is fed to the mesh as before. The shape map is nonlinear while active, so the renderer composes the displacement through `displacementGain` (as the droplet did).

### Review findings from EdgeReview carried into this round
- **F1** (snap on a close begun from a hidden opening): fix in the model as stated above, and add a hidden-start continuity test.
- **F4**: owned-close draws intersect their damage with the visible half-plane box.
- **F6**: correct the `spring()` stability comment.
- **F7**: clear `STracked::openEdge` at every open attempt.
- **F2**: per-frame `makeUnique` pass elements are the host API's required pattern and were already present. Avoid only the `CRegion` copy where it is cheap; no pooling. Document this.
- **F3**: the >16-item batch fallback is pre-existing, documented behaviour; unchanged.
- **F5**: compensation is frozen at stage; pre-existing; unchanged unless nested evidence shows a jump.
