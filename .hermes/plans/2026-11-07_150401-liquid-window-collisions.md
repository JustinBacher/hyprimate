# Liquid Window Collisions Implementation Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Under `mode = "liquid"`, windows react to each other as soft bodies. A moving window reaches toward a nearby window, presses its face flat on contact and knocks the struck window into a slosh. If pushed hard enough it pops through. Tiled windows that cross each other during layout animations (e.g. a swap from top-right to bottom-left) collide the same way. One new boolean, `plugin:liquid-glass:collisions` (default `false`), turns all of this on or off.

**Architecture:** Collision response is **visual only**. Real window positions, layout and input hit-testing never change. A new pure module `src/LiquidContact.{hpp,cpp}` handles three things: it resolves the facing relation of two window rectangles, runs a per-pair contact state machine (Apart → Reaching → Pressing → Latched), and converts world geometry into capture-local `ContactInput`s. `LiquidMesh` gets one new entry point, `setContacts()`, which turns those inputs into an extra per-node target field (dent, spread, meniscus) plus a one-shot impact velocity. The existing Liquid springs relax toward that field, so contact inherits the material's viscosity, elasticity, momentum, bounds and triangle guard. `CPluginRuntime::onTick` gains a collision pass between sampling and advancing.

**Tech Stack:** C++26, Hyprland 0.57.0 plugin API (pinned `0aa6745f`), CMake/Ninja, CTest with the repo's own `CHECK` harness, nested Hyprland for visual verification.

---

## Research: Apple Liquid Glass (inspiration, not a spec)

Sources read for this plan:

- WWDC25 "Meet Liquid Glass": https://developer.apple.com/videos/play/wwdc2025/219/
- SwiftUI `GlassEffectContainer`: https://developer.apple.com/documentation/swiftui/glasseffectcontainer
- "Applying Liquid Glass to custom views": https://developer.apple.com/documentation/swiftui/applying-liquid-glass-to-custom-views
- HIG Materials: https://developer.apple.com/design/human-interface-guidelines/materials

What Apple says about how the material behaves, and what this plan takes from it:

| Apple behaviour (quoted or paraphrased from the sources) | Translation for windows in this plugin |
|---|---|
| "behaves and moves organically in a manner that feels more like a lightweight liquid"; "an inherent gel-like flexibility … moves in tandem with your interaction" (219) | Contact is not a rigid stop. Both faces give way through the existing gel lattice: dent, sideways spread, then a rebound. |
| Container shapes: "As shapes near one another, their paths start to blend into one another. The higher the spacing, the sooner blending begins" (`GlassEffectContainer`); morph happens "when the eraser's nearest edge is less than or equal to the container's spacing" (custom views article) | **Reach / meniscus:** inside a fixed reach (40 px gap), facing edges bulge toward each other and "kiss" before contact. When the windows separate past 1.5× reach, the bulge releases with a snap (pinch-off). The bridge itself can't be built: this plugin draws no shader/fill between windows. The bulge is the deformation-only analogue. |
| "the glow spreads throughout the element and onto any Liquid Glass elements nearby, interacting with the flexible properties of the material" (219) | **Energy transfer:** at impact, the struck window gets an inward velocity kick sized by closing speed and relative area. The smaller window sloshes more. |
| "always avoid glass on glass. Stacking Liquid Glass elements on top of each other can quickly make the interface feel cluttered" (219) | While pressing, the faces flatten so the two silhouettes meet at a shared plane instead of overlapping. Deep pushes beyond the material's capacity pop through. |
| HIG: Liquid Glass is reserved for the floating navigation layer, not content | Known departure. The user's creative direction applies the material to whole windows and explicitly does not aim for an exact Apple recreation. |

---

## Context and evidence (read before starting)

- Movement material: `src/LiquidMesh.cpp`. 9×9 lattice (`kSide`, `kCells`), fixed 1/240 s substeps, at most 12 per advance. The Liquid substep lives in `integrateLiquidSubstep` (`src/LiquidMesh.cpp:372-465`). Each node relaxes toward `lagField` with stiffness blended by grip (`a[i] = -stiffness*(e - lagField) - damping*v + coupled + inertia`, lines 451-452). Bounds come from `displacementLimit` (line 129, anonymous namespace today) plus `softSaturate`. `constrain` (608-659) halves the field until every triangle keeps ≥ 20% of rest area. The quiet reset fires after 3 quiet substeps (553-561).
- Lattice coordinates: node `(x, y)` sits at rest position `(W·x/8, H·y/8)` in **capture-local logical px**. The capture is `window->getFullWindowBoundingBox()`, i.e. decorated and including the shadow extents (`PluginRuntime.cpp:284`, `LiquidGlassTransformer.cpp:70`).
- Runtime: `CPluginRuntime::onTick` (`src/PluginRuntime.cpp:249-273`) currently samples and then advances each window in turn. Config values are created in the constructor (`:40-64`) with `addConfigValueV2`. Window tracking struct `STracked` is at `src/PluginRuntime.hpp:32-44`.
- Host APIs verified in pinned headers (`/var/cache/hyprpm/justin/headersRoot/include/hyprland/src`):
  - `CWindow::isFloating()`, `isHidden()`, `m_workspace`, `m_state & WINDOW_STATE_PINNED`, `geometricBox(GEOMETRIC_CURRENT)`, `getFullWindowBoundingBox()`: `desktop/view/window/Window.hpp`
  - `presentation().borderSize()`, `presentation().floatingOffset()`, `presentation().animatingIn()`: `desktop/view/window/WindowPresentation.hpp`
  - `CHLWorkspace::visible()`, `m_renderOffset`: `workspace/HLWorkspace.hpp`, `workspace/presentation/WorkspacePresentable.hpp`
  - `Desktop::windowState()->windows()` (z-ordered list, not needed here)
- No Git repository; initialisation is not authorised (`.project-manager/progress.md:4`). **Skip all commit steps**; checkpoint by running the tests.
- Primary desktop configuration and plugin loading stay untouched. Do all visual work in the nested compositor (README "Isolated nested verification").

## Design decisions

| Decision | Choice | Why |
|---|---|---|
| Effect on real geometry | None. Visual deformation only; positions, layout and hit-testing unchanged | The plugin is a transformer; moving windows would fight layouts and user drags |
| Scope | Liquid mode only; Wiggle, `strength = 0`, disabled/inert plugin → no collision | User asked for Liquid |
| Config | One bool `plugin:liquid-glass:collisions`, default **`false`** | Toggle requested; opt-in keeps existing configs visually identical. Numeric tuning stays internal (YAGNI) |
| Collision body | `geometricBox(GEOMETRIC_CURRENT)` expanded by `presentation().borderSize()`, translated by `floatingOffset()`; shadows ignored | The visible window plus border is what "touches" |
| Pairs considered | Same workspace (or pinned + same monitor), workspace visible. **All** combinations collide: floating–floating, floating–tiled, and tiled–tiled while layout animations carry them across each other (e.g. a swap that sends a tile from top-right to bottom-left through its neighbours) | User requirement (2026-11-07). Tiled windows never overlap at rest, so tiled–tiled contact only ever happens during animated repositioning |
| Excluded windows | Fullscreen/maximised, hidden (group tabs), opening (`animatingIn` or droplet active), closing (detached), during workspace render-offset animation | Prevents spurious contact from native animations |
| Contact geometry | Axis-aligned rectangles; separating axis = smaller overlap, with 0.75 hysteresis against axis flip-flop; corner-to-corner (no tangential overlap) → no contact | Windows are AABBs; flips would jitter |
| Interface | Each window flattens by **half** the penetration (shared mid-plane), soft-saturated to `displacementLimit` | Symmetric; the two silhouettes meet instead of overlapping |
| Pass-through | When half-penetration > 1.25× the smaller window's limit, the pair latches and both dents spring back | Material capacity is bounded (≤ 120 px / 20%), and a pop reads as organic |
| Static overlap | Contact only engages with relative motion (> 8 px/s). When motion stops, engagement decays over 0.35 s and the pair latches inert until it separates. A pair whose windows have **never been observed apart** (stacked before the drag) starts latched; a pair observed apart, including diagonally, presses on its first overlapping tick | A permanent dent would keep transformers active and distort content; dragging one of two stacked windows must not suddenly dent the other; tiles crossing diagonally must still collide |
| Reach / meniscus | Pairs with at least one floating window only: engages at gap < 40 px; bulge = `min(0.5·gap, 10 px)` on the middle half of the facing span; releases at gap > 60 px. Tiled–tiled pairs are **press-only** | Apple container "spacing" blend. Tiled neighbours always sit within reach at `gaps_in`, so a meniscus there would bulge every edge on every layout change |
| Pair tracking | Every candidate pair whose bodies are within 60 px on both axes is tracked every tick, moving or not; pairs farther apart are dropped | A pair must be seen apart *before* it overlaps, or a fast diagonal tile swap would be mistaken for stacked windows. Rect checks are O(n²) on visible windows, negligible |
| Impact | One-shot inward kick on the tick contact begins: `0.6 · closingSpeed · strength · areaOther/(areaSelf+areaOther)`, capped in the mesh at `1.5 · limit · ω_body` | Momentum-like transfer; the smaller window moves more |
| Mesh integration | Extra target field added to `lagField`, computed once per `setContacts` call (once per tick), not per substep | Reuses springs, bounds and guard; no per-substep cost |
| Per-window cap | 4 contacts (deepest by `|depth|` kept); 64 pairs total, reserved once | Resting tiled grids keep ~2 pairs per window tracked; bounded, allocation-free steady state |

**Decisions (confirmed by user, 2026-11-07):**

1. Default `collisions = false`.
2. Pairs with a floating window collide as planned; tiled windows additionally collide with each other while layout animations move them across each other (press-only, no meniscus).
3. A held overlap relaxes rather than staying dented.

## Files

- Create: `src/LiquidContact.hpp`, `src/LiquidContact.cpp`, `tests/LiquidContactTests.cpp`
- Modify: `CMakeLists.txt`, `src/LiquidMesh.hpp`, `src/LiquidMesh.cpp`, `tests/LiquidMeshTests.cpp`, `src/PluginRuntime.hpp`, `src/PluginRuntime.cpp`, `README.md`, `tests/nested/hyprland.lua`, `.project-manager/progress.md`

Build/test commands used throughout:

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/c++ -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Baseline before any change: `ctest` → 2/2 tests pass (`liquid-mesh-tests`, `droplet-transition-tests`). Record the output.

---

## Phase A — Mesh contact field (pure, unit-tested)

### Task 1: Export `displacementLimit`

**Objective:** Let the contact module use the same bound as the mesh.

**Files:** Modify `src/LiquidMesh.hpp`, `src/LiquidMesh.cpp:128-131`

**Step 1:** In `LiquidMesh.hpp`, after `struct MaterialParams`, declare:

```cpp
// Liquid displacement bound in logical px: min(120, 20% of the shorter capture side).
double displacementLimit(Vec2 size) noexcept;
```

**Step 2:** Move the definition at `LiquidMesh.cpp:128-131` out of the anonymous namespace. Put it directly after `} // namespace` at line 284, inside `namespace liquid_glass`, unchanged in body. Callers in `constrain` keep working.

**Step 3:** `cmake --build build && ctest --test-dir build --output-on-failure` → 2/2 pass.

### Task 2: `ContactInput` and `setContacts` API (declarations)

**Objective:** Define the mesh-facing contract.

**Files:** Modify `src/LiquidMesh.hpp`

**Step 1:** Add `#include <span>`. After `displacementLimit`, add:

```cpp
// One contact on this window's lattice, in capture-local logical px (origin = capture top-left).
struct ContactInput {
    unsigned axis          = 0;   // 0 = x, 1 = y
    double   sign          = 1.0; // +1: the other window lies toward +axis; -1: toward -axis
    double   face          = 0.0; // this window's facing body edge along `axis`, capture-local px
    double   extent        = 0.0; // body length along `axis`, px (> 0)
    double   spanMin       = 0.0; // tangential contact range on the other axis, capture-local px
    double   spanMax       = 0.0;
    double   tangentExtent = 0.0; // body length on the tangential axis, px (> 0)
    double   depth         = 0.0; // > 0 flattens the face inward; < 0 reaches outward (meniscus); px, mesh-saturated
    double   impulse       = 0.0; // one-shot inward speed in px/s (>= 0), applied once per call
};
inline constexpr std::size_t kMaxContacts = 4;
```

**Step 2:** In `class LiquidMesh` public section add:

```cpp
    // Replaces the contact target field (Liquid only; ignored for Wiggle and invalid input).
    // Impulses kick velocities immediately. Call at most once per tick, before advance().
    void setContacts(std::span<const ContactInput> contacts, Vec2 size) noexcept;
    bool hasContactTarget() const noexcept { return m_hasContactTarget; }
```

Private members, after `m_grip`:

```cpp
    // Liquid-only: per-node contact target added to the lag field; constant across the substeps of one tick.
    std::array<Vec2, 81> m_contactTarget{};
    bool                 m_hasContactTarget = false;
```

**Step 3:** Add an empty-body `setContacts` definition to `LiquidMesh.cpp` so it links: `void LiquidMesh::setContacts(std::span<const ContactInput>, Vec2) noexcept {}`. Build → compiles.

### Task 3: Failing mesh tests for contact behaviour

**Objective:** Pin the observable contract before implementing.

**Files:** Modify `tests/LiquidMeshTests.cpp` (add before `main`, call from `main` after `testViscosityBoundariesAndCadence()`)

**Step 1:** Add helpers and tests:

```cpp
using liquid_glass::ContactInput;

// Body == capture for test simplicity; the other window lies to the right (+x).
ContactInput rightContact(Vec2 size, double depth, double impulse = 0.0) {
    return ContactInput{.axis = 0, .sign = 1.0, .face = size.x, .extent = size.x, .spanMin = 0.0, .spanMax = size.y,
                        .tangentExtent = size.y, .depth = depth, .impulse = impulse};
}

// Re-applies the same contact every frame for `seconds`, like the runtime does each tick.
void holdContact(LiquidMesh& mesh, const ContactInput& c, double seconds, double hz, Vec2 size, bool requireValid = true) {
    const int frames = static_cast<int>(std::lround(seconds * hz));
    for (int f = 0; f < frames; ++f) {
        mesh.setContacts(std::span<const ContactInput>(&c, 1), size);
        mesh.advance(1.0 / hz, size);
        if (requireValid) {
            CHECK(latticeValid(mesh, size));
            CHECK(renderValid(mesh, size));
        }
    }
}

void testContactFlattensAndSpreadsFace() {
    LiquidMesh mesh{MotionMode::Liquid};
    holdContact(mesh, rightContact(kWindow, 30.0), 0.8, 60.0, kWindow);
    const auto& d = mesh.displacements();
    CHECK(d[node(8, 4)].x <= -0.8 * 30.0);       // face centre pressed inward
    CHECK(d[node(8, 4)].x >= -1.15 * 30.0);
    CHECK(std::abs(d[node(0, 4)].x) <= 0.2 * 30.0); // far edge barely moves
    CHECK(d[node(8, 0)].y < -1.0);                 // face corners spread outward
    CHECK(d[node(8, 8)].y > 1.0);
}

void testContactMeniscusReachesOut() {
    LiquidMesh mesh{MotionMode::Liquid};
    holdContact(mesh, rightContact(kWindow, -10.0), 0.8, 60.0, kWindow);
    CHECK(mesh.displacements()[node(8, 4)].x >= 0.8 * 10.0);
}

void testContactMaxDepthStaysValid() {
    for (const Vec2 size : {Vec2{600.0, 400.0}, Vec2{120.0, 600.0}, Vec2{600.0, 120.0}}) {
        LiquidMesh mesh{MotionMode::Liquid};
        const double limit = liquid_glass::displacementLimit(size);
        holdContact(mesh, rightContact(size, 10.0 * limit), 0.8, 60.0, size);
        const double face = -mesh.displacements()[node(8, 4)].x;
        CHECK(face <= limit + 1e-9);
        CHECK(face >= 0.7 * limit); // no halving collapse of the whole field
    }
}

void testContactReleaseSettlesExactly() {
    LiquidMesh mesh{MotionMode::Liquid};
    holdContact(mesh, rightContact(kWindow, 30.0), 0.5, 60.0, kWindow);
    mesh.setContacts({}, kWindow);
    CHECK(!mesh.hasContactTarget());
    for (int f = 0; f < 180 && !mesh.settled(); ++f)
        coastFrame(mesh, 60.0, kWindow);
    CHECK(mesh.settled());
    CHECK(maxMagnitude(mesh.displacements()) == 0.0);
}

// Impulse at rest: face centre goes inward (-x), then returns. Tolerance mirrors testLiquidMomentumOvershoot (2% of peak).
struct Rebound {
    double inward = 0.0; // most negative face-centre x
    double past   = 0.0; // most positive face-centre x (overshoot past rest)
};

Rebound traceImpulse(double momentum) {
    LiquidMesh mesh{MotionMode::Liquid, MaterialParams{.momentum = momentum}};
    const ContactInput kick = rightContact(kWindow, 0.0, 1500.0);
    mesh.setContacts(std::span<const ContactInput>(&kick, 1), kWindow);
    mesh.setContacts({}, kWindow); // impulse already applied; target is zero
    Rebound r;
    for (int f = 0; f < 120; ++f) {
        coastFrame(mesh, 60.0, kWindow);
        r.inward = std::min(r.inward, mesh.displacements()[node(8, 4)].x);
        r.past   = std::max(r.past, mesh.displacements()[node(8, 4)].x);
        CHECK(latticeValid(mesh, kWindow));
    }
    return r;
}

void testContactImpulseFollowsMomentum() {
    const Rebound none = traceImpulse(0.0);
    const Rebound standard = traceImpulse(0.5);
    CHECK(none.inward < -10.0 && standard.inward < -10.0);
    CHECK(none.past <= 0.02 * -none.inward);
    CHECK(standard.past > none.past + 1.0);
}

void testContactIgnoredOutsideLiquid() {
    const ContactInput c = rightContact(kWindow, 30.0, 1500.0);
    LiquidMesh wiggle{MotionMode::Wiggle};
    wiggle.setContacts(std::span<const ContactInput>(&c, 1), kWindow);
    CHECK(!wiggle.hasContactTarget() && wiggle.settled());
}

void testContactRejectsInvalidInput() {
    for (ContactInput c : {rightContact(kWindow, kNaN), rightContact(kWindow, 30.0, kInfinity), rightContact(kWindow, 30.0, -5.0)}) {
        LiquidMesh mesh{MotionMode::Liquid};
        mesh.setContacts(std::span<const ContactInput>(&c, 1), kWindow);
        CHECK(!mesh.hasContactTarget() && mesh.settled());
    }
    ContactInput badExtent = rightContact(kWindow, 30.0);
    badExtent.extent = 0.0;
    LiquidMesh mesh{MotionMode::Liquid};
    mesh.setContacts(std::span<const ContactInput>(&badExtent, 1), kWindow);
    CHECK(!mesh.hasContactTarget());
}

void testContactRefreshRateAgreement() {
    const auto pressAt = [](double hz) {
        LiquidMesh mesh{MotionMode::Liquid};
        holdContact(mesh, rightContact(kWindow, 30.0), 0.15, hz, kWindow, false);
        return mesh.displacements();
    };
    const auto s60 = pressAt(60.0);
    CHECK(maxDifference(pressAt(144.0), s60) <= 1.5);
    CHECK(maxDifference(pressAt(240.0), s60) <= 1.5);
}
```

Add `#include <span>` to the test file. Register all eight in `main`.

**Step 2:** `cmake --build build && ctest --test-dir build -R liquid-mesh --output-on-failure`
Expected: FAIL. The flatten, meniscus, max-depth and impulse checks fail; the ignore and invalid-input checks pass trivially.

### Task 4: Implement the contact target field

**Objective:** Make Task 3 pass.

**Files:** Modify `src/LiquidMesh.cpp`

**Step 1:** In the anonymous namespace, after the Liquid constants, add:

```cpp
// Contacts. Normal falloff spans this fraction of the body length along the contact normal; smoothstep keeps the
// dent C1 and its steepest compression at 1.5·depth/falloff, so a saturated depth cannot fold a triangle.
constexpr double kContactFalloff = 0.45;
// Tangential fade outside the contact span, as a fraction of the body's tangential length.
constexpr double kContactTangentFalloff = 0.25;
// Pressed faces widen sideways by this fraction of the dent depth at the body's tangential extremes.
constexpr double kContactSpread = 0.3;
// Impact kick cap, in limit · body omega (px/s).
constexpr double kContactImpulseCap = 1.5;

double smoothstep01(double t) noexcept {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Scalar version of softSaturate: identity up to 0.75·limit, then tanh toward limit; keeps the sign.
double saturateDepth(double depth, double limit) noexcept {
    const double magnitude = std::abs(depth);
    const double knee = 0.75 * limit;
    if (magnitude <= knee)
        return depth;
    const double span = limit - knee;
    return std::copysign(knee + span * std::tanh((magnitude - knee) / span), depth);
}

bool validContact(const ContactInput& c) noexcept {
    return c.axis <= 1 && (c.sign == 1.0 || c.sign == -1.0) && std::isfinite(c.face) && std::isfinite(c.extent) &&
           c.extent > 0.0 && std::isfinite(c.spanMin) && std::isfinite(c.spanMax) && c.spanMax >= c.spanMin &&
           std::isfinite(c.tangentExtent) && c.tangentExtent > 0.0 && std::isfinite(c.depth) && std::isfinite(c.impulse) &&
           c.impulse >= 0.0;
}

double& component(Vec2& v, unsigned axis) noexcept {
    return axis == 0 ? v.x : v.y;
}
```

**Step 2:** Define `setContacts`:

```cpp
void LiquidMesh::setContacts(std::span<const ContactInput> contacts, Vec2 size) noexcept {
    m_contactTarget.fill(Vec2{});
    m_hasContactTarget = false;
    if (m_mode != MotionMode::Liquid || !hasFiniteParams(m_params) || !isValidSize(size))
        return;

    const double limit = displacementLimit(size);
    const double kickCap = kContactImpulseCap * limit * std::sqrt(m_bodyStiffness);
    bool kicked = false;
    const std::size_t count = std::min(contacts.size(), kMaxContacts);
    for (std::size_t c = 0; c < count; ++c) {
        const ContactInput& in = contacts[c];
        if (!validContact(in))
            continue;
        const unsigned n = in.axis;
        const unsigned t = 1u - in.axis;
        const double depth = saturateDepth(in.depth, limit);
        const double kick = std::min(in.impulse, kickCap);
        const double falloff = kContactFalloff * in.extent;
        const double fade = kContactTangentFalloff * in.tangentExtent;
        const double centre = 0.5 * (in.spanMin + in.spanMax);
        for (unsigned y = 0; y < kSide; ++y) {
            for (unsigned x = 0; x < kSide; ++x) {
                Vec2 rest = restPosition(GridPoint{x, y}, size);
                const double rn = component(rest, n);
                const double rt = component(rest, t);
                // >= 0 inside the body; < 0 in the shadow margin beyond the face (moves with the face).
                const double inward = in.sign * (in.face - rn);
                const double wN = 1.0 - smoothstep01(inward / falloff);
                const double outside = std::max({in.spanMin - rt, rt - in.spanMax, 0.0});
                const double wT = 1.0 - smoothstep01(outside / fade);
                const std::size_t i = gridIndex(x, y);
                component(m_contactTarget[i], n) += -in.sign * depth * wN * wT;
                if (depth > 0.0) {
                    const double sigma = std::clamp((rt - centre) / (0.5 * in.tangentExtent), -1.0, 1.0);
                    component(m_contactTarget[i], t) += kContactSpread * depth * wN * sigma;
                }
                if (kick > 0.0) {
                    component(m_velocities[i], n) += -in.sign * kick * wN * wT;
                    kicked = true;
                }
            }
        }
    }
    m_hasContactTarget = !isZero(m_contactTarget);
    if (kicked)
        m_quietSteps = 0;
    constrain(size);
}
```

**Step 3:** Wire the field into the integrator and lifecycle:

- `integrateLiquidSubstep`, lines 451-452: replace `lagField.x` / `lagField.y` with `(lagField.x + m_contactTarget[i].x)` / `(lagField.y + m_contactTarget[i].y)`.
- `reset()`: add `m_contactTarget.fill(Vec2{}); m_hasContactTarget = false;`.
- `settled()`: Liquid branch returns `isZero(m_windowVelocity) && isZero(m_pendingDelta) && !m_hasContactTarget;`.
- `advance()` quiet block (line 553): `const bool quiet = !m_hasContactTarget && isQuiet(m_displacements, m_velocities);`.

**Step 4:** `cmake --build build && ctest --test-dir build --output-on-failure` → all pass.
If `testContactFlattensAndSpreadsFace` misses the 0.8·depth floor because pressure coupling (`waveSpeed2 * grad`) resists the dent, tune in this order. Change one lever at a time and re-run the whole suite:
1. Make the normal falloff gentler: `kContactFalloff` 0.45 → 0.6.
2. Blend contact nodes toward head stiffness: `stiffness = mix(stiffness, m_headStiffness, wN·wT)`. Store the per-node weight in a new `std::array<double,81> m_contactGrip`. Re-check that `testContactImpulseFollowsMomentum` still rebounds at momentum 0.5.

Do not loosen test tolerances.

**Step 5:** Existing tests must still pass unchanged. That proves movement, droplet and Wiggle behaviour are untouched when no contacts are set.

---

## Phase B — Contact geometry and pair state machine (pure, unit-tested)

### Task 5: Register the contact module and test target

**Files:** Modify `CMakeLists.txt:13-36`

**Step 1:** Add `src/LiquidContact.cpp` to `add_library(liquid-glass SHARED …)` after `src/LiquidMesh.cpp`.

**Step 2:** Inside `if(BUILD_TESTING)`:

```cmake
    add_executable(liquid-contact-tests tests/LiquidContactTests.cpp src/LiquidContact.cpp src/LiquidMesh.cpp)
    target_include_directories(liquid-contact-tests PRIVATE src)
    target_compile_options(liquid-contact-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME liquid-contact-tests COMMAND liquid-contact-tests)
```

### Task 6: `LiquidContact.hpp` contract

**Files:** Create `src/LiquidContact.hpp`

```cpp
#pragma once

#include "LiquidMesh.hpp"

namespace liquid_glass {

struct Rect {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};

// Gap below which a moving pair starts reaching (logical px), and the multiple of it at which reaching releases.
inline constexpr double      kContactReach        = 40.0;
inline constexpr double      kContactReleaseRatio = 1.5;
// Meniscus bulge cap as a fraction of kContactReach (10 px).
inline constexpr double      kContactMeniscusPeak = 0.25;
// Pass-through: half-penetration beyond this multiple of the smaller window's limit latches the pair.
inline constexpr double      kContactBreakRatio   = 1.25;
// Relative motion below this speed (px/s) counts as still.
inline constexpr double      kContactQuietSpeed   = 8.0;
inline constexpr double      kContactEngageSeconds  = 0.03;
inline constexpr double      kContactReleaseSeconds = 0.35;
inline constexpr double      kContactLatchEngagement = 0.02;
// Keep the previous separating axis unless the other overlap drops below this fraction of it.
inline constexpr double      kContactAxisHysteresis = 0.75;
inline constexpr double      kContactImpactGain   = 0.6;
inline constexpr std::size_t kMaxContactPairs     = 64;

// Facing relation of window bodies a and b along one axis. Invalid when diagonal, too far apart or degenerate.
struct ContactGeometry {
    bool     valid       = false;
    unsigned axis        = 0;   // 0 = x, 1 = y
    double   sign        = 1.0; // +1: b lies toward +axis from a
    double   penetration = 0.0; // > 0 overlap along axis; <= 0 gap (negated)
    double   spanMin     = 0.0; // world tangential overlap of the two bodies
    double   spanMax     = 0.0;
};

// maxGap: farthest gap still reported (pass kContactReach * kContactReleaseRatio).
// preferredAxis: 0/1 from the pair's previous contact, or -1.
ContactGeometry resolveContact(const Rect& a, const Rect& b, double maxGap, int preferredAxis) noexcept;

// Pair-tracking net: true when both bodies are usable and their gap on each axis is <= maxGap (diagonal neighbours included).
bool withinReach(const Rect& a, const Rect& b, double maxGap) noexcept;

enum class ContactPhase { Apart, Reaching, Pressing, Latched };

struct ContactResponse {
    double depth        = 0.0; // per window: > 0 flatten, < 0 reach
    double closingSpeed = 0.0; // > 0 only on the tick pressing begins (px/s)
};

class ContactPair {
  public:
    ContactPhase phase() const noexcept { return m_phase; }
    int          axis() const noexcept { return m_axis; }
    // geometry.valid == false means "tracked but not facing" (diagonal): the pair is apart and remembers it.
    // relativeVelocity: d(centre_b - centre_a)/dt in px/s. limit: smaller displacementLimit of the two.
    // reach: false for tiled–tiled pairs, which press but never form a meniscus.
    ContactResponse step(const ContactGeometry& geometry, Vec2 relativeVelocity, double dt, double limit, bool reach) noexcept;
    void            reset() noexcept;

  private:
    ContactPhase m_phase         = ContactPhase::Apart;
    int          m_axis          = -1;
    double       m_engagement    = 0.0;
    bool         m_seenSeparated = false; // observed with a gap or diagonal; an overlap after this is a real collision
};

// Capture-local mesh input for one window. `towardOther` is +1/-1 along geometry.axis from this window.
// Reaching (depth < 0) narrows the span to its middle half so the bulge forms a neck, not a wall.
ContactInput makeContactInput(const Rect& body, const Rect& capture, const ContactGeometry& geometry, double towardOther,
                              double depth, double impulse) noexcept;

// Inward impact speed for `self`: kContactImpactGain * closing * strength * areaOther / (areaSelf + areaOther); 0 if degenerate.
double contactImpulse(double closingSpeed, double strength, double areaSelf, double areaOther) noexcept;

} // namespace liquid_glass
```

### Task 7: Failing contact tests

**Files:** Create `tests/LiquidContactTests.cpp`, using the same `report`/`CHECK` harness as `tests/LiquidMeshTests.cpp:20-29`.

```cpp
// Compositor-independent tests for liquid_glass contact geometry and pair dynamics.
#include "../src/LiquidContact.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace liquid_glass;
int g_failures = 0;
void report(bool ok, const char* expression, const char* file, int line) {
    if (ok) return;
    ++g_failures;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}
#define CHECK(expr) report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

constexpr double kMaxGap = kContactReach * kContactReleaseRatio; // 60
constexpr double kDt = 1.0 / 60.0;
constexpr double kLimit = 80.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

void testOverlapPicksSmallerAxisAndSpan() {
    // b overlaps a's right side by 30 px in x and fully in y.
    const auto g = resolveContact({0, 0, 400, 300}, {370, 50, 400, 300}, kMaxGap, -1);
    CHECK(g.valid && g.axis == 0 && g.sign == 1.0);
    CHECK(std::abs(g.penetration - 30.0) < 1e-9);
    CHECK(std::abs(g.spanMin - 50.0) < 1e-9 && std::abs(g.spanMax - 300.0) < 1e-9);
    const auto flipped = resolveContact({370, 50, 400, 300}, {0, 0, 400, 300}, kMaxGap, -1);
    CHECK(flipped.valid && flipped.sign == -1.0);
}

void testAxisHysteresis() {
    // Overlap x = 100, y = 90: without preference y wins; preferring x keeps x (90 > 0.75·100).
    const Rect a{0, 0, 400, 400};
    const Rect b{300, 310, 400, 400};
    CHECK(resolveContact(a, b, kMaxGap, -1).axis == 1);
    CHECK(resolveContact(a, b, kMaxGap, 0).axis == 0);
    // y = 70 < 0.75·100 forces the switch.
    CHECK(resolveContact(a, {300, 330, 400, 400}, kMaxGap, 0).axis == 1);
}

void testGapDiagonalAndInvalid() {
    const auto gap = resolveContact({0, 0, 400, 300}, {430, 0, 400, 300}, kMaxGap, -1);
    CHECK(gap.valid && gap.axis == 0 && std::abs(gap.penetration + 30.0) < 1e-9);
    CHECK(!resolveContact({0, 0, 400, 300}, {470, 0, 400, 300}, kMaxGap, -1).valid);   // beyond maxGap
    CHECK(!resolveContact({0, 0, 400, 300}, {410, 310, 400, 300}, kMaxGap, -1).valid); // diagonal
    CHECK(!resolveContact({0, 0, 0, 300}, {10, 0, 400, 300}, kMaxGap, -1).valid);
    CHECK(!resolveContact({kNaN, 0, 400, 300}, {10, 0, 400, 300}, kMaxGap, -1).valid);
}

void testWithinReachIncludesDiagonalNeighbours() {
    const Rect a{0, 0, 400, 300};
    CHECK(withinReach(a, {408, 308, 400, 300}, kMaxGap));  // diagonal tile across gaps_in = 8
    CHECK(withinReach(a, {200, 100, 400, 300}, kMaxGap));  // overlapping
    CHECK(!withinReach(a, {461, 0, 400, 300}, kMaxGap));   // 61 px gap on x
    CHECK(!withinReach(a, {0, 361, 400, 300}, kMaxGap));
    CHECK(!withinReach(a, {0, 0, 0, 300}, kMaxGap));
    CHECK(!withinReach(a, {kNaN, 0, 400, 300}, kMaxGap));
}

ContactGeometry along(double penetration) {
    return ContactGeometry{.valid = true, .axis = 0, .sign = 1.0, .penetration = penetration, .spanMin = 0, .spanMax = 300};
}

// b lies on a's +x side, so b moving toward -x closes the pair.
constexpr Vec2 kApproach{-600.0, 0.0};
constexpr Vec2 kRecede{600.0, 0.0};
constexpr Vec2 kStill{};

void testStaticAdjacencyNeverEngages() {
    ContactPair pair;
    for (int f = 0; f < 120; ++f) {
        const auto r = pair.step(along(50.0), kStill, kDt, kLimit, true); // overlapping, but nothing moves
        CHECK(r.depth == 0.0 && r.closingSpeed == 0.0);
    }
    CHECK(pair.phase() == ContactPhase::Apart);
}

// Windows already stacked at rest: starting to drag one must not suddenly dent the other.
void testPreexistingOverlapStaysInert() {
    ContactPair pair;
    for (int f = 0; f < 30; ++f) {
        const auto r = pair.step(along(120.0 - f), kRecede, kDt, kLimit, true);
        CHECK(r.depth == 0.0 && r.closingSpeed == 0.0);
    }
    CHECK(pair.phase() == ContactPhase::Latched);
    pair.step(along(-20.0), kRecede, kDt, kLimit, true); // dragged clear, still within reach
    CHECK(pair.phase() == ContactPhase::Reaching);
}

void testReachSnapInAndPinchOff() {
    ContactPair pair;
    CHECK(pair.step(along(-50.0), kApproach, kDt, kLimit, true).depth == 0.0); // outside reach on approach
    const auto in = pair.step(along(-39.0), kApproach, kDt, kLimit, true);
    CHECK(pair.phase() == ContactPhase::Reaching);
    CHECK(std::abs(in.depth + 10.0) < 1e-6);                                    // min(19.5, 10), fully engaged
    CHECK(std::abs(pair.step(along(-8.0), kApproach, kDt, kLimit, true).depth + 4.0) < 1e-6); // 0.5·gap cap
    CHECK(pair.step(along(-55.0), kRecede, kDt, kLimit, true).depth < 0.0);     // still held while separating
    const auto out = pair.step(along(-61.0), kRecede, kDt, kLimit, true);
    CHECK(pair.phase() == ContactPhase::Apart && out.depth == 0.0);             // pinch-off
}

void testPressOnsetImpulseIsOneShot() {
    ContactPair pair;
    pair.step(along(-5.0), kApproach, kDt, kLimit, true);
    const auto hit = pair.step(along(10.0), Vec2{-900.0, 250.0}, kDt, kLimit, true);
    CHECK(pair.phase() == ContactPhase::Pressing);
    CHECK(std::abs(hit.closingSpeed - 900.0) < 1e-9); // closing component along the contact axis only
    CHECK(std::abs(hit.depth - 5.0) < 1e-6);
    CHECK(pair.step(along(20.0), kApproach, kDt, kLimit, true).closingSpeed == 0.0);
}

// Tiled–tiled: no meniscus at gaps_in during reflow, but a real overlap during animation presses.
void testTiledPairPressesWithoutMeniscus() {
    ContactPair pair;
    CHECK(pair.step(along(-8.0), kApproach, kDt, kLimit, false).depth == 0.0);
    CHECK(pair.phase() == ContactPhase::Apart);
    const auto hit = pair.step(along(30.0), kApproach, kDt, kLimit, false);
    CHECK(pair.phase() == ContactPhase::Pressing && hit.closingSpeed > 0.0 && hit.depth > 0.0);
    pair.step(along(-8.0), kRecede, kDt, kLimit, false);
    CHECK(pair.phase() == ContactPhase::Apart);
}

// A tile swap that crosses diagonally (top-right ↔ bottom-left) is first tracked corner-to-corner, then overlaps.
void testDiagonalCrossingCollides() {
    ContactPair pair;
    CHECK(pair.step(ContactGeometry{}, Vec2{-600.0, 600.0}, kDt, kLimit, false).depth == 0.0);
    const auto hit = pair.step(along(30.0), Vec2{-600.0, 600.0}, kDt, kLimit, false);
    CHECK(pair.phase() == ContactPhase::Pressing);
    CHECK(std::abs(hit.closingSpeed - 600.0) < 1e-9);
}

void testPassThroughLatchesUntilSeparated() {
    ContactPair pair;
    pair.step(along(-5.0), kApproach, kDt, kLimit, true);
    pair.step(along(150.0), kApproach, kDt, kLimit, true);
    const auto r = pair.step(along(2.0 * kContactBreakRatio * kLimit + 1.0), kApproach, kDt, kLimit, true);
    CHECK(pair.phase() == ContactPhase::Latched && r.depth == 0.0);
    CHECK(pair.step(along(40.0), kRecede, kDt, kLimit, true).depth == 0.0); // overlap shrinking again: stays latched, no pop
    pair.step(along(-61.0), kRecede, kDt, kLimit, true);
    CHECK(pair.phase() == ContactPhase::Apart);
}

void testStillnessReleasesThenReengages() {
    ContactPair pair;
    pair.step(along(-5.0), kApproach, kDt, kLimit, true);
    pair.step(along(40.0), kApproach, kDt, kLimit, true);
    double previous = 20.0;
    for (int f = 0; f < 180 && pair.phase() == ContactPhase::Pressing; ++f) {
        const double depth = pair.step(along(40.0), kStill, kDt, kLimit, true).depth;
        CHECK(depth <= previous + 1e-12);
        previous = depth;
    }
    CHECK(pair.phase() == ContactPhase::Latched);
    CHECK(pair.step(along(40.0), kApproach, kDt, kLimit, true).depth == 0.0); // moving while still overlapped: no pop
    pair.step(along(-20.0), kRecede, kDt, kLimit, true);                       // separated but within reach
    CHECK(pair.phase() == ContactPhase::Reaching);
}

void testReleaseCadenceAgreement() {
    const auto depthAfter = [](double hz) {
        ContactPair pair;
        pair.step(along(-5.0), kApproach, 1.0 / hz, kLimit, true);
        pair.step(along(40.0), kApproach, 1.0 / hz, kLimit, true);
        double depth = 0.0;
        for (int f = 0; f < static_cast<int>(std::lround(0.2 * hz)); ++f)
            depth = pair.step(along(40.0), kStill, 1.0 / hz, kLimit, true).depth;
        return depth;
    };
    const double d60 = depthAfter(60.0);
    CHECK(d60 > 0.0 && std::abs(depthAfter(144.0) - d60) <= 0.05 * d60);
}

void testMakeContactInputConvertsToCaptureLocal() {
    const Rect body{100, 100, 400, 300};
    const Rect capture{80, 80, 440, 340};
    const ContactGeometry g{.valid = true, .axis = 0, .sign = 1.0, .penetration = 10, .spanMin = 150, .spanMax = 350};
    const auto right = makeContactInput(body, capture, g, 1.0, 5.0, 0.0);
    CHECK(right.axis == 0 && right.sign == 1.0 && right.face == 420.0 && right.extent == 400.0);
    CHECK(right.spanMin == 70.0 && right.spanMax == 270.0 && right.tangentExtent == 300.0);
    const auto left = makeContactInput(body, capture, g, -1.0, 5.0, 0.0);
    CHECK(left.sign == -1.0 && left.face == 20.0);
    const auto neck = makeContactInput(body, capture, g, 1.0, -5.0, 0.0);
    CHECK(neck.spanMin == 120.0 && neck.spanMax == 220.0); // middle half of [70, 270]
}

void testContactImpulseShare() {
    CHECK(std::abs(contactImpulse(1000.0, 0.65, 1.0, 1.0) - kContactImpactGain * 1000.0 * 0.65 * 0.5) < 1e-9);
    CHECK(contactImpulse(1000.0, 0.65, 1.0, 3.0) > contactImpulse(1000.0, 0.65, 3.0, 1.0));
    CHECK(contactImpulse(1000.0, 0.65, 0.0, 0.0) == 0.0);
    CHECK(contactImpulse(kNaN, 0.65, 1.0, 1.0) == 0.0);
}
} // namespace

int main() {
    testOverlapPicksSmallerAxisAndSpan();
    testAxisHysteresis();
    testGapDiagonalAndInvalid();
    testWithinReachIncludesDiagonalNeighbours();
    testStaticAdjacencyNeverEngages();
    testPreexistingOverlapStaysInert();
    testReachSnapInAndPinchOff();
    testPressOnsetImpulseIsOneShot();
    testTiledPairPressesWithoutMeniscus();
    testDiagonalCrossingCollides();
    testPassThroughLatchesUntilSeparated();
    testStillnessReleasesThenReengages();
    testReleaseCadenceAgreement();
    testMakeContactInputConvertsToCaptureLocal();
    testContactImpulseShare();
    if (g_failures == 0) {
        std::printf("liquid-contact-tests: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "liquid-contact-tests: %d check(s) failed\n", g_failures);
    return 1;
}
```

Create a stub `src/LiquidContact.cpp` in which every function returns a default value. Then run `cmake --build build && ctest --test-dir build -R liquid-contact --output-on-failure` → FAIL.

### Task 8: Implement `resolveContact`

**Files:** `src/LiquidContact.cpp`

```cpp
#include "LiquidContact.hpp"

#include <algorithm>
#include <cmath>

namespace liquid_glass {
namespace {
bool usable(const Rect& r) noexcept {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h) && r.w > 0.0 && r.h > 0.0;
}
double overlap(double a0, double a1, double b0, double b1) noexcept {
    return std::min(a1, b1) - std::max(a0, b0);
}
} // namespace

ContactGeometry resolveContact(const Rect& a, const Rect& b, double maxGap, int preferredAxis) noexcept {
    if (!usable(a) || !usable(b) || !(maxGap >= 0.0))
        return {};
    const double ox = overlap(a.x, a.x + a.w, b.x, b.x + b.w);
    const double oy = overlap(a.y, a.y + a.h, b.y, b.y + b.h);
    unsigned axis;
    if (ox > 0.0 && oy > 0.0) {
        axis = ox <= oy ? 0u : 1u;
        if (preferredAxis == 0 && !(oy < kContactAxisHysteresis * ox))
            axis = 0u;
        else if (preferredAxis == 1 && !(ox < kContactAxisHysteresis * oy))
            axis = 1u;
    } else if (oy > 0.0 && ox <= 0.0 && -ox <= maxGap) {
        axis = 0u;
    } else if (ox > 0.0 && oy <= 0.0 && -oy <= maxGap) {
        axis = 1u;
    } else {
        return {};
    }
    const double centreDelta = axis == 0 ? (b.x + 0.5 * b.w) - (a.x + 0.5 * a.w) : (b.y + 0.5 * b.h) - (a.y + 0.5 * a.h);
    ContactGeometry g;
    g.valid       = true;
    g.axis        = axis;
    g.sign        = centreDelta >= 0.0 ? 1.0 : -1.0;
    g.penetration = axis == 0 ? ox : oy;
    g.spanMin     = axis == 0 ? std::max(a.y, b.y) : std::max(a.x, b.x);
    g.spanMax     = axis == 0 ? std::min(a.y + a.h, b.y + b.h) : std::min(a.x + a.w, b.x + b.w);
    return g;
}

bool withinReach(const Rect& a, const Rect& b, double maxGap) noexcept {
    if (!usable(a) || !usable(b) || !(maxGap >= 0.0))
        return false;
    return -overlap(a.x, a.x + a.w, b.x, b.x + b.w) <= maxGap && -overlap(a.y, a.y + a.h, b.y, b.y + b.h) <= maxGap;
}
```

Run the tests: the four geometry tests pass.

### Task 9: Implement `ContactPair`

```cpp
void ContactPair::reset() noexcept {
    *this = ContactPair{};
}

ContactResponse ContactPair::step(const ContactGeometry& geometry, Vec2 relativeVelocity, double dt, double limit, bool reach) noexcept {
    if (!(dt > 0.0) || !std::isfinite(dt) || !std::isfinite(relativeVelocity.x) || !std::isfinite(relativeVelocity.y) ||
        !(limit > 0.0)) {
        reset();
        return {};
    }
    const bool   moving = std::hypot(relativeVelocity.x, relativeVelocity.y) > kContactQuietSpeed;
    const double tau    = moving ? kContactEngageSeconds : kContactReleaseSeconds;
    m_engagement += ((moving ? 1.0 : 0.0) - m_engagement) * -std::expm1(-dt / tau);

    if (!geometry.valid || !std::isfinite(geometry.penetration)) {
        // Tracked but not facing (corner to corner): any dent releases, and the pair now counts as having been apart.
        m_phase         = ContactPhase::Apart;
        m_seenSeparated = true;
        return {};
    }
    const double pen     = geometry.penetration;
    const double gap     = -pen;
    const double release = kContactReach * kContactReleaseRatio;
    // b lies toward geometry.sign from a; the pair closes when b's relative velocity points back at a along the axis.
    const double closing = std::max(0.0, -geometry.sign * (geometry.axis == 0 ? relativeVelocity.x : relativeVelocity.y));
    m_axis = static_cast<int>(geometry.axis);

    ContactResponse r;
    switch (m_phase) {
        case ContactPhase::Apart:
            if (pen <= 0.0)
                m_seenSeparated = true;
            if (!moving)
                return r; // static adjacency or stacked windows at rest never engage
            if (pen > 0.0) {
                if (!m_seenSeparated) {
                    m_phase = ContactPhase::Latched; // stacked before anything moved: inert until separated
                    return r;
                }
                m_phase        = ContactPhase::Pressing; // includes diagonal entry and tiled crossings
                r.closingSpeed = closing;
            } else if (reach && gap < kContactReach) {
                m_phase = ContactPhase::Reaching;
            } else {
                return r;
            }
            m_engagement = 1.0;
            break;
        case ContactPhase::Reaching:
            if (pen > 0.0) {
                m_phase        = ContactPhase::Pressing;
                r.closingSpeed = closing;
            } else if (!reach || gap > release) {
                m_phase = ContactPhase::Apart; // pinch-off: the bulge target drops to zero and the lattice snaps back
                return r;
            }
            break;
        case ContactPhase::Pressing:
            if (pen <= 0.0) {
                m_seenSeparated = true;
                m_phase         = (reach && gap <= release) ? ContactPhase::Reaching : ContactPhase::Apart;
            }
            break;
        case ContactPhase::Latched:
            if (pen <= 0.0) {
                m_seenSeparated = true;
                if (gap > release || moving)
                    m_phase = (reach && moving && gap < kContactReach) ? ContactPhase::Reaching : ContactPhase::Apart;
            }
            if (m_phase != ContactPhase::Reaching)
                return r;
            m_engagement = 1.0;
            break;
    }

    if (m_phase == ContactPhase::Pressing && 0.5 * pen > kContactBreakRatio * limit) {
        m_phase = ContactPhase::Latched; // pushed through; dents spring back
        return {};
    }
    if (m_phase != ContactPhase::Apart && m_engagement < kContactLatchEngagement) {
        m_phase = ContactPhase::Latched; // held still: release instead of a permanent dent
        return {};
    }
    if (m_phase == ContactPhase::Pressing)
        r.depth = 0.5 * pen * m_engagement;
    else if (m_phase == ContactPhase::Reaching)
        r.depth = -std::min(0.5 * gap, kContactMeniscusPeak * kContactReach) * m_engagement;
    return r;
}
```

Run the tests: the pair tests pass. If `testReachSnapInAndPinchOff` fails on the `-10` exactness, check that the first engaging step sets `m_engagement = 1.0` *after* the decay update, as written.

### Task 10: Implement `makeContactInput` and `contactImpulse`

```cpp
ContactInput makeContactInput(const Rect& body, const Rect& capture, const ContactGeometry& geometry, double towardOther,
                              double depth, double impulse) noexcept {
    const bool   x       = geometry.axis == 0;
    const double bodyMin = x ? body.x : body.y;
    const double bodyLen = x ? body.w : body.h;
    const double capMin  = x ? capture.x : capture.y;
    const double capTan  = x ? capture.y : capture.x;
    double spanMin = geometry.spanMin - capTan;
    double spanMax = geometry.spanMax - capTan;
    if (depth < 0.0) {
        const double centre = 0.5 * (spanMin + spanMax);
        const double half   = 0.25 * (spanMax - spanMin);
        spanMin = centre - half;
        spanMax = centre + half;
    }
    return ContactInput{
        .axis          = geometry.axis,
        .sign          = towardOther,
        .face          = (towardOther > 0.0 ? bodyMin + bodyLen : bodyMin) - capMin,
        .extent        = bodyLen,
        .spanMin       = spanMin,
        .spanMax       = spanMax,
        .tangentExtent = x ? body.h : body.w,
        .depth         = depth,
        .impulse       = impulse,
    };
}

double contactImpulse(double closingSpeed, double strength, double areaSelf, double areaOther) noexcept {
    const double total = areaSelf + areaOther;
    if (!std::isfinite(closingSpeed) || !std::isfinite(strength) || !(total > 0.0) || closingSpeed <= 0.0 || strength <= 0.0)
        return 0.0;
    return kContactImpactGain * closingSpeed * strength * (areaOther / total);
}

} // namespace liquid_glass
```

Run: `cmake --build build && ctest --test-dir build --output-on-failure` → 3/3 pass.

---

## Phase C — Runtime integration

No unit tests here: this is compositor wiring, verified in the nested session (Phase D).

### Task 11: Register `plugin:liquid-glass:collisions`

**Files:** `src/PluginRuntime.hpp:82-91`, `src/PluginRuntime.cpp:40-64`

**Step 1:** Header: add `SP<Config::Values::CBoolValue> m_collisions;` after `m_transitions`, and `bool collisionsEnabled() const;` after `transitionsEnabled()`.

**Step 2:** Constructor, after `m_transitions`:

```cpp
    m_collisions = makeShared<Config::Values::CBoolValue>("plugin:liquid-glass:collisions", "Liquid windows deform on contact with each other", false);
```

Append `|| !HyprlandAPI::addConfigValueV2(m_handle, m_collisions)` to the registration chain.

**Step 3:**

```cpp
bool CPluginRuntime::collisionsEnabled() const {
    return pluginEnabled() && !m_inert && m_collisions->value() && effectiveMode() == liquid_glass::MotionMode::Liquid
        && effectiveStrength() > 0.0 && materialUsable();
}
```

### Task 12: Tracking state for contacts and pairs

**Files:** `src/PluginRuntime.hpp`

**Step 1:** Add `#include "LiquidContact.hpp"` and `#include <array>`. Extend `STracked`:

```cpp
        std::array<liquid_glass::ContactInput, liquid_glass::kMaxContacts> contacts{};
        std::size_t       contactCount = 0;
        bool              hadContacts  = false; // last tick pushed a non-empty contact set into the mesh
```

**Step 2:** Add a pair record and members:

```cpp
    struct SContactPair {
        PHLWINDOWREF              a;
        PHLWINDOWREF              b;
        liquid_glass::ContactPair state;
        Vector2D                  lastOffset; // centre_b - centre_a at the previous tick
        bool                      hasOffset = false;
    };
    std::vector<SContactPair> m_pairs;
    clock::time_point         m_lastCollisionTick = clock::now();
```

Declare these private helpers:

```cpp
    bool               collisionCandidate(const PHLWINDOW& window) const;
    bool               sharesSpace(const PHLWINDOW& a, const PHLWINDOW& b) const;
    liquid_glass::Rect bodyRect(const PHLWINDOW& window) const;
    liquid_glass::Rect captureRect(const PHLWINDOW& window) const;
    void               resolveCollisions(clock::time_point now);
    void               addContact(STracked& entry, const liquid_glass::ContactInput& input);
    void               clearCollisions();
```

**Step 3:** Constructor: `m_pairs.reserve(liquid_glass::kMaxContactPairs);` before the listeners.

### Task 13: Candidate, space and rect helpers

**Files:** `src/PluginRuntime.cpp`. Add includes as needed: `HLWorkspace.hpp`, `WindowPresentation.hpp`. Copy the include paths from `LiquidGlassTransformer.cpp:3-6`.

```cpp
bool CPluginRuntime::collisionCandidate(const PHLWINDOW& window) const {
    if (!eligible(window) || window->isHidden() || window->presentation().animatingIn())
        return false;
    const auto& workspace = window->m_workspace;
    if (!workspace || !workspace->visible() || workspace->m_renderOffset->isBeingAnimated())
        return false;
    const auto* transformer = transformerOf(window);
    return !(transformer && transformer->droplet().active());
}

bool CPluginRuntime::sharesSpace(const PHLWINDOW& a, const PHLWINDOW& b) const {
    // Tiled pairs are included: they only ever overlap while layout animations carry them across each other.
    if (a->m_workspace == b->m_workspace)
        return true;
    const bool pinned = (a->m_state & Desktop::View::WINDOW_STATE_PINNED) || (b->m_state & Desktop::View::WINDOW_STATE_PINNED);
    return pinned && a->m_monitor == b->m_monitor;
}

liquid_glass::Rect CPluginRuntime::bodyRect(const PHLWINDOW& window) const {
    CBox box = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    box.expand(window->presentation().borderSize());
    box.translate(window->presentation().floatingOffset());
    return {box.x, box.y, box.w, box.h};
}

liquid_glass::Rect CPluginRuntime::captureRect(const PHLWINDOW& window) const {
    CBox box = window->getFullWindowBoundingBox();
    box.translate(window->presentation().floatingOffset());
    return {box.x, box.y, box.w, box.h};
}
```

Check the `PINNED` enum's qualified name against `Window.hpp:85-88` (`worldFootprint` already uses `Desktop::View::WINDOW_STATE_PINNED`).

### Task 14: `resolveCollisions` and contact bookkeeping

```cpp
void CPluginRuntime::addContact(STracked& entry, const liquid_glass::ContactInput& input) {
    if (entry.contactCount < entry.contacts.size()) {
        entry.contacts[entry.contactCount++] = input;
        return;
    }
    // Full: replace the shallowest contact if this one is deeper.
    auto* weakest = std::min_element(entry.contacts.begin(), entry.contacts.end(),
                                     [](const auto& l, const auto& r) { return std::abs(l.depth) < std::abs(r.depth); });
    if (std::abs(input.depth) > std::abs(weakest->depth))
        *weakest = input;
}

void CPluginRuntime::clearCollisions() {
    m_pairs.clear();
    for (auto& entry : m_tracked) {
        entry.contactCount = 0;
        if (!entry.hadContacts)
            continue;
        entry.hadContacts = false;
        if (const auto window = entry.window.lock()) {
            if (auto* transformer = transformerOf(window)) {
                const auto size = window->getFullWindowBoundingBox().size();
                transformer->mesh().setContacts({}, {size.x, size.y});
            }
        }
    }
}

void CPluginRuntime::resolveCollisions(clock::time_point now) {
    const double dt = std::chrono::duration<double>(now - m_lastCollisionTick).count();
    m_lastCollisionTick = now;
    if (!collisionsEnabled() || !(dt > 0.0) || dt > MAX_GAP_SECONDS) {
        clearCollisions();
        return;
    }
    for (auto& entry : m_tracked)
        entry.contactCount = 0;

    const double maxGap = liquid_glass::kContactReach * liquid_glass::kContactReleaseRatio;

    // 1. Track every candidate pair within reach, moving or not. A pair must be observed apart before it overlaps,
    //    otherwise a fast diagonal tile swap would read as windows stacked before the drag.
    for (std::size_t i = 0; i < m_tracked.size() && m_pairs.size() < liquid_glass::kMaxContactPairs; ++i) {
        const auto wa = m_tracked[i].window.lock();
        if (!wa || !collisionCandidate(wa))
            continue;
        for (std::size_t j = i + 1; j < m_tracked.size() && m_pairs.size() < liquid_glass::kMaxContactPairs; ++j) {
            const auto wb = m_tracked[j].window.lock();
            if (!wb || !collisionCandidate(wb) || !sharesSpace(wa, wb) || !liquid_glass::withinReach(bodyRect(wa), bodyRect(wb), maxGap))
                continue;
            const bool known = std::ranges::any_of(m_pairs, [&](const SContactPair& p) {
                return (p.a.get() == wa.get() && p.b.get() == wb.get()) || (p.a.get() == wb.get() && p.b.get() == wa.get());
            });
            if (!known)
                m_pairs.push_back({PHLWINDOWREF{wa}, PHLWINDOWREF{wb}, {}, {}, false});
        }
    }

    // 2. Step every pair; drop pairs that became ineligible or left the reach net.
    std::erase_if(m_pairs, [&](SContactPair& pair) {
        const auto wa = pair.a.lock();
        const auto wb = pair.b.lock();
        if (!wa || !wb || !collisionCandidate(wa) || !collisionCandidate(wb) || !sharesSpace(wa, wb))
            return true;
        STracked* ea = find(wa);
        STracked* eb = find(wb);
        if (!ea || !eb)
            return true;
        const auto ra = bodyRect(wa);
        const auto rb = bodyRect(wb);
        if (!liquid_glass::withinReach(ra, rb, maxGap))
            return true;
        // Invalid geometry here means corner-to-corner; the pair records it as "apart" and keeps tracking.
        const auto geometry = liquid_glass::resolveContact(ra, rb, maxGap, pair.state.axis());
        const Vector2D offset{(rb.x + 0.5 * rb.w) - (ra.x + 0.5 * ra.w), (rb.y + 0.5 * rb.h) - (ra.y + 0.5 * ra.h)};
        const liquid_glass::Vec2 relativeVelocity = pair.hasOffset
            ? liquid_glass::Vec2{(offset.x - pair.lastOffset.x) / dt, (offset.y - pair.lastOffset.y) / dt}
            : liquid_glass::Vec2{};
        pair.lastOffset = offset;
        pair.hasOffset  = true;
        const auto ca = captureRect(wa);
        const auto cb = captureRect(wb);
        const double limit = std::min(liquid_glass::displacementLimit({ca.w, ca.h}), liquid_glass::displacementLimit({cb.w, cb.h}));
        // Tiled neighbours always sit within reach at gaps_in, so only pairs with a floating window form a meniscus.
        const bool reach = wa->isFloating() || wb->isFloating();
        const auto response = pair.state.step(geometry, relativeVelocity, dt, limit, reach);
        if (response.depth != 0.0 || response.closingSpeed > 0.0) {
            const double areaA = ra.w * ra.h;
            const double areaB = rb.w * rb.h;
            const double strength = effectiveStrength();
            addContact(*ea, liquid_glass::makeContactInput(ra, ca, geometry, geometry.sign, response.depth,
                                                           liquid_glass::contactImpulse(response.closingSpeed, strength, areaA, areaB)));
            addContact(*eb, liquid_glass::makeContactInput(rb, cb, geometry, -geometry.sign, response.depth,
                                                           liquid_glass::contactImpulse(response.closingSpeed, strength, areaB, areaA)));
        }
        return false;
    });

    // 3. Push contact sets into meshes. A resting struck window gets a transformer on demand.
    for (auto& entry : m_tracked) {
        if (entry.contactCount == 0 && !entry.hadContacts)
            continue;
        const auto window = entry.window.lock();
        if (!window)
            continue;
        auto* transformer = transformerOf(window);
        if (!transformer && entry.contactCount > 0)
            transformer = acquireTransformer(window);
        if (!transformer)
            continue;
        const bool wasActive = transformer->active();
        const auto capture = window->getFullWindowBoundingBox().size();
        const liquid_glass::Vec2 size{capture.x, capture.y};
        transformer->mesh().setContacts(std::span<const liquid_glass::ContactInput>(entry.contacts.data(), entry.contactCount), size);
        transformer->refreshPose(size);
        entry.hadContacts = entry.contactCount > 0;
        if (!wasActive && transformer->active())
            entry.lastAdvance = now; // first advance of a freshly excited field integrates from now
    }
}
```

Notes for the implementer:
- `pair.state.axis()` is `-1` until the first contact, matching `resolveContact`'s "no preference".
- Cleanup when a field settles is left to `advanceWindow`'s existing `removeInactive` path. Do not prune here.

### Task 15: Wire into tick, sampling and lifecycle

**Files:** `src/PluginRuntime.cpp`

**Step 1:** Replace the per-window loop in `onTick` (`:252-259`) with three passes:

```cpp
    for (auto& entry : m_tracked)
        if (const auto window = entry.window.lock())
            sampleWindow(entry, window);
    resolveCollisions(now);
    for (auto& entry : m_tracked)
        if (const auto window = entry.window.lock())
            advanceWindow(entry, window, now);
```

Keep the existing `erase_if` and close-tick code after it.

**Step 2:** No `sampleWindow` change: pairs are tracked by proximity every tick, independent of who moved.

**Step 3:** Lifecycle:
- `m_listenClose`: before `forget(window)`, add `std::erase_if(m_pairs, [&](const SContactPair& p) { return p.a.get() == window.get() || p.b.get() == window.get(); });`. The partner's contact vanishes on the next tick, so its dent snaps back.
- `m_listenDestroy`: also `std::erase_if(m_pairs, [](const SContactPair& p) { return p.a.expired() || p.b.expired(); });`.
- `onReload()`: call `clearCollisions();` right after `detachAll();`. Fields were already reset by detach; this also clears `hadContacts`.
- `detachWindow`: set `entry.hadContacts = false; entry.contactCount = 0;`. The mesh was reset with the transformer.

**Step 4:** `cmake --build build` must compile with no new warnings (`-Wall -Wextra -Wpedantic`). Then `ctest` → 3/3 pass.

---

## Phase D — Documentation and nested verification

### Task 16: README and fixture

**Files:** `README.md`, `tests/nested/hyprland.lua:42-47`

1. Intro paragraph (line 3): add one sentence. "With `collisions` enabled, Liquid windows reach toward, press flat against and knock each other; the response is visual only."
2. Lua example (line 38-39): add `collisions = false,` after `transitions = true,`.
3. Settings table: add the row `| \`plugin:liquid-glass:collisions\` | boolean / \`false\` | Liquid-only soft-body contact between windows; visual only, ignored by \`wiggle\` |`.
4. New `### Collisions` subsection under "Liquid material". It lists the rules from *Design decisions*: body box, pairs considered (floating and tiled; tiled–tiled collide while layout animations cross them, press-only), reach 40 px with release at 60 px for pairs with a floating window, `min(0.5·gap, 10 px)` meniscus, half-penetration flatten with 30% spread, pass-through at 1.25× limit, 0.35 s release when held still, windows stacked before motion stay inert until separated, impact formula, 4 contacts/window, 64 pairs, and the exclusions. State that hit-testing, real positions and layout are unchanged.
5. Update the tests sentence (line 84) to mention `tests/LiquidContactTests.cpp` and the contact cases in `LiquidMeshTests.cpp`.
6. Fixture comment example: add `collisions = true` to the commented `liquid_glass` block.

### Task 17: Nested visual verification

Follow README "Isolated nested verification" exactly: an exact-PID signature, `-i "$NEST_SIGNATURE"` on every command, and no primary config or plugin changes.

1. Build, launch nested, load `build/liquid-glass.so`, enable the settings block with `collisions = true`.
2. Open two kitty windows and float both (Alt+Space). Scripted motion: move window A toward B in exact pixel steps at a steady speed over IPC. Take the 0.57 Lua move-by-pixels dispatcher name from the pinned `example/hyprland.lua` (linked in README "Upstream contracts"); do not guess it. Capture with `grim` on the nested output at ~40 fps, the way the earlier harness did (`.project-manager/progress.md:97`; that harness has been deleted, so recreate it under `/tmp`). If scripted moves are impossible, ask the user to drag manually after explicit capture approval, as before (`progress.md:44`).
3. Scenarios, each as a contact sheet under `.project-manager/evidence/collisions/`:
   - approach to ~20 px gap: facing faces bulge toward each other (snap-in); pull back past 60 px: snap-back ripple;
   - push to ~60 px overlap: both faces flat at a shared plane, sides spread, struck window sloshes after impact;
   - small vs large window: the smaller one sloshes visibly more;
   - drive overlap past pass-through: both dents pop back, windows overlap by z-order;
   - stop mid-press: dent eases out within ~0.5 s and the field settles; afterwards there is no idle work. Repeat the idle probe method from `progress.md:103`;
   - two floating windows already stacked, then drag one out: no dent while overlapping; a meniscus appears only once they separate;
   - `collisions = false`: identical to pre-change behaviour (compare with `evidence/fluid` drag sheets);
   - `mode = "wiggle"`: no collision response;
   - tiled swap through a neighbour (fixture with 3–4 tiles; swap the top-right tile with the bottom-left one using the 0.57 Lua window swap/move dispatcher from the pinned `example/hyprland.lua`, not a guessed name). Expected: as the animating tiles start to overlap, both crossing faces flatten and the struck tile sloshes; once the overlap passes the pass-through depth, the dents pop back and the tiles finish their native animation. Repeat with a straight (horizontal) swap;
   - tiled reflow without overlap (`togglesplit`, Alt+F1): no meniscus bulge on neighbouring edges; a transient overlap of a few px may give a small press only;
   - floating window dragged onto a tiled one: full response, including the meniscus;
   - workspace switch with overlapping floating windows: no response during the slide;
   - close one window mid-press: partner's dent springs back, no stale damage.
4. Unload during an active press (×3) → no crash, nested compositor alive, no stale frame.

### Task 18: Ledger

Append a dated entry to `.project-manager/progress.md` covering: the plan path, build/ctest output (3/3), nested PID/signature, evidence paths, and any tuning applied in Task 4 Step 4. Delete the `/tmp` harness when done.

---

## Risks, tradeoffs, open questions

1. **Visual-only contact.** Clicks still land on the undeformed rectangle. A dent is at most `displacementLimit` (≤ 120 px, ≤ 20% of the shorter side), so the mismatch is bounded. Real pushing (moving windows) is out of scope.
2. **No true merge.** Apple blends container shapes into one continuous glass body. Here the meniscus only bulges the two silhouettes toward each other; drawing a bridge would need a custom shader or compositing two windows into one pass, which the plugin deliberately avoids.
3. **Lattice resolution.** A 9×9 lattice spaces nodes about W/8 apart (75 px at 600 px). Small corner contacts blur over ~2 cells. Accepted as gel-like.
4. **Rounded corners and borders.** Bodies are rectangles plus border. Rounded corners touching leave a visible sliver gap at the corner.
5. **Capture vs body alignment.** The plan assumes `getFullWindowBoundingBox()` and `geometricBox()` share world space, with `floatingOffset()` applied to both, so the face lands on the visible edge. Verify visually in Task 17. If the dent is offset by the shadow margin, the fix is local to `bodyRect`/`captureRect`.
6. **Pressure coupling may resist the dent.** Tuning levers are given in Task 4 Step 4, in order. Do not loosen tests.
7. **Held overlap relaxes.** This trades physical persistence for zero idle cost and readable content.
8. **Pair tracking is O(n²) on boxes every tick.** Ticks stop when nothing animates (the earlier idle probe saw `ticks=0`), so this costs nothing at idle and is negligible while animating at desktop window counts. Pairs are capped at 64 and storage is reserved once. If more than 64 pairs are within reach, extra pairs are untracked; a pair first discovered mid-overlap latches instead of colliding.
9. **Tiled crossings still pass through.** Real geometry is untouched, so tiles swapping places still cross each other. Collision shows as an impact slosh plus flattened faces until the overlap exceeds 2.5× the smaller window's limit (≈ 200 px for a 400 px-tall tile), then the dents pop. Fast layout animations reach that depth in a few frames, so this contact is brief by nature. Deflecting the actual tile paths would mean overriding layout animation targets; that is out of scope.
10. **Fast tiles skipping the reach net.** A tile that moves more than 60 px per tick from outside the net straight into overlap with a non-adjacent tile is first seen overlapping and latches (no collision). Tiles that start adjacent, including diagonal neighbours at `gaps_in`, are already tracked at rest, so the requested top-right ↔ bottom-left swap is covered.
11. **Open:** reach of 40 px. It is a single constant.
