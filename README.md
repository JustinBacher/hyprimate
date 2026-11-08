# liquid-glass

Hyprland C++ plugin that makes windows move like a cohesive gel **while they move, open and close**. A coupled 9×9 lattice simulates movement; a smooth 32×32 render mesh bends the captured image and silhouette. The default `liquid` material drags its tail into a curved teardrop and lands with a small forward surge. Windows pop out of a screen edge as a small bead, trailed by a coloured (default black) edge liquid that necks, snaps and recoils, then grow into place; on close they shrink back into a bead and dive into an edge. `wiggle` preserves the original directional bow/shear. These are artistic materials, not Navier–Stokes. The plugin adds no tint, shine, blur or refraction apart from the edge liquid; Liquid closes own their geometry and duration.

## Compatibility and build

Initial target: **Hyprland 0.57.0**, commit `0aa6745f705f2c4b0a5004eb019ef76e5bbdb426`, compiled with GCC 16.2.1 20260810. No compatibility with other versions is claimed. The plugin compares running-compositor and header ABI hashes before initialization; rebuild after a compositor update with matching headers and compiler. A successful link is not proof of runtime symbol compatibility.

Dependencies: matching Hyprland development headers and their pkg-config dependencies, CMake, Ninja, GCC with C++26 support. Include paths come from `pkg-config hyprland`, including hyprpm's cached headers when configured there.

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Output: `build/liquid-glass.so`. The supplied `hyprpm.toml` describes the same build; this project does not register a hyprpm repository or change autoloading.

## Load, configure, unload

**Disable built-in `decoration:wobble:enabled` first.** Initialization rejects double deformation. A conflicting reload detaches this plugin's effects, leaves it loaded but inert, and notifies once; disable built-in wobble and reload to resume. The plugin never changes that setting itself.

Load explicitly into the intended instance, using an absolute path:

```sh
hyprctl -i "$SIGNATURE" plugin load /home/justin/projects/hypr-bubble/build/liquid-glass.so
hyprctl -i "$SIGNATURE" plugin list
```

After the first load has registered the settings, add this Lua configuration:

```lua
hl.config({
    decoration = { wobble = { enabled = false } },
    plugin = {
        ["liquid_glass"] = {
            enabled = true, strength = 0.65, mode = "liquid", viscosity = 0.75,
            elasticity = 0.5, momentum = 0.5, transitions = true,
            first_window_side = "bottom", edge_bounds = "screen", close_side = "auto",
            liquid_color = 0xff000000, emerge_drag = 0.5,
        },
    },
})
```

Hyprland 0.57 normalizes hyphens to underscores in Lua config names: use `liquid_glass` in `hl.config`, while the plugin name and `hyprctl` keys remain `liquid-glass` / `plugin:liquid-glass:*`.

| Setting | Type / default | Meaning |
|---|---|---|
| `plugin:liquid-glass:enabled` | boolean / `true` | Enable deformation |
| `plugin:liquid-glass:strength` | float / `0.65` | Deformation intensity; finite values clamped to 0–1.5; zero is undeformed |
| `plugin:liquid-glass:mode` | string / `"liquid"` | `"liquid"` or `"wiggle"`; invalid names are rejected |
| `plugin:liquid-glass:viscosity` | float / `0.75` | Liquid-only internal flow resistance (damping), clamped to 0–1; higher is thicker/more cohesive |
| `plugin:liquid-glass:elasticity` | float / `0.5` | Liquid-only stiffness / recovery speed, clamped to 0–1; higher snaps back faster and firmer |
| `plugin:liquid-glass:momentum` | float / `0.5` | Liquid-only inertia carried past a stop (landing surge/overshoot), clamped to 0–1; `0` lands without overshoot |
| `plugin:liquid-glass:transitions` | boolean / `true` | Edge pop-out on open and drain on close; movement deformation is unaffected |
| `plugin:liquid-glass:first_window_side` | string / `"bottom"` | `bottom`, `top`, `left` or `right`: edge the first window on a workspace pops out of; later windows come from the side they appear on (see **Edge choice**) |
| `plugin:liquid-glass:edge_bounds` | string / `"screen"` | `screen`: the monitor's edges (windows emerge from under bars and docks); `work_area`: the edges inside reserved areas |
| `plugin:liquid-glass:close_side` | string / `"auto"` | `auto`: drain by the same side-of-appearance rule as opening, using the neighbours at close time; `nearest`: the valid edge nearest the window centre; `origin`: the edge the window opened from, falling back to `auto` when unknown or no longer valid |
| `plugin:liquid-glass:liquid_color` | color / `0xff000000` | Edge liquid colour (ARGB); alpha `0` draws no liquid |
| `plugin:liquid-glass:emerge_drag` | float / `0.5` | Share of pop-out travel fed to the movement field as drag, clamped to 0–1; `0` is a rigid pop |

Select `mode = "wiggle"` to retain the original wiggle effect; it ignores the Liquid-only numeric knobs and has no edge transitions. Disabled mode: `enabled = false` or `strength = 0`. Changing mode or any Liquid parameter clears the previous field instead of carrying incompatible displacement or velocity forward. Reload drops pending closes: native fades still alive resume natively; retained snapshots whose native fades already ended disappear immediately. Nonfinite strength or Liquid parameters bypass deformation; disable/reload restores original geometry. No low-level solver options are registered.

```sh
hyprctl -i "$SIGNATURE" reload
hyprctl -i "$SIGNATURE" plugin unload /home/justin/projects/hypr-bubble/build/liquid-glass.so
```

Remove the plugin settings block before a later config reload with the plugin absent. No primary desktop configuration, persistent autoloading or security policy is changed by this project.

## Movement behavior and boundaries

Actual current window translation drives the field, including animated tiled repositioning, snapping and interactive drags. Resizing alone adds no impulse. Fullscreen and maximized windows bypass deformation, as do snapshot captures. Teleports, output changes, invalid geometry and long timing gaps reset rather than stretching across the screen. Later eligible movement starts from a fresh baseline.

The captured client/subsurfaces, ordinary border, rounded edges and rasterized normal shadow deform together. Popups and OVERLAY decorations remain under compositor control. Input hit testing is unchanged. Other transformer effects are preserved. Physics advances once on the global tick, never inside color or blur-matte drawing; settled fields do not request continuing animation ticks or offscreen passes.

Both modes use fixed 1/240 s steps, at most 12 per interval; intervals over 250 ms reset. Triangle orientation of the lattice and of the smooth render mesh is protected at 20% of rest area. Below 0.15 px displacement and 2 px/s velocity for three substeps, the field resets exactly. Wiggle preserves its original solver (local restoring stiffness 144, damping 24, neighbor coupling 80/12) and its 12% / 96 px hard bound.

### Liquid material

- **Drag:** window velocity is low-pass filtered (60 ms) so irregular input or frames with no position update still read as steady motion. The trailing body lags by `0.075·V²/(V+600)` px per unit strength: slow drags barely deform, fast ones stretch. The trailing edge bows back (its middle lags twice as far as its corners) and the tail tapers cross-axis (18% of the lag), both along `t²`, so side edges sag into curves. The leading face grips (4× body stiffness, critically damped).
- **Stop and reversal:** a slower velocity estimate (150 ms) supplies an inertial pseudo-force from deceleration only, scaled by `momentum`; the grip relaxes over ~40 ms, so the surging tail pushes the front out a little, then one or two decaying lobes settle. A reversal re-bows the material toward the new trailing side without folding.
- **Material:** body frequency 26 rad/s scaled by `2^(elasticity-0.5)`; damping ratio from `momentum`; shear coupling 60 with viscosity damping `4 + 44·viscosity`; cross-axis pressure coupling at 1.5× the shorter capture side. Large motion soft-saturates (identity below 75% of the bound, then `tanh`) under a hard bound of min(120 px, 20% of the shorter decorated capture side).
- **Edge choice:** the first window on a workspace (no other mapped window there) uses `first_window_side`. Any other tiled window comes from the side it appears on: the screen edge opposite the side where it touches its tiled neighbours (facing sides within 64 px, weighted by contact length, using the layouts' goal boxes). This is layout-agnostic: a dwindle window comes from the side it split into, and a new scrolling column to the right of the focused one comes from the right edge rather than the top (full-height columns are always nearer the top or bottom). Ties, floating windows and windows without contact use the valid edge nearest their centre, with ties going to `first_window_side`, then bottom, right, left, top. An edge is valid only when the decorated window lies inside it; with no valid edge the native animation runs undeformed.
- **Open (after the reference droplet):** the window's content is squeezed into a small round bead of radius `r = clamp(14% of the shorter decorated side, 20, 80)·strength/0.65` that starts behind the edge, tangentially centred on the window. The bead hops out to hover at `r + 1.25·Lp` from the edge (22 rad/s scaled by `2^(elasticity−0.5)`, damping `0.8` → `0.45` with `momentum`). The edge liquid follows the bead: fillets of height `Fs = clamp(0.35 r, 5, 20)` while the bead crosses the edge, then a neck tucked `Fs` under the bead that narrows over `Lp = clamp(1.1 r, 16, 72)` and snaps at `Lp`, at the hop apex or at rest. After that the peak recoils (28 rad/s, damping `0.6` → `0.2`) with one small rebound bump. The snap starts the grow: the bead first widens along the edge (15 rad/s), then away from it (0.75× that), rounding into the rest rectangle with a soft rebound, while its centre travels to the window's rest position. Every sizing scales with the bead, so small and large windows read the same. `emerge_drag` feeds the centre's travel to the movement field, composed through a guard that keeps every triangle at ≥ 20% of its shape-only area. Native `windowsIn` scale and translation are neutralised; native fade-in still applies, so disable `fadeIn` for the cleanest pop.
- **Close:** a retained native snapshot starts from its current pose, so a close that interrupts an opening continues on the opening's edge without a jump. Over `0.46·2^(0.5−elasticity)` seconds it folds back into a bead (the normal axis first, then along the edge) while moving to the hover point near the chosen edge, then dives behind the edge. The edge rises to meet the bead within `2.5·Lp`, attaches, fillets it while it crosses, and recoils once it is gone. The liquid ramps in over 80 ms, from the current peak when an opening is interrupted. The snapshot keeps its native alpha: the edge does the disappearing. Native `windowsOut` scaling is neutralised; native translation is kept while its fadeout lives.

Movement is verified by `tests/LiquidMeshTests.cpp`; `tests/EdgeTransitionTests.cpp` covers edge choice (including the origin fallback), the hidden start, bead size until the snap, edge-first growth, image orientation, exact rest, neck attach/snap/recoil, the movement guard, liquid bounds and continuity, refresh-rate agreement, close reach/drain and interrupted-open continuity and invalid input. Real nested captures are in `.project-manager/evidence/edge/` (full-size predecessor in `superseded-full-size/`); the superseded droplet evidence remains in `.project-manager/evidence/droplet/`; earlier movement evidence remains in `.project-manager/evidence/fluid/`.

### Closing seam

Hyprland 0.57 draws closing windows from `CWindowFadeout` snapshots outside the transformer pipeline. Two hooks installed through `HyprlandAPI::createFunctionHook`, selected by exact demangled signature, associate `Desktop::CWindowFadeout::create` with the staged pose and replace associated MONITOR-mode draws in the five-argument `Render::IHyprRenderer::renderFadeouts`. If either symbol is absent or installation fails, both hooks are removed and closing falls back to native; movement and opening keep working. Hooked by-value parameters are never moved: the pinned host releases cached argument control blocks after the call. Hooks are removed before other unload teardown.

Owned closes retain the monitor snapshot beyond the native fadeout's lifetime, preserving its captured workspace, plane, z-order and no-screen-share selection. Records are capped at 16; extra closes stay native. A render batch exceeding 16 native/retained items falls back to native for that frame (retained-only items skip that frame; a suppressed native fade can become visible again during overflow). Texture-blurred closes use native rendering. Native dim-around and pre-blur apply only while the native fadeout lives; they are not extended with the retained image. Completed drains draw nothing and suppress any remaining native fade until it ends. Timing gaps are clamped to 250 ms on close. Final frames and dropped owned records damage their output so no stale snapshot or liquid remains after unload/reload.

## Isolated nested verification

`tests/nested/hyprland.lua` starts no shell services, imports no activation environment, and initially omits plugin settings. Launch directly inside an existing Wayland session:

```sh
env -u HYPRLAND_INSTANCE_SIGNATURE HYPRLAND_NO_SD_VARS=1 HYPRLAND_NO_SD_NOTIFY=1 \
  XDG_SESSION_ID="$(tr '\0' '\n' < /proc/$(pgrep -o -x Hyprland)/environ | sed -n 's/^XDG_SESSION_ID=//p')" \
  Hyprland --config /home/justin/projects/hypr-bubble/tests/nested/hyprland.lua
hyprctl instances -j
```

Launch from the primary's logind session. A shell from another (inactive) session makes the nested libseat bind that session, and the nested compositor then renders nothing ("Attempted to render frame on inactive session"); the `XDG_SESSION_ID` override above takes the primary's session without stealing control of it.

Select the instance whose PID exactly matches the launched process; set `NEST_SIGNATURE` to that entry's signature. Never use a guessed index or inherited primary signature. Use `-i "$NEST_SIGNATURE"` for every load/unload/reload command. If plugin permission is requested, explicitly approve **only** the disposable nested test; do not weaken enforcement or edit primary security configuration.

Inside the nested window: **Alt+Return** opens kitty; **Alt+Space** toggles floating; **Alt+left drag** moves; **Alt+right drag** resizes; **Alt+F1** toggles tiled split; **Alt+C** closes the fixture window. Open two windows, drag/reverse one, release and observe the material response; compare `liquid` slosh with `wiggle` separately. Test tiled repositioning independently of dragging. Ordinary opening/closing animations are enabled to verify movement does not replace them. After first load, enable the commented settings example for mode/viscosity/strength/disable/conflict/reload tests. Change the fixture monitor scale to 1.25 and 2 for logical-scale/bounds checks; the Wayland backend also needed the outer test window resized before applying the new scale. Verify the actual scale in nested `monitors -j`, not merely the configured value. Unload during motion repeatedly and reload with existing windows.

## Future lifecycle variants

Beyond the edge pop-out, a pour/drain variant (fill from the top behind a curved front, drain to bottom-centre) remains a design idea only; it is not implemented or configurable.

## Upstream contracts

Implementation target sources are pinned, not `main`:

- [Window-transform framebuffer pattern](https://github.com/hyprwm/Hyprland/blob/0aa6745f705f2c4b0a5004eb019ef76e5bbdb426/src/render/transformer/WobbleTransformer.cpp)
- [Capture, snapshot and matte integration](https://github.com/hyprwm/Hyprland/blob/0aa6745f705f2c4b0a5004eb019ef76e5bbdb426/src/render/ElementRenderer.cpp)
- [Lua configuration conventions](https://github.com/hyprwm/Hyprland/blob/0aa6745f705f2c4b0a5004eb019ef76e5bbdb426/example/hyprland.lua)
- [GCC GNU-unique / dlclose behavior](https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html#index-fno-gnu-unique): GNU builds disable unique binding so unload can release the image instead of silently reusing stale code after a rebuild.
