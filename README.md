# hyprimate

hyprimate is an animation extension for [Hyprland](https://hyprland.org/) that adds custom animations to your desktop. It began as an attempt to capture the fluid motion of Apple's Liquid Glass animations, and is intended to grow with more animation styles over time.

Currently, hyprimate animates windows as they move, open, and close:

- **Liquid** gives moving windows a fluid, elastic feel and animates them in and out from the edge of the screen.
- **Cloth** lets moving windows trail and fold like a sheet pulled from the cursor or a corner, then settle flat.
- **Paper** is an alternative open and close style. A closing window crumples into a creased, lit ball where it is and disappears; an opening window starts as a ball at its own position and uncrumples into place.
- **Swarm** breaks a window into creature-shaped pieces of itself. They scurry in from the screen edges and merge into the window when it opens, trail and regroup when it moves, and scatter off the screen when it closes.
- **Bubble** blows a clear, iridescent soap bubble at the window's position. It bursts the way a real film does — a thinning spot tears open, the holes' rims race across the film and break into ligaments and droplets — while a clear inner film takes on the window and splats into place. Moving windows behave like the same film full of liquid: the trailing side lags and bows out, ripples run around the outline, small bubbles form, slide back and detach, and a thin film is left behind on the screen. Closing bursts the window's own film the same way.

![Demo of hyprimate's window animations](hyprimate-demo.webp)

[Watch the full-quality video](hyprimate-demo-closing.mp4)

## Installation

hyprimate currently targets **Hyprland 0.57.0**. Other versions have not been verified. You'll need `hyprpm`, matching Hyprland development headers, CMake, Ninja, and a C++26-capable compiler, along with hyprpm's dependencies. Rebuild the plugin after updating Hyprland.

Add the project's Git repository with hyprpm, then enable and load the plugin:

```sh
hyprpm add https://github.com/JustinBacher/hyprimate
hyprpm enable hyprimate
hyprpm reload
```

The plugin is registered with hyprpm as `hyprimate`. Access to the repository is required while it is private.

If you used the former `liquid-glass` plugin name, update your hyprpm entry and Lua configuration to `hyprimate`. The settings now use `plugin:hyprimate:*`; the former keys are no longer registered.

To load enabled plugins at startup, add `hyprpm reload` to your Hyprland autostart. See the [Hyprland plugin guide](https://wiki.hypr.land/Plugins/Using-Plugins/) for hyprpm dependencies, updates, and permission settings.

## Animation architecture

The root-level compositor adapters (`AnimationTransformer` and `AnimationCloseRenderer`) track windows, schedule damage, and integrate retained close fadeouts. Style-specific live and close state, lifecycle decisions, and rendering live under `src/animations/liquid/`, `paper/`, `cloth/`, `swarm/`, and `bubble/`. Shared sheet state, rendering, and animation primitives live under `src/animations/shared/`.

Swarm keeps three concerns apart so creatures can be swapped without touching the motion: `Swarm` is the fixed-timestep steering simulation (where each agent is, how far it has walked, how locked into the window it is); `SwarmSprite` maps that to a walk-cycle frame, a rotation and a size from a sheet's measurements; `SwarmRenderer` bakes the sheets in `src/animations/swarm/sprites/` into mask atlases and draws the window through them. A new creature is a PNG sheet of equal walk-cycle frames plus one entry in `sourceOf()` (`SwarmRenderer.cpp`), the `SwarmCreature` enum and the `creature` choices in `config/settings.json`.

## Configuration

Hyprland 0.57 has a built-in **wobble** effect for moving and resizing windows. Its `decoration.wobble.enabled` setting defaults to `false`. If you enabled it, disable it before loading hyprimate: the two window-deformation effects conflict, and the plugin will refuse to load while both are enabled. You do not need to disable Hyprland's regular window or workspace animations.

While a liquid window opens, hyprimate drives the window's real box along the pop-out, so anything that draws at that box, such as borders and [HyprGlass](https://github.com/hyprnux/hyprglass) panes, grows out of the bead from the edge together with the window. HyprGlass needs no changes for that style.

The example below explicitly disables Hyprland's wobble and shows all hyprimate settings with their defaults. Add the plugin settings to your Hyprland Lua configuration after the plugin has been loaded.

Set `window.style` to `"liquid"`, `"paper"`, `"cloth"`, `"swarm"` or `"bubble"` to choose the material for movement, open and close together. Liquid deforms moving windows and animates them in and out from the screen edge; paper crumples windows open and closed; cloth folds moving windows and opens or closes them as a sheet; swarm carries the window as a flock of creatures; bubble inflates and pops a soap-film surface and sloshes it as it moves. Paper leaves movement undeformed. Each of open and close keeps its own options for the chosen style, and movement is configured under `window.movement.options`.

Liquid closing shrinks and travels toward the screen edge in one continuous motion, without stopping at a bead-sized hover. Close momentum advances the dive; elasticity controls its speed. The optional splash follows once the window has disappeared.

Paper (`window.style` set to `"paper"`) has no screen edge, bead or splash, and does not drive the window box; windows move undeformed. Its crumples are real cloth simulations of a stiff sheet squeezed from all sides into a solid ball, baked offline with Blender and shipped inside the plugin (`src/animations/paper/assets/paper_takes.bin`); `src/animations/paper/tools/bake_all.sh` rebakes them. The window crumples in place, facing you: it creases all over while its outline pulls in, then collapses into the ball, without spinning. Each of open and close has its own `duration`, `crumpliness`, `shading` and `variation` under `options.paper`. Closing a window that is still uncrumpling recrumples it from its current shape. Settings used by only one style live under that style's `options` table, so the liquid `strength` and `color` are set per section.

Cloth (`window.style` set to `"cloth"`) uses **Jolt Physics soft bodies for movement, opening and closing**. The custom movement solver has been retired. All 57×57 mesh vertices are simulated at 120 Hz on a worker; the compositor samples the published mesh without running a second cloth solver.

Movement hangs the curtain from a continuous rod-pocket sleeve covering a hidden horizontal rod: no rings or hardware are rendered. The top stays horizontal and inside the actual window's left and right bounds. `window.movement.options.cloth.springiness` defaults to `0`, keeping the header completely stiff against movement-induced gathering and rebound; the lower curtain remains free-flowing. Raising it toward `1` enables progressively more horizontal spring motion. The spring pushes the sleeve back toward its resting spread even during continued travel, with spring damping rather than sliding friction. Its trailing end is reinforced by up to four times: moving left stiffens the right end, and moving right stiffens the left end. End limits and ordered fabric samples keep the pocket on the rod through movement, reversal and recovery. Jolt gravity pulls downward; the sides and weighted hem remain free, with progressively weaker return support toward the bottom. An interrupted opening finishes spreading its header smoothly even with springiness at zero. The curtain settles back to an exact flat window after motion stops. Movement keeps its `strength`, `flutter`, `shading`, `variation` and `shine` controls under `window.movement.options.cloth`.

Opening starts as one hanging curtain panel with vertical folds below the level pocket. Jolt relaxes the initial gathered shape on the worker before publishing playback, avoiding a visible initial constraint kick. Closing carries the same top support toward the selected screen edge, including downward exits; it never switches the attachment to a side or bottom edge. Open and close keep their own cloth controls, and handoffs carry the displayed shape and motion. Folds are transient: settled windows are flat and fully readable. Cloth shine defaults to zero, leaving matte folds without highlights. Independently rendered third-party glass is not deformed by the cloth mesh.

With springiness enabled, the pocket uses invisible, frictionless Jolt slider supports with rod end limits and signed neighbour limits; the latter prevent sample swapping without collision-based exclusion. The cloth follows their solved positions, and Jolt integrates the applied restoring spring and damping forces. This is a reduced-coupling model: effective header-column masses respond to window acceleration, but cloth forces do not feed back into those supports. At zero springiness, prescribed top anchors replace the extra rigid-body support system; the free panel is still a Jolt soft body. A close handoff retains the active movement's header springiness. No separate custom cloth solver is used.

When cloth opening is disabled, native opening finishes before a new cloth movement session starts. An active cloth opening can instead hand its displayed sheet directly to movement.

Brief cloth-worker publication gaps hold the last available finite animation state and resume the same worker when it catches up, rather than restarting the animation. This applies to movement, opening and closing; a paused recording is not physical settling or a completed close. Failed workers and gaps reaching the stall bound still use the safe fallback.

Swarm (`window.style` set to `"swarm"`) cuts the window into `count` near-square tiles, one per creature. A loose creature is its tile seen through the creature's silhouette, so it is literally made of that part of the window, live content included; it walks with the sheet's walk cycle and turns to face where it is going. The gait follows the creature's speed and continues while it turns in place; its rate is capped to keep frames visible when small creatures move quickly. Because each creature is cut from its own tile, creatures get larger as `count` goes down.

Creatures are steered by a custom fixed 1/120 s solver (no physics engine), and none of them walks a straight line. Each approaches its place on its own arc, meanders and hesitates on the way (`wander`), steps around creatures in its path and cannot walk through another one's body (`separation`), and loosely follows its neighbours (`cohesion`); each has its own pace, grip and arc (`variation`). Bodies are solid only in transit: near its own place a creature lets the rest of the crowd through, so arrivals can settle. There a spring places it exactly, it swings round to face a direction of its own, so no two settle alike, and swells until it fills its tile. Creatures still on their way walk over what has already merged.

What has merged reads as a blob, not as a stack of squares. A merged tile on its own is rounded off; where merged tiles meet, their corners square up into each other, and the notch two of them leave at the corner of a third is filled in, so a group has one smooth outline, holes included. Only when the last tile is in do the remaining corners square up into the window, which is then drawn natively again.

During a movement animation, resizing keeps each creature assigned to the same tile instead of recutting the grid into different rows. Creatures that reach their tiles after movement wait for an independently randomized interval up to `stagger` before merging; zero removes the wait.

Nothing about where a creature comes from or when it arrives depends on where its tile is. Opening sends each one in from a random point beyond a random one of the allowed screen edges, and times it to reach its place at a random moment within `stagger` seconds, hurrying or dawdling a little to keep to that; near and far tiles therefore fill in no order (zero lands them all together). Closing lets creatures go in random order over `stagger` and runs each off a random point of the edges. Moving a window slower than a creature's grip keeps it whole; faster movement shakes creatures loose, each stays where it was for its own reaction time, then chases the window, and they regroup when it stops. `responsiveness` sets the grip and how hard they accelerate, `settling` how eagerly they lock back in. All swarm settings are shared by movement, open and close under `window.options.swarm`; only the screen sides are per direction. Changed settings apply from the next animation.

Bubble (`window.style` set to `"bubble"`) starts round even for a wide or tall window. The outer bubble is a clear soap film: thin-film interference colours swirl and drain across it and grow more vivid as inflation thins the film; it reflects a soft window light (upright on its front, inverted and dimmer on its back) and bends the wallpaper behind it only slightly, near its rim. It refracts the wallpaper, not the opening window or other windows behind it, and stays circular until it bursts. The burst starts where drainage thins the film most: interference fringes collapse into a black spot, a small hole opens there and accelerates to a constant speed, and a few more holes open at thin spots ahead of it. Every hole's rim is a beaded liquid thread that gathers the retracting film, sprouts ligaments that pinch off into a mist of droplets, and leaves short-lived strands and irregular islands where holes meet; the last film collapses into a small cluster of drops. Meanwhile a separate, clear inner film emerges from the middle as a circle, takes on the window's imagery while it stretches into its rounded rectangular shape, and splats into place: its perimeter lands first while the fluid center compresses, rebounds and settles, bending the content and reflections. The final surface resolves into the native renderer.

Closing never reverses inflation. Nothing changes on the window until a spot of its film thins and tears. A finite band of surviving film then peels out of the window plane and curls back, carrying its imagery with it rather than leaving a flat copy underneath. Where fronts meet and the last island collapses, source-owned pieces detach, turn and contract; their cutouts persist so the detached material cannot reappear on the parent film. Droplets drift, settle and evaporate within the close duration. A close during opening continues the holes, droplets and detached sheets already in flight and bursts the window's film as well. Native close slides do not move the bubble.

Bubble has two timing controls: `window.open.options.bubble.duration` (default `0.62` seconds) and `window.close.options.bubble.duration` (default `0.42` seconds). Longer durations slow inflation and the tear fronts; free sheets and droplets keep short physical lifetimes, capped by the remaining transition. Disable either direction with its existing `enabled` switch.

Bubble movement treats the window as the same fluid film the opening leaves behind. Its trailing side carries an inertial body of liquid: it lags and bows out while the window gathers speed, holds a small steady stretch while it travels, carries on past the window once when it stops or turns, and settles within about half a second. The disturbance runs around the outline as capillary waves (higher modes faster and shorter-lived), the interior pressure mode that settles the opening's landing is driven again by changes of speed, and the stretched film near the outline thins and shows interference colour while the middle of the window stays clear. Fast or turning windows grow microbubbles out of their stretched film, mostly on the trailing side; they ride the deforming outline, slide toward the trailing edge, and then detach and drift briefly, merge back, or collapse with a small tear. Above a walking pace the trailing edge also leaves a thin ribbon of film on the screen along the window's path; it drains through interference colours, tears open into holes and retracts within a second, carrying any microbubbles that detached into it. Movement continues from the end of an opening without a reset, and a close takes over whatever deformation the film still has. Once every disturbance has died out the window is drawn natively again.

`window.movement.options.bubble.strength` (default `0.65`) scales the deformation; `0` moves bubble windows undeformed. `microbubbles` (default `0.5`) sets how readily microbubbles form and `trail` (default `0.5`) how much film is left behind; `0` turns either off. `window.movement.enabled` switches bubble movement off entirely.

Droplets thrown toward you can land on the screen glass: each burst leaves at most four tiny beads where its droplets actually hit, which wet the glass briefly and then shrink away within about `0.7` seconds. They are separate from the window: they stay at their screen positions when it moves, changes workspace or closes. Movement wakes are screen residue of the same kind: at most twelve exist at once (older ones dissolve faster to make room), each holds at most 48 ribbon points and 12 free microbubbles, and each draws as one triangle strip plus one instanced draw into a layer cropped to its bounds; the regions they leave are damaged as they retract. Residue renders above windows but below native panels and security overlays, uses no capture or refraction of the underlying scene, and its small cropped GPU layers reuse the existing work-buffer pool. Disabling the effect, changing monitor layout or unloading the plugin clears it.

The film colour comes from a spectral thin-film table (a soap film of index 1.33 in air, integrated over the visible spectrum with CIE colour matching) built once and looked up by local film thickness and viewing angle; the thickness field drains, swirls and thins as the surface inflates, deforms and ruptures, and a white studio environment supplies the reflections. The unrolled membranes share one full-screen GPU pass; each rupturing membrane adds an instanced strip pass for its curled film, with transported imagery and turned-over normals. Up to seven detached film sectors reuse that strip grid, while droplets use one instanced draw. Curl geometry, detached sheets and droplets are deterministic functions of time, with no per-frame mesh allocation. Opening takes one cropped, GPU-only snapshot after Hyprland draws the wallpaper/background layers but before panels and windows. That capture frame redraws the background even where opaque windows cover it; subsequent animation frames reuse the snapshot without CPU readback or scene copying. Animated wallpaper is frozen for that brief opening. Where Hyprland does not draw wallpaper (such as XP mode), the bubble remains transparent film until window content arrives rather than sampling stale windows. Separate read-only/screen-capture render contexts do not reuse a private monitor snapshot.

Captured borders, rounded corners and alpha resolve back to their native appearance. Independently rendered third-party panes are not part of the captured surface. As with the other retained styles, texture-blurred close snapshots use the native fadeout path; this restriction does not disable ordinary background blur. Wallpaper snapshots are released on settlement, cancellation or completion of an interrupted pop. The shared shader is released on plugin unload; per-close window snapshots are released when the pop finishes.

For an isolated preview, run `tests/nested/run.sh --style bubble`; use `--lua 'window = { open = { options = { bubble = { duration = 2 } } }, close = { options = { bubble = { duration = 1 } } } }'` to inspect the phases slowly, and `--lua 'window = { movement = { options = { bubble = { microbubbles = 1, trail = 1 } } } }'` to exaggerate the movement's secondary effects while dragging with ALT.

Active window animations continue to render until they settle, including after scrolling-layout focus changes; no mouse movement is needed to complete them.

<!-- BEGIN GENERATED CONFIGURATION -->
After the first load has registered the settings, add this Lua configuration:

```lua
hl.config({
    decoration = { wobble = { enabled = false } },
    plugin = {
        ["hyprimate"] = {
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
                        bubble = {
                            strength = 0.65,
                            microbubbles = 0.5,
                            trail = 0.5,
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
                        bubble = {
                            duration = 0.62,
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
                        bubble = {
                            duration = 0.42,
                        },
                    },
                },
            },
        },
    },
})
```

Use `hyprimate` under `plugin` in `hl.config`; the matching `hyprctl` keys begin with `plugin:hyprimate:`. Each nested Lua table maps to colon-separated segments of the key, for example `window.movement.options.liquid.viscosity` is `plugin:hyprimate:window:movement:options:liquid:viscosity`.

| Setting | Type / default | Meaning |
|---|---|---|
| `plugin:hyprimate:window:enabled` | boolean / `true` | Master switch: when false, no movement, open or close deformation runs. |
| `plugin:hyprimate:window:style` | string / `"liquid"` | Material for movement, open and close: liquid deforms edges with a viscous field, paper crumples into a ball, cloth drapes and flutters, swarm breaks the window into creature-shaped pieces, bubble inflates a soap film into the window, sloshes it as a fluid film while it moves and ruptures it on close. Paper windows move undeformed. Invalid names are rejected. Allowed: `liquid`, `paper`, `cloth`, `swarm`, `bubble`. |
| `plugin:hyprimate:window:options:swarm:creature` | string / `"spider"` | Swarm-only creature every window piece is shaped as. Allowed: `spider`, `cat`. |
| `plugin:hyprimate:window:options:swarm:count` | int / `250` | Swarm-only number of creatures a window breaks into; each carries one piece of the window, so fewer creatures are larger. Clamped to 1–400. |
| `plugin:hyprimate:window:options:swarm:speed` | float / `1400` | Swarm-only creature cruise speed in logical pixels per second. Finite values clamped to 100–6000. |
| `plugin:hyprimate:window:options:swarm:stagger` | float / `0.6` | Swarm-only seconds over which creatures reach their places in random order when a window opens, or let go in random order when it closes, and half that when movement shakes them loose; zero lands or releases them all together. Finite values clamped to 0–3. |
| `plugin:hyprimate:window:options:swarm:cohesion` | float / `0.5` | Swarm-only pull that keeps nearby creatures travelling together; zero lets each find its own way. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:swarm:separation` | float / `0.5` | Swarm-only personal space while travelling: creatures step around those in their way and bump off each other instead of passing through; zero lets them cross freely. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:swarm:responsiveness` | float / `0.5` | Swarm-only acceleration and grip: higher holds formation through faster window movement and turns more sharply; lower breaks apart sooner and trails further. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:swarm:settling` | float / `0.5` | Swarm-only eagerness to lock back into the window: higher regroups and merges sooner; lower mills about longer first. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:swarm:wander` | float / `0.5` | Swarm-only meander and hesitation while travelling; zero walks steadily along its route. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:swarm:variation` | float / `0.5` | Swarm-only per-creature difference in speed, grip and how far its route curves; zero sends every creature straight and alike. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:enabled` | boolean / `true` | Deform windows while they move. Does not enable or disable open or close. |
| `plugin:hyprimate:window:movement:options:liquid:strength` | float / `0.65` | Liquid-only movement deformation intensity; zero moves windows undeformed. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:movement:options:liquid:viscosity` | float / `0.75` | Liquid-only internal flow resistance; higher is thicker and more cohesive. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:liquid:elasticity` | float / `0.5` | Liquid-only movement stiffness and recovery speed; higher snaps back faster. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:liquid:momentum` | float / `0.5` | Liquid-only movement inertia after a stop; zero lands without overshoot. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:liquid:variation` | float / `0.5` | Liquid-only per-window and per-movement randomness; zero moves windows identically. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:cloth:strength` | float / `0.65` | Cloth-only movement deformation intensity; zero moves windows undeformed. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:movement:options:cloth:springiness` | float / `0` | Horizontal springiness of the curtain header; zero keeps the top stiff, higher values allow more gathering and rebound. The lower curtain stays free-flowing. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:cloth:flutter` | float / `0.5` | Cloth-only movement ripple strength; zero removes the varying gusts while motion still bends the sheet. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:cloth:shading` | float / `0.7` | Cloth-only light and shadow on movement folds; zero draws the folds unlit. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:cloth:shine` | float / `0` | Cloth-only highlight strength on the folds; zero removes highlights. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:cloth:variation` | float / `0.5` | Cloth-only per-window movement variation; zero uses the same motion for every window. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:bubble:strength` | float / `0.65` | Bubble-only movement deformation: how far the trailing film lags, ripples and overshoots; zero moves windows undeformed. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:movement:options:bubble:microbubbles` | float / `0.5` | Bubble-only rate at which small soap bubbles form on a moving window's stretched film, slide toward its trailing edge and detach; zero forms none. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:bubble:trail` | float / `0.5` | Bubble-only amount of thin soap film a fast-moving window leaves behind on the screen before it drains and breaks up; zero leaves none. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:enabled` | boolean / `true` | Animate windows as they open (see style); false opens without it. Movement is unaffected. |
| `plugin:hyprimate:window:open:options:liquid:strength` | float / `0.65` | Liquid-only pop-out size and deformation intensity; zero opens without the pop-out. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:open:options:liquid:color` | color / `0xff000000` | Liquid-only edge liquid colour (ARGB); alpha zero draws no liquid. |
| `plugin:hyprimate:window:open:options:liquid:first_window_side` | string / `"bottom"` | Edge the first window on a workspace pops out of; later windows use the side they appear on (see Edge choice). Allowed: `bottom`, `top`, `left`, `right`. |
| `plugin:hyprimate:window:open:options:liquid:edge_bounds` | string / `"screen"` | Use monitor edges (screen) or edges inside reserved areas (work_area) for the pop-out. Allowed: `screen`, `work_area`. |
| `plugin:hyprimate:window:open:options:liquid:emerge_drag` | float / `0.5` | Share of pop-out travel fed to the movement field as drag; zero is a rigid pop. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:liquid:elasticity` | float / `0.5` | Open-only stiffness and recovery speed of the pop-out; higher snaps back faster. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:liquid:momentum` | float / `0.5` | Open-only inertia of the pop-out; zero lands without overshoot. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:paper:duration` | float / `0.6` | Paper-only seconds for the ball to uncrumple into the window. Finite values clamped to 0.1–3. |
| `plugin:hyprimate:window:open:options:paper:crumpliness` | float / `0.6` | Paper-only crumple tightness; higher crumples into a smaller, more creased ball. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:paper:shading` | float / `0.7` | Paper-only light and shadow on the creases; zero draws the folds unlit. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:paper:variation` | float / `0.5` | Paper-only per-window randomness of which crumple plays and its mirroring; zero crumples every window identically. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:cloth:duration` | float / `1.0` | Cloth-only seconds for the gathered curtain panel to spread from its top edge and settle flat. Finite values clamped to 0.1–4. |
| `plugin:hyprimate:window:open:options:cloth:flutter` | float / `0.5` | Cloth-only gust strength; zero removes flutter while gravity still moves the sheet. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:cloth:shading` | float / `0.7` | Cloth-only light and shadow on the folds; zero draws the folds unlit. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:cloth:shine` | float / `0` | Cloth-only highlight strength on the folds; zero removes highlights. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:cloth:variation` | float / `0.5` | Cloth-only per-window variation; zero uses the same motion for every window. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:swarm:lone_window_sides` | set / `"all"` | Swarm-only screen sides the creatures enter from when the window is the first on its workspace; later windows enter from the side they appear on (see Edge choice). Comma-separated list of: `top`, `bottom`, `left`, `right`; `all` selects every one and overrides the rest. |
| `plugin:hyprimate:window:open:options:bubble:duration` | float / `0.62` | Bubble-only seconds for inflation, the outer film's rupture, flattening, splat and elastic settlement. Finite values clamped to 0.1–3. |
| `plugin:hyprimate:window:close:enabled` | boolean / `true` | Animate windows as they close (see style); false closes natively. Movement is unaffected. |
| `plugin:hyprimate:window:close:options:liquid:strength` | float / `0.65` | Liquid-only drain deformation intensity; zero closes natively. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:close:options:liquid:color` | color / `0xff000000` | Liquid-only edge liquid colour (ARGB); alpha zero draws no liquid. |
| `plugin:hyprimate:window:close:options:liquid:side` | string / `"auto"` | Shared liquid/cloth exit: side of appearance (auto), nearest valid edge, opening origin, or a fixed edge. Invalid preferred edges fall back to the normal resolver. Allowed: `auto`, `nearest`, `origin`, `top`, `bottom`, `left`, `right`. |
| `plugin:hyprimate:window:close:options:liquid:edge_bounds` | string / `"screen"` | Shared liquid/cloth edge choice: monitor edges (screen) or edges inside reserved areas (work_area). Cloth travels beyond the actual screen before disappearing. Allowed: `screen`, `work_area`. |
| `plugin:hyprimate:window:close:options:liquid:splash` | boolean / `true` | Small edge-liquid splash where a closing window disappears; false closes without liquid. |
| `plugin:hyprimate:window:close:options:liquid:elasticity` | float / `0.5` | Close-only stiffness and recovery speed of the drain; higher snaps back faster. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:liquid:momentum` | float / `0.5` | Close-only dive acceleration; higher advances the dive faster. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:paper:duration` | float / `0.5` | Paper-only seconds to crumple the window into a ball and vanish. Finite values clamped to 0.1–3. |
| `plugin:hyprimate:window:close:options:paper:crumpliness` | float / `0.6` | Paper-only crumple tightness; higher crumples into a smaller, more creased ball. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:paper:shading` | float / `0.7` | Paper-only light and shadow on the creases; zero draws the folds unlit. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:paper:variation` | float / `0.5` | Paper-only per-window randomness of which crumple plays and its mirroring; zero crumples every window identically. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:cloth:duration` | float / `0.9` | Cloth-only seconds for the top rod to carry the curtain beyond the selected screen edge. Finite values clamped to 0.1–4. |
| `plugin:hyprimate:window:close:options:cloth:flutter` | float / `0.5` | Cloth-only gust strength; zero removes flutter while gravity still moves the sheet. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:cloth:shading` | float / `0.7` | Cloth-only light and shadow on the folds; zero draws the folds unlit. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:cloth:shine` | float / `0` | Cloth-only highlight strength on the folds; zero removes highlights. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:cloth:variation` | float / `0.5` | Cloth-only per-window variation; zero uses the same motion for every window. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:swarm:lone_window_sides` | set / `"all"` | Swarm-only screen sides the creatures leave through when the window is the last on its workspace; otherwise they leave through the side the window sits on (see Edge choice). Comma-separated list of: `top`, `bottom`, `left`, `right`; `all` selects every one and overrides the rest. |
| `plugin:hyprimate:window:close:options:bubble:duration` | float / `0.42` | Bubble-only seconds for the film to thin, tear open, retract into ligaments and droplets, and for the droplets to evaporate. Finite values clamped to 0.1–3. |
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
