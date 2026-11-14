# hyprimate

hyprimate is an animation extension for [Hyprland](https://hyprland.org/) that adds custom animations to your desktop. It began as an attempt to capture the fluid motion of Apple's Liquid Glass animations, and is intended to grow with more animation styles over time.

Currently, hyprimate animates windows as they move, open, and close:

- **Liquid** gives moving windows a fluid, elastic feel and animates them in and out from the edge of the screen.
- **Cloth** lets moving windows trail and fold like a sheet pulled from the cursor or a corner, then settle flat.
- **Paper** is an alternative open and close style. A closing window crumples into a creased, lit ball where it is and disappears; an opening window starts as a ball at its own position and uncrumples into place.

![Demo of hyprimate's window animations](hyprimate-demo.webp)

[Watch the full-quality video](hyprimate-demo-closing.mp4)

## Installation

hyprimate currently targets **Hyprland 0.57.0**. Other versions have not been verified. You'll need `hyprpm`, matching Hyprland development headers, CMake, Ninja, and a C++26-capable compiler, along with hyprpm's dependencies. Rebuild the plugin after updating Hyprland.

Add the project's Git repository with hyprpm, then enable and load the plugin:

```sh
hyprpm add https://github.com/JustinBacher/hyprimate
hyprpm enable liquid-glass
hyprpm reload
```

The plugin is registered with hyprpm as `liquid-glass`. Access to the repository is required while it is private.

To load enabled plugins at startup, add `hyprpm reload` to your Hyprland autostart. See the [Hyprland plugin guide](https://wiki.hypr.land/Plugins/Using-Plugins/) for hyprpm dependencies, updates, and permission settings.

## Configuration

Hyprland 0.57 has a built-in **wobble** effect for moving and resizing windows. Its `decoration.wobble.enabled` setting defaults to `false`. If you enabled it, disable it before loading hyprimate: the two window-deformation effects conflict, and the plugin will refuse to load while both are enabled. You do not need to disable Hyprland's regular window or workspace animations.

While a window opens, hyprimate drives the window's real box along the pop-out, so anything that draws at that box, such as borders and [HyprGlass](https://github.com/hyprnux/hyprglass) panes, grows out of the bead from the edge together with the window. HyprGlass needs no changes.

The example below explicitly disables Hyprland's wobble and shows all hyprimate settings with their defaults. Add the plugin settings to your Hyprland Lua configuration after the plugin has been loaded.

Set `window.style` to `"liquid"`, `"paper"` or `"cloth"` to choose the material for movement, open and close together. Liquid deforms moving windows and animates them in and out from the screen edge; paper crumples windows open and closed and never moves them undeformed; cloth folds moving windows and opens or closes them as a sheet. Each of open and close keeps its own options for the chosen style, and movement is configured under `window.movement.options`.

Liquid closing shrinks and travels toward the screen edge in one continuous motion, without stopping at a bead-sized hover. Close momentum advances the dive; elasticity controls its speed. The optional splash follows once the window has disappeared.

Paper (`window.style` set to `"paper"`) has no screen edge, bead or splash, and does not drive the window box; windows move undeformed. Its crumples are real cloth simulations of a stiff sheet squeezed from all sides into a solid ball, baked offline with Blender and shipped inside the plugin (`assets/paper_takes.bin`); `tools/paper_bake/bake_all.sh` rebakes them. The window crumples in place, facing you: it creases all over while its outline pulls in, then collapses into the ball, without spinning. Each of open and close has its own `duration`, `crumpliness`, `shading` and `variation` under `options.paper`. Closing a window that is still uncrumpling recrumples it from its current shape. Settings used by only one style live under that style's `options` table, so the liquid `strength` and `color` are set per section.

Cloth (`window.style` set to `"cloth"`) uses **Jolt Physics soft bodies for movement, opening and closing**. The custom movement solver has been retired. All 57×57 mesh vertices are simulated at 120 Hz on a worker; the compositor samples the published mesh without running a second cloth solver.

Movement hangs the curtain from a continuous rod-pocket sleeve covering a hidden horizontal rod: no rings or hardware are rendered. The top stays horizontal and inside the actual window's left and right bounds. `window.movement.options.cloth.springiness` defaults to `0`, keeping the header completely stiff against movement-induced gathering and rebound; the lower curtain remains free-flowing. Raising it toward `1` enables progressively more horizontal spring motion. The spring pushes the sleeve back toward its resting spread even during continued travel, with spring damping rather than sliding friction. Its trailing end is reinforced by up to four times: moving left stiffens the right end, and moving right stiffens the left end. End limits and ordered fabric samples keep the pocket on the rod through movement, reversal and recovery. Jolt gravity pulls downward; the sides and weighted hem remain free, with progressively weaker return support toward the bottom. An interrupted opening finishes spreading its header smoothly even with springiness at zero. The curtain settles back to an exact flat window after motion stops. Movement keeps its `strength`, `flutter`, `shading`, `variation` and `shine` controls under `window.movement.options.cloth`.

Opening starts as one hanging curtain panel with vertical folds below the level pocket. Jolt relaxes the initial gathered shape on the worker before publishing playback, avoiding a visible initial constraint kick. Closing carries the same top support toward the selected screen edge, including downward exits; it never switches the attachment to a side or bottom edge. Open and close keep their own cloth controls, and handoffs carry the displayed shape and motion. Folds are transient: settled windows are flat and fully readable. Cloth shine defaults to zero, leaving matte folds without highlights. Independently rendered third-party glass is not deformed by the cloth mesh.

With springiness enabled, the pocket uses invisible, frictionless Jolt slider supports with rod end limits and signed neighbour limits; the latter prevent sample swapping without collision-based exclusion. The cloth follows their solved positions, and Jolt integrates the applied restoring spring and damping forces. This is a reduced-coupling model: effective header-column masses respond to window acceleration, but cloth forces do not feed back into those supports. At zero springiness, prescribed top anchors replace the extra rigid-body support system; the free panel is still a Jolt soft body. A close handoff retains the active movement's header springiness. No separate custom cloth solver is used.

When cloth opening is disabled, native opening finishes before a new cloth movement session starts. An active cloth opening can instead hand its displayed sheet directly to movement.

Brief cloth-worker publication gaps hold the last available finite animation state and resume the same worker when it catches up, rather than restarting the animation. This applies to movement, opening and closing; a paused recording is not physical settling or a completed close. Failed workers and gaps reaching the stall bound still use the safe fallback.

Active window animations continue to render until they settle, including after scrolling-layout focus changes; no mouse movement is needed to complete them.

<!-- BEGIN GENERATED CONFIGURATION -->
After the first load has registered the settings, add this Lua configuration:

```lua
hl.config({
    decoration = { wobble = { enabled = false } },
    plugin = {
        ["liquid_glass"] = {
            window = {
                enabled = true,
                style = "liquid",
                movement = {
                    enabled = true,
                    options = {
                        liquid = {
                            strength = 0.65,
                            viscosity = 0.75,
                            elasticity = 0.5,
                            momentum = 0.5,
                            variation = 0.5,
                        },
                        cloth = {
                            strength = 0.65,
                            springiness = 0,
                            flutter = 0.5,
                            shading = 0.7,
                            shine = 0,
                            variation = 0.5,
                        },
                    },
                },
                open = {
                    enabled = true,
                    options = {
                        liquid = {
                            strength = 0.65,
                            color = 0xff000000,
                            first_window_side = "bottom",
                            edge_bounds = "screen",
                            emerge_drag = 0.5,
                            elasticity = 0.5,
                            momentum = 0.5,
                        },
                        paper = {
                            duration = 0.6,
                            crumpliness = 0.6,
                            shading = 0.7,
                            variation = 0.5,
                        },
                        cloth = {
                            duration = 1.0,
                            flutter = 0.5,
                            shading = 0.7,
                            shine = 0,
                            variation = 0.5,
                        },
                    },
                },
                close = {
                    enabled = true,
                    options = {
                        liquid = {
                            strength = 0.65,
                            color = 0xff000000,
                            side = "auto",
                            edge_bounds = "screen",
                            splash = true,
                            elasticity = 0.5,
                            momentum = 0.5,
                        },
                        paper = {
                            duration = 0.5,
                            crumpliness = 0.6,
                            shading = 0.7,
                            variation = 0.5,
                        },
                        cloth = {
                            duration = 0.9,
                            flutter = 0.5,
                            shading = 0.7,
                            shine = 0,
                            variation = 0.5,
                        },
                    },
                },
            },
        },
    },
})
```

Hyprland 0.57 normalizes hyphens to underscores in Lua config names: use `liquid_glass` in `hl.config`, while the plugin name and `hyprctl` keys remain `liquid-glass` / `plugin:liquid-glass:*`. Each nested Lua table maps to colon-separated segments of the key, for example `window.movement.options.liquid.viscosity` is `plugin:liquid-glass:window:movement:options:liquid:viscosity`.

| Setting | Type / default | Meaning |
|---|---|---|
| `plugin:liquid-glass:window:enabled` | boolean / `true` | Master switch: when false, no movement, open or close deformation runs. |
| `plugin:liquid-glass:window:style` | string / `"liquid"` | Material for movement, open and close: liquid deforms edges with a viscous field, paper crumples into a ball (paper windows move undeformed), cloth drapes and flutters. Invalid names are rejected. Allowed: `liquid`, `paper`, `cloth`. |
| `plugin:liquid-glass:window:movement:enabled` | boolean / `true` | Deform windows while they move. Does not enable or disable open or close. |
| `plugin:liquid-glass:window:movement:options:liquid:strength` | float / `0.65` | Liquid-only movement deformation intensity; zero moves windows undeformed. Finite values clamped to 0–1.5. |
| `plugin:liquid-glass:window:movement:options:liquid:viscosity` | float / `0.75` | Liquid-only internal flow resistance; higher is thicker and more cohesive. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:liquid:elasticity` | float / `0.5` | Liquid-only movement stiffness and recovery speed; higher snaps back faster. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:liquid:momentum` | float / `0.5` | Liquid-only movement inertia after a stop; zero lands without overshoot. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:liquid:variation` | float / `0.5` | Liquid-only per-window and per-movement randomness; zero moves windows identically. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:cloth:strength` | float / `0.65` | Cloth-only movement deformation intensity; zero moves windows undeformed. Finite values clamped to 0–1.5. |
| `plugin:liquid-glass:window:movement:options:cloth:springiness` | float / `0` | Horizontal springiness of the curtain header; zero keeps the top stiff, higher values allow more gathering and rebound. The lower curtain stays free-flowing. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:cloth:flutter` | float / `0.5` | Cloth-only movement ripple strength; zero removes the varying gusts while motion still bends the sheet. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:cloth:shading` | float / `0.7` | Cloth-only light and shadow on movement folds; zero draws the folds unlit. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:cloth:shine` | float / `0` | Cloth-only highlight strength on the folds; zero removes highlights. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:cloth:variation` | float / `0.5` | Cloth-only per-window movement variation; zero uses the same motion for every window. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:enabled` | boolean / `true` | Animate windows as they open (see style); false opens without it. Movement is unaffected. |
| `plugin:liquid-glass:window:open:options:liquid:strength` | float / `0.65` | Liquid-only pop-out size and deformation intensity; zero opens without the pop-out. Finite values clamped to 0–1.5. |
| `plugin:liquid-glass:window:open:options:liquid:color` | color / `0xff000000` | Liquid-only edge liquid colour (ARGB); alpha zero draws no liquid. |
| `plugin:liquid-glass:window:open:options:liquid:first_window_side` | string / `"bottom"` | Edge the first window on a workspace pops out of; later windows use the side they appear on (see Edge choice). Allowed: `bottom`, `top`, `left`, `right`. |
| `plugin:liquid-glass:window:open:options:liquid:edge_bounds` | string / `"screen"` | Use monitor edges (screen) or edges inside reserved areas (work_area) for the pop-out. Allowed: `screen`, `work_area`. |
| `plugin:liquid-glass:window:open:options:liquid:emerge_drag` | float / `0.5` | Share of pop-out travel fed to the movement field as drag; zero is a rigid pop. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:liquid:elasticity` | float / `0.5` | Open-only stiffness and recovery speed of the pop-out; higher snaps back faster. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:liquid:momentum` | float / `0.5` | Open-only inertia of the pop-out; zero lands without overshoot. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:paper:duration` | float / `0.6` | Paper-only seconds for the ball to uncrumple into the window. Finite values clamped to 0.1–3. |
| `plugin:liquid-glass:window:open:options:paper:crumpliness` | float / `0.6` | Paper-only crumple tightness; higher crumples into a smaller, more creased ball. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:paper:shading` | float / `0.7` | Paper-only light and shadow on the creases; zero draws the folds unlit. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:paper:variation` | float / `0.5` | Paper-only per-window randomness of which crumple plays and its mirroring; zero crumples every window identically. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:cloth:duration` | float / `1.0` | Cloth-only seconds for the gathered curtain panel to spread from its top edge and settle flat. Finite values clamped to 0.1–4. |
| `plugin:liquid-glass:window:open:options:cloth:flutter` | float / `0.5` | Cloth-only gust strength; zero removes flutter while gravity still moves the sheet. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:cloth:shading` | float / `0.7` | Cloth-only light and shadow on the folds; zero draws the folds unlit. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:cloth:shine` | float / `0` | Cloth-only highlight strength on the folds; zero removes highlights. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:cloth:variation` | float / `0.5` | Cloth-only per-window variation; zero uses the same motion for every window. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:enabled` | boolean / `true` | Animate windows as they close (see style); false closes natively. Movement is unaffected. |
| `plugin:liquid-glass:window:close:options:liquid:strength` | float / `0.65` | Liquid-only drain deformation intensity; zero closes natively. Finite values clamped to 0–1.5. |
| `plugin:liquid-glass:window:close:options:liquid:color` | color / `0xff000000` | Liquid-only edge liquid colour (ARGB); alpha zero draws no liquid. |
| `plugin:liquid-glass:window:close:options:liquid:side` | string / `"auto"` | Shared liquid/cloth exit: side of appearance (auto), nearest valid edge, opening origin, or a fixed edge. Invalid preferred edges fall back to the normal resolver. Allowed: `auto`, `nearest`, `origin`, `top`, `bottom`, `left`, `right`. |
| `plugin:liquid-glass:window:close:options:liquid:edge_bounds` | string / `"screen"` | Shared liquid/cloth edge choice: monitor edges (screen) or edges inside reserved areas (work_area). Cloth travels beyond the actual screen before disappearing. Allowed: `screen`, `work_area`. |
| `plugin:liquid-glass:window:close:options:liquid:splash` | boolean / `true` | Small edge-liquid splash where a closing window disappears; false closes without liquid. |
| `plugin:liquid-glass:window:close:options:liquid:elasticity` | float / `0.5` | Close-only stiffness and recovery speed of the drain; higher snaps back faster. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:options:liquid:momentum` | float / `0.5` | Close-only dive acceleration; higher advances the dive faster. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:options:paper:duration` | float / `0.5` | Paper-only seconds to crumple the window into a ball and vanish. Finite values clamped to 0.1–3. |
| `plugin:liquid-glass:window:close:options:paper:crumpliness` | float / `0.6` | Paper-only crumple tightness; higher crumples into a smaller, more creased ball. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:options:paper:shading` | float / `0.7` | Paper-only light and shadow on the creases; zero draws the folds unlit. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:options:paper:variation` | float / `0.5` | Paper-only per-window randomness of which crumple plays and its mirroring; zero crumples every window identically. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:options:cloth:duration` | float / `0.9` | Cloth-only seconds for the top rod to carry the curtain beyond the selected screen edge. Finite values clamped to 0.1–4. |
| `plugin:liquid-glass:window:close:options:cloth:flutter` | float / `0.5` | Cloth-only gust strength; zero removes flutter while gravity still moves the sheet. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:options:cloth:shading` | float / `0.7` | Cloth-only light and shadow on the folds; zero draws the folds unlit. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:options:cloth:shine` | float / `0` | Cloth-only highlight strength on the folds; zero removes highlights. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:options:cloth:variation` | float / `0.5` | Cloth-only per-window variation; zero uses the same motion for every window. Finite values clamped to 0–1. |
<!-- END GENERATED CONFIGURATION -->

### Edge choice

The first window on a workspace opens from `first_window_side`. Later tiled windows normally open from the screen edge on the side where they appear; floating windows and windows without a clear neighboring window use the nearest valid edge. Set `edge_bounds` to `"work_area"` to use edges inside reserved areas instead of the screen edges.

Liquid and cloth share the existing `window.close.options.liquid.side` and `edge_bounds` controls. For closing windows, `side = "auto"` chooses the side of appearance, `"nearest"` uses the nearest valid edge, and `"origin"` uses the opening edge when available. Fixed `"top"`, `"bottom"`, `"left"` and `"right"` overrides prefer that edge; an invalid preferred edge falls back to the normal resolver. Cloth opens in place but records the same logical opening origin as liquid. Its exit choice can use the work area, and its travel always continues beyond the actual monitor boundary before the sheet disappears.

## Development

Format the C++ sources with `clang-format`:

```sh
cmake -S . -B build
cmake --build build --target format
```

Check formatting without changing files with `cmake --build build --target format-check`.
