# hyprimate

hyprimate is an animation extension for [Hyprland](https://hyprland.org/) that adds custom animations to your desktop. It began as an attempt to capture the fluid motion of Apple's Liquid Glass animations, and is intended to grow with more animation styles over time.

Currently, hyprimate animates windows as they move, open, and close:

- **Liquid** gives moving windows a fluid, elastic feel and animates them in and out from the edge of the screen.
- **Cloth** lets moving windows trail and fold like a sheet pulled from the cursor or a corner, then settle flat.
- **Paper** is an alternative open and close style. A closing window crumples into a creased, lit ball where it is and disappears; an opening window starts as a ball at its own position and uncrumples into place.
- **Swarm** breaks a window into creature-shaped pieces of itself. They scurry in from the screen edges and merge into the window when it opens, trail and regroup when it moves, and scatter off the screen when it closes.

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

## Animation architecture

The root-level compositor adapters (`AnimationTransformer` and `AnimationCloseRenderer`) track windows, schedule damage, and integrate retained close fadeouts. Style-specific live and close state, lifecycle decisions, and rendering live under `src/animations/liquid/`, `paper/`, `cloth/`, and `swarm/`. Shared sheet state, rendering, and animation primitives live under `src/animations/shared/`.

Swarm keeps three concerns apart so creatures can be swapped without touching the motion: `Swarm` is the fixed-timestep steering simulation (where each agent is, how far it has walked, how locked into the window it is); `SwarmSprite` maps that to a walk-cycle frame, a rotation and a size from a sheet's measurements; `SwarmRenderer` bakes the sheets in `src/animations/swarm/sprites/` into mask atlases and draws the window through them. A new creature is a PNG sheet of equal walk-cycle frames plus one entry in `sourceOf()` (`SwarmRenderer.cpp`), the `SwarmCreature` enum and the `creature` choices in `config/settings.json`.

## Configuration

Hyprland 0.57 has a built-in **wobble** effect for moving and resizing windows. Its `decoration.wobble.enabled` setting defaults to `false`. If you enabled it, disable it before loading hyprimate: the two window-deformation effects conflict, and the plugin will refuse to load while both are enabled. You do not need to disable Hyprland's regular window or workspace animations.

While a window opens, hyprimate drives the window's real box along the pop-out, so anything that draws at that box, such as borders and [HyprGlass](https://github.com/hyprnux/hyprglass) panes, grows out of the bead from the edge together with the window. HyprGlass needs no changes.

The example below explicitly disables Hyprland's wobble and shows all hyprimate settings with their defaults. Add the plugin settings to your Hyprland Lua configuration after the plugin has been loaded.

Set `window.style` to `"liquid"`, `"paper"`, `"cloth"` or `"swarm"` to choose the material for movement, open and close together. Liquid deforms moving windows and animates them in and out from the screen edge; paper crumples windows open and closed and never moves them undeformed; cloth folds moving windows and opens or closes them as a sheet; swarm carries the window as a flock of creatures. Each of open and close keeps its own options for the chosen style, and movement is configured under `window.movement.options`.

Liquid closing shrinks and travels toward the screen edge in one continuous motion, without stopping at a bead-sized hover. Close momentum advances the dive; elasticity controls its speed. The optional splash follows once the window has disappeared.

Paper (`window.style` set to `"paper"`) has no screen edge, bead or splash, and does not drive the window box; windows move undeformed. Its crumples are real cloth simulations of a stiff sheet squeezed from all sides into a solid ball, baked offline with Blender and shipped inside the plugin (`src/animations/paper/assets/paper_takes.bin`); `src/animations/paper/tools/bake_all.sh` rebakes them. The window crumples in place, facing you: it creases all over while its outline pulls in, then collapses into the ball, without spinning. Each of open and close has its own `duration`, `crumpliness`, `shading` and `variation` under `options.paper`. Closing a window that is still uncrumpling recrumples it from its current shape. Settings used by only one style live under that style's `options` table, so the liquid `strength` and `color` are set per section.

Cloth (`window.style` set to `"cloth"`) uses **Jolt Physics soft bodies for movement, opening and closing**. The custom movement solver has been retired. All 57×57 mesh vertices are simulated at 120 Hz on a worker; the compositor samples the published mesh without running a second cloth solver.

Movement hangs the curtain from a continuous rod-pocket sleeve covering a hidden horizontal rod: no rings or hardware are rendered. The top stays horizontal and inside the actual window's left and right bounds. `window.movement.options.cloth.springiness` defaults to `0`, keeping the header completely stiff against movement-induced gathering and rebound; the lower curtain remains free-flowing. Raising it toward `1` enables progressively more horizontal spring motion. The spring pushes the sleeve back toward its resting spread even during continued travel, with spring damping rather than sliding friction. Its trailing end is reinforced by up to four times: moving left stiffens the right end, and moving right stiffens the left end. End limits and ordered fabric samples keep the pocket on the rod through movement, reversal and recovery. Jolt gravity pulls downward; the sides and weighted hem remain free, with progressively weaker return support toward the bottom. An interrupted opening finishes spreading its header smoothly even with springiness at zero. The curtain settles back to an exact flat window after motion stops. Movement keeps its `strength`, `flutter`, `shading`, `variation` and `shine` controls under `window.movement.options.cloth`.

Opening starts as one hanging curtain panel with vertical folds below the level pocket. Jolt relaxes the initial gathered shape on the worker before publishing playback, avoiding a visible initial constraint kick. Closing carries the same top support toward the selected screen edge, including downward exits; it never switches the attachment to a side or bottom edge. Open and close keep their own cloth controls, and handoffs carry the displayed shape and motion. Folds are transient: settled windows are flat and fully readable. Cloth shine defaults to zero, leaving matte folds without highlights. Independently rendered third-party glass is not deformed by the cloth mesh.

With springiness enabled, the pocket uses invisible, frictionless Jolt slider supports with rod end limits and signed neighbour limits; the latter prevent sample swapping without collision-based exclusion. The cloth follows their solved positions, and Jolt integrates the applied restoring spring and damping forces. This is a reduced-coupling model: effective header-column masses respond to window acceleration, but cloth forces do not feed back into those supports. At zero springiness, prescribed top anchors replace the extra rigid-body support system; the free panel is still a Jolt soft body. A close handoff retains the active movement's header springiness. No separate custom cloth solver is used.

When cloth opening is disabled, native opening finishes before a new cloth movement session starts. An active cloth opening can instead hand its displayed sheet directly to movement.

Brief cloth-worker publication gaps hold the last available finite animation state and resume the same worker when it catches up, rather than restarting the animation. This applies to movement, opening and closing; a paused recording is not physical settling or a completed close. Failed workers and gaps reaching the stall bound still use the safe fallback.

Swarm (`window.style` set to `"swarm"`) cuts the window into `count` near-square tiles, one per creature. A loose creature is its tile seen through the creature's silhouette, so it is literally made of that part of the window, live content included; it walks with the sheet's walk cycle and turns to face where it is going. Because each creature is cut from its own tile, creatures get larger as `count` goes down.

Creatures are steered by a custom fixed 1/120 s solver (no physics engine), and none of them walks a straight line. Each approaches its place on its own arc, meanders and hesitates on the way (`wander`), steps around creatures in its path and cannot walk through another one's body (`separation`), and loosely follows its neighbours (`cohesion`); each has its own pace, grip and arc (`variation`). Bodies are solid only in transit: near its own place a creature lets the rest of the crowd through, so arrivals can settle. There a spring places it exactly, it swings round to face a direction of its own, so no two settle alike, and swells until it fills its tile. Creatures still on their way walk over what has already merged.

What has merged reads as a blob, not as a stack of squares. A merged tile on its own is rounded off; where merged tiles meet, their corners square up into each other, and the notch two of them leave at the corner of a third is filled in, so a group has one smooth outline, holes included. Only when the last tile is in do the remaining corners square up into the window, which is then drawn natively again.

Nothing about where a creature comes from or when it arrives depends on where its tile is. Opening sends each one in from a random point beyond a random one of the allowed screen edges, and times it to reach its place at a random moment within `stagger` seconds, hurrying or dawdling a little to keep to that; near and far tiles therefore fill in no order (zero lands them all together). Closing lets creatures go in random order over `stagger` and runs each off a random point of the edges. Moving a window slower than a creature's grip keeps it whole; faster movement shakes creatures loose, each stays where it was for its own reaction time, then chases the window, and they regroup when it stops. `responsiveness` sets the grip and how hard they accelerate, `settling` how eagerly they lock back in. All swarm settings are shared by movement, open and close under `window.options.swarm`; only the screen sides are per direction. Changed settings apply from the next animation.

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
                options = {
                    swarm = {
                        creature = "spider",
                        count = 250,
                        speed = 1400,
                        stagger = 0.6,
                        cohesion = 0.5,
                        separation = 0.5,
                        responsiveness = 0.5,
                        settling = 0.5,
                        wander = 0.5,
                        variation = 0.5,
                    },
                },
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
                        swarm = {
                            lone_window_sides = "all",
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
                        swarm = {
                            lone_window_sides = "all",
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
| `plugin:liquid-glass:window:style` | string / `"liquid"` | Material for movement, open and close: liquid deforms edges with a viscous field, paper crumples into a ball (paper windows move undeformed), cloth drapes and flutters, swarm breaks the window into creature-shaped pieces that scurry and regroup. Invalid names are rejected. Allowed: `liquid`, `paper`, `cloth`, `swarm`. |
| `plugin:liquid-glass:window:options:swarm:creature` | string / `"spider"` | Swarm-only creature every window piece is shaped as. Allowed: `spider`, `cat`. |
| `plugin:liquid-glass:window:options:swarm:count` | int / `250` | Swarm-only number of creatures a window breaks into; each carries one piece of the window, so fewer creatures are larger. Clamped to 1–400. |
| `plugin:liquid-glass:window:options:swarm:speed` | float / `1400` | Swarm-only creature cruise speed in logical pixels per second. Finite values clamped to 100–6000. |
| `plugin:liquid-glass:window:options:swarm:stagger` | float / `0.6` | Swarm-only seconds over which creatures reach their places in random order when a window opens, or let go in random order when it closes, and half that when movement shakes them loose; zero lands or releases them all together. Finite values clamped to 0–3. |
| `plugin:liquid-glass:window:options:swarm:cohesion` | float / `0.5` | Swarm-only pull that keeps nearby creatures travelling together; zero lets each find its own way. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:options:swarm:separation` | float / `0.5` | Swarm-only personal space while travelling: creatures step around those in their way and bump off each other instead of passing through; zero lets them cross freely. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:options:swarm:responsiveness` | float / `0.5` | Swarm-only acceleration and grip: higher holds formation through faster window movement and turns more sharply; lower breaks apart sooner and trails further. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:options:swarm:settling` | float / `0.5` | Swarm-only eagerness to lock back into the window: higher regroups and merges sooner; lower mills about longer first. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:options:swarm:wander` | float / `0.5` | Swarm-only meander and hesitation while travelling; zero walks steadily along its route. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:options:swarm:variation` | float / `0.5` | Swarm-only per-creature difference in speed, grip and how far its route curves; zero sends every creature straight and alike. Finite values clamped to 0–1. |
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
| `plugin:liquid-glass:window:open:options:swarm:lone_window_sides` | set / `"all"` | Swarm-only screen sides the creatures enter from when the window is the first on its workspace; later windows enter from the side they appear on (see Edge choice). Comma-separated list of: `top`, `bottom`, `left`, `right`; `all` selects every one and overrides the rest. |
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
| `plugin:liquid-glass:window:close:options:swarm:lone_window_sides` | set / `"all"` | Swarm-only screen sides the creatures leave through when the window is the last on its workspace; otherwise they leave through the side the window sits on (see Edge choice). Comma-separated list of: `top`, `bottom`, `left`, `right`; `all` selects every one and overrides the rest. |
<!-- END GENERATED CONFIGURATION -->

### Edge choice

The first window on a workspace opens from `first_window_side`. Later tiled windows normally open from the screen edge on the side where they appear; floating windows and windows without a clear neighboring window use the nearest valid edge. Set `edge_bounds` to `"work_area"` to use edges inside reserved areas instead of the screen edges.

Liquid and cloth share the existing `window.close.options.liquid.side` and `edge_bounds` controls. For closing windows, `side = "auto"` chooses the side of appearance, `"nearest"` uses the nearest valid edge, and `"origin"` uses the opening edge when available. Fixed `"top"`, `"bottom"`, `"left"` and `"right"` overrides prefer that edge; an invalid preferred edge falls back to the normal resolver. Cloth opens in place but records the same logical opening origin as liquid. Its exit choice can use the work area, and its travel always continues beyond the actual monitor boundary before the sheet disappears.

Swarm can use several edges at once; each creature picks one of them at random, and a random point along it. A window that is alone on its workspace (the first to open, the last to close) uses `lone_window_sides`: a comma-separated list such as `"top,left"`, or `"all"`, which overrides anything listed with it. Otherwise a tiled window uses the screen edges facing away from its neighbours, both when it opens and when it closes; a window wedged between neighbours uses its free edges, and floating windows use the nearest edge. Swarm always uses the monitor's own edges.

## Development

Format the C++ sources with `clang-format`:

```sh
cmake -S . -B build
cmake --build build --target format
```

Check formatting without changing files with `cmake --build build --target format-check`.
