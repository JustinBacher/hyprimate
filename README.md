# hyprimate

Hyprimate is a [Hyprland](https://hyprland.org/) plugin that animates windows as they open, move, and close. Choose a style for your windows:

- **Liquid** makes windows flow and stretch.
- **Cloth** drapes and folds windows like a curtain.
- **Paper** crumples windows as they open and close; movement remains undeformed.
- **Swarm** transforms windows into creatures that scatter and regroup.
- **Bubble** gives windows a soap-film surface that inflates, moves, and bursts.

## Demo

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

## Configuration

Hyprland's built-in wobble conflicts with Hyprimate. If enabled, disable `decoration.wobble.enabled` before loading the plugin. Hyprland's regular window and workspace animations can remain enabled.

The example below shows the default settings and disables wobble. After loading the plugin for the first time, add the settings to your Hyprland Lua configuration. Set `window.style` to choose an animation style; movement, opening, and closing can be enabled separately. Style options under `window.options` apply globally; an option under `window.open.options`, `window.close.options`, or `window.movement.options` overrides it for that animation.

Swarm's former `creature` and `responsiveness` settings are now named `sprite` and `agility`; update both global settings and any per-animation overrides.

During a mouse move gesture, swarm agents keep wandering even when the pointer pauses. `wander` controls this motion; zero retains formation when stationary. Releasing the gesture lets the agents settle back into the window.

For example, to set a shared bubble duration while giving closing its own duration:

```lua
hl.config({ plugin = { hyprimate = { window = {
    options = { bubble = { duration = 0.8 } },
    close = { options = { bubble = { duration = 0.3 } } },
} } } })
```

<!-- BEGIN GENERATED CONFIGURATION -->
After the first load has registered the settings, add this Lua configuration:

```lua
hl.config({
    decoration = { wobble = { enabled = false } },
    plugin = {
        ["hyprimate"] = {
            window = {
                options = {
                    liquid = {
                        strength = 0.65,
                        viscosity = 0.75,
                        elasticity = 0.5,
                        momentum = 0.5,
                        variation = 0.5,
                        color = 0xff000000,
                        default_animation_edge = "bottom",
                        edge_bounds = "screen",
                        emerge_drag = 0.5,
                        side = "auto",
                        splash = true,
                    },
                    cloth = {
                        strength = 0.65,
                        springiness = 0,
                        gravity = 1,
                        flutter = 0.5,
                        shading = 0.7,
                        shine = 0,
                        variation = 0.5,
                        -- duration = 1.0,
                        default_animation_edge = "bottom",
                        edge_bounds = "screen",
                        side = "auto",
                    },
                    bubble = {
                        strength = 0.65,
                        microbubbles = 0.5,
                        trail = 0.5,
                        shape = "pillow",
                        roundness = 1,
                        shape_speed = 1200,
                        -- duration = 0.62,
                    },
                    paper = {
                        -- duration = 0.6,
                        crumpliness = 0.6,
                        shading = 0.7,
                        variation = 0.5,
                    },
                    swarm = {
                        lone_window_sides = "all",
                        sprite = "spider",
                        count = 250,
                        speed = 1400,
                        stagger = 0.6,
                        cohesion = 0.5,
                        separation = 0.5,
                        agility = 0.5,
                        settling = 0.5,
                        wander = 0.5,
                        variation = 0.5,
                    },
                },
                enabled = true,
                style = "liquid",
                movement = {
                    enabled = true,
                    workspace_movement = true,
                },
                open = {
                    enabled = true,
                },
                close = {
                    enabled = true,
                },
            },
        },
    },
})
```

Use `hyprimate` under `plugin` in `hl.config`; the matching `hyprctl` keys begin with `plugin:hyprimate:`. Each nested Lua table maps to colon-separated key segments.

Style settings under `window.options.<style>` are shared; set `window.<animation>.options.<style>` to override one animation. When a shared setting is unset, each animation uses its own default. Shared values are clamped to the receiving animation’s range. Example lines with differing animation defaults are commented out to preserve them.

| Setting | Type / default | Numeric range | Meaning |
|---|---|---|---|
| `window:options:liquid:strength` | float / animation defaults: `0.65` | 0–1.5 | Movement: Liquid movement deformation strength. Zero moves windows without deformation. Open: Liquid pop-out size and deformation strength when opening. Zero removes the pop-out. Close: Liquid drain deformation strength when closing. Zero closes the window natively. |
| `window:options:liquid:viscosity` | float / animation defaults: `0.75` | 0–1 | Liquid resistance to flow during movement. Higher values make it thicker and more cohesive. |
| `window:options:liquid:elasticity` | float / animation defaults: `0.5` | 0–1 | Movement: Liquid stiffness and recovery speed during movement. Higher values snap back faster. Open: Liquid pop-out stiffness and recovery speed. Higher values snap back faster. Close: Liquid drain stiffness and recovery speed. Higher values snap back faster. |
| `window:options:liquid:momentum` | float / animation defaults: `0.5` | 0–1 | Movement: Liquid inertia after movement stops. Zero settles without overshoot. Open: Liquid pop-out inertia. Zero settles without overshoot. Close: Liquid closing dive acceleration. Higher values make the dive advance faster. |
| `window:options:liquid:variation` | float / animation defaults: `0.5` | 0–1 | Random variation between windows and movements. Zero makes liquid movement identical. |
| `window:options:cloth:strength` | float / animation defaults: `0.65` | 0–1.5 | Cloth movement deformation strength. Zero moves windows without deformation. |
| `window:options:cloth:springiness` | float / animation defaults: `0` | 0–1 | Horizontal springiness of the curtain's top edge during movement. Zero keeps it stiff; higher values allow more gathering and rebound. The lower curtain remains free-flowing. |
| `window:options:cloth:gravity` | float / animation defaults: `1` | 0–3 | Cloth movement gravity multiplier. One is normal gravity; zero disables gravity. Lower values let fabric lift more easily. Opening and closing are unaffected. |
| `window:options:cloth:flutter` | float / animation defaults: `0.5` | 0–1 | Movement: Cloth movement ripple strength. Zero removes varying gusts, but movement still bends the sheet. Open: Cloth gust strength when opening. Zero removes flutter, but gravity still moves the sheet. Close: Cloth gust strength when closing. Zero removes flutter, but gravity still moves the sheet. |
| `window:options:cloth:shading` | float / animation defaults: `0.7` | 0–1 | Movement: Light and shadow on cloth movement folds. Zero draws them unlit. Open: Light and shadow on cloth folds when opening. Zero draws them unlit. Close: Light and shadow on cloth folds when closing. Zero draws them unlit. |
| `window:options:cloth:shine` | float / animation defaults: `0` | 0–1 | Movement: Highlight strength on cloth movement folds. Zero removes highlights. Open: Highlight strength on cloth folds when opening. Zero removes highlights. Close: Highlight strength on cloth folds when closing. Zero removes highlights. |
| `window:options:cloth:variation` | float / animation defaults: `0.5` | 0–1 | Movement: Variation in cloth movement between windows. Zero gives every window the same motion. Open: Variation in cloth opening motion between windows. Zero gives every window the same motion. Close: Variation in cloth closing motion between windows. Zero gives every window the same motion. |
| `window:options:bubble:strength` | float / animation defaults: `0.65` | 0–1.5 | How much the trailing bubble film lags, ripples and overshoots during movement. Zero moves windows without deformation. |
| `window:options:bubble:microbubbles` | float / animation defaults: `0.5` | 0–1 | Rate at which small bubbles form on stretched film, slide to the trailing edge and detach. Zero forms none. |
| `window:options:bubble:trail` | float / animation defaults: `0.5` | 0–1 | Amount of thin soap film a fast-moving window leaves behind before it drains and breaks up. Zero leaves none. |
| `window:options:bubble:shape` | string / animation defaults: `"pillow"` | — | Shape surface tension pulls a moving bubble toward. Pillow rounds the corners and gently bulges the sides; ellipse keeps the window's proportions; circle makes a disc regardless of its proportions. Invalid names are rejected. Allowed: `pillow`, `ellipse`, `circle`. |
| `window:options:bubble:roundness` | float / animation defaults: `1` | 0–1 | How far a window moving at shape_speed contracts toward its bubble shape before returning to a rectangle when stopped. Zero keeps it rectangular. |
| `window:options:bubble:shape_speed` | float / animation defaults: `1200` | 100–6000 | Window speed in logical pixels per second at which bubble shape contraction reaches its full amount. Slower windows contract proportionally less. |
| `window:options:liquid:color` | color / animation defaults: `0xff000000` | — | Open: Colour of the liquid at the opening edge, in ARGB. Zero alpha draws no liquid. Close: Colour of the liquid at the closing edge, in ARGB. Zero alpha draws no liquid. |
| `window:options:liquid:default_animation_edge` | string / animation defaults: `"bottom"` | — | Open: Preferred opening edge for the first window on a workspace. Later windows use their placement. Invalid edges fall back to a valid edge. Close: Preferred closing edge for the last window on a workspace when side is auto. Other windows use their placement. Invalid edges fall back to a valid edge. Allowed: `bottom`, `top`, `left`, `right`. |
| `window:options:liquid:edge_bounds` | string / animation defaults: `"screen"` | — | Open: Choose monitor edges (screen) or edges inside reserved areas (work_area) for the liquid pop-out. Close: Choose monitor edges (screen) or edges inside reserved areas (work_area) for the liquid exit. Allowed: `screen`, `work_area`. |
| `window:options:liquid:emerge_drag` | float / animation defaults: `0.5` | 0–1 | Share of liquid pop-out travel applied as drag to the movement field. Zero makes the pop rigid. |
| `window:options:paper:duration` | float / animation defaults: open `0.6`, close `0.5` | 0.1–3 | Open: Time in seconds for the paper ball to uncrumple into the window. Close: Time in seconds for the window to crumple into a paper ball and vanish. |
| `window:options:paper:crumpliness` | float / animation defaults: `0.6` | 0–1 | Open: Paper crumple tightness when opening. Higher values make a smaller, more creased ball. Close: Paper crumple tightness when closing. Higher values make a smaller, more creased ball. |
| `window:options:paper:shading` | float / animation defaults: `0.7` | 0–1 | Open: Light and shadow on paper creases when opening. Zero draws the folds unlit. Close: Light and shadow on paper creases when closing. Zero draws the folds unlit. |
| `window:options:paper:variation` | float / animation defaults: `0.5` | 0–1 | Open: Random choice and mirroring of the opening paper crumple for each window. Zero uses the same crumple for every window. Close: Random choice and mirroring of the closing paper crumple for each window. Zero uses the same crumple for every window. |
| `window:options:cloth:duration` | float / animation defaults: open `1.0`, close `0.9` | 0.1–4 | Open: Time in seconds for the gathered curtain to spread from its top edge and settle flat. Close: Time in seconds for the top rod to carry the curtain beyond the selected screen edge. |
| `window:options:cloth:default_animation_edge` | string / animation defaults: `"bottom"` | — | Open: Opening origin for the first cloth window on a workspace, used when the closing side is origin. Later windows use their placement. Cloth still opens in place. Close: Preferred cloth closing edge for the last window on a workspace when side is auto. Invalid edges fall back to a valid edge. Allowed: `bottom`, `top`, `left`, `right`. |
| `window:options:cloth:edge_bounds` | string / animation defaults: `"screen"` | — | Open: Choose monitor edges (screen) or edges inside reserved areas (work_area) for the cloth opening origin. Close: Choose monitor edges (screen) or edges inside reserved areas (work_area) for cloth edge selection. Cloth travels beyond the actual screen before disappearing. Allowed: `screen`, `work_area`. |
| `window:options:swarm:lone_window_sides` | set / animation defaults: `"all"` | — | Open: Screen sides swarm creatures enter from for the first window on a workspace. Later windows use the side where they appear (see Edge choice). Close: Screen sides swarm creatures leave through for the last window on a workspace. Otherwise they use the side where the window sits (see Edge choice). Comma-separated list of: `top`, `bottom`, `left`, `right`; `all` selects every one and overrides the rest. |
| `window:options:bubble:duration` | float / animation defaults: open `0.62`, close `0.42` | 0.1–3 | Open: Time in seconds for bubble inflation, outer film rupture, flattening, splat and elastic settling. Close: Time in seconds for the bubble film to thin, tear, retract into strands and droplets, and for the droplets to evaporate. |
| `window:options:liquid:side` | string / animation defaults: `"auto"` | — | Liquid exit edge: the side of appearance (auto), nearest valid edge, opening origin, or a fixed edge. Invalid preferred edges fall back to the normal edge selection. Allowed: `auto`, `nearest`, `origin`, `top`, `bottom`, `left`, `right`. |
| `window:options:liquid:splash` | boolean / animation defaults: `true` | — | Show a small edge-liquid splash where a window closes. When false, it closes without liquid. |
| `window:options:cloth:side` | string / animation defaults: `"auto"` | — | Cloth exit edge: the side of appearance (auto), nearest valid edge, opening origin, or a fixed edge. Invalid preferred edges fall back to the normal edge selection. Allowed: `auto`, `nearest`, `origin`, `top`, `bottom`, `left`, `right`. |
| `window:enabled` | boolean / `true` | — | Enable window movement, opening and closing effects. When false, none of these effects run. |
| `window:style` | string / `"liquid"` | — | Choose the material for movement, opening and closing. Liquid deforms the edges; paper crumples into a ball and moves without deformation; cloth drapes and flutters; swarm breaks the window into creature-shaped pieces; bubble inflates a soap film, sloshes while moving and bursts on close. Invalid names are rejected. Allowed: `liquid`, `paper`, `cloth`, `swarm`, `bubble`. |
| `window:options:swarm:sprite` | string / `"spider"` | — | Swarm sprite name in the sprites directory, with or without .png, or an explicit PNG path. The sheet has four columns and two rows of frames; transparency defines the silhouette. |
| `window:options:swarm:count` | int / `250` | 1–400 | Number of swarm creatures per window. Each carries a piece of the window, so fewer creatures mean larger pieces. |
| `window:options:swarm:speed` | float / `1400` | 100–6000 | Swarm creature travel speed in logical pixels per second. |
| `window:options:swarm:stagger` | float / `0.6` | 0–3 | Time in seconds over which swarm creatures arrive or leave in random order when a window opens or closes. Movement releases them over half this time. Zero makes them arrive or leave together. |
| `window:options:swarm:cohesion` | float / `0.5` | 0–1 | How strongly nearby swarm creatures travel together. Zero lets each find its own way. |
| `window:options:swarm:separation` | float / `0.5` | 0–1 | How much swarm creatures avoid and bump off each other while travelling. Zero lets them pass through each other. |
| `window:options:swarm:agility` | float / `0.5` | 0–1 | Swarm acceleration and grip. Higher values hold formation during faster movement and turn more sharply; lower values break apart sooner and trail farther. |
| `window:options:swarm:settling` | float / `0.5` | 0–1 | How quickly swarm creatures regroup and merge back into the window. Lower values make them mill about longer. |
| `window:options:swarm:wander` | float / `0.5` | 0–1 | How much swarm creatures wander and hesitate while travelling. Zero makes them follow their routes steadily. |
| `window:options:swarm:variation` | float / `0.5` | 0–1 | Variation in each swarm creature's speed, grip and route curvature. Zero makes them travel straight and alike. |
| `window:movement:enabled` | boolean / `true` | — | Deform windows while they move. This does not affect opening or closing effects. |
| `window:movement:workspace_movement` | boolean / `true` | — | Let workspace slides trigger movement effects. When false, ordinary movement, opening and closing are unaffected. |
| `window:open:enabled` | boolean / `true` | — | Animate windows as they open using the selected style. When false, windows open without this effect; movement is unaffected. |
| `window:close:enabled` | boolean / `true` | — | Animate windows as they close using the selected style. When false, windows close natively; movement is unaffected. |
<!-- END GENERATED CONFIGURATION -->

### Edge choice

The first window on a workspace opens from `first_window_side`. Later tiled windows normally open from the screen edge on the side where they appear; floating windows and windows without a clear neighboring window use the nearest valid edge. Set `edge_bounds` to `"work_area"` to use edges inside reserved areas instead of the screen edges.

Liquid and cloth share the existing `window.close.options.liquid.side` and `edge_bounds` controls. For closing windows, `side = "auto"` chooses the side of appearance, `"nearest"` uses the nearest valid edge, and `"origin"` uses the opening edge when available. Fixed `"top"`, `"bottom"`, `"left"` and `"right"` overrides prefer that edge; an invalid preferred edge falls back to the normal resolver. Cloth opens in place but records the same logical opening origin as liquid. Its exit choice can use the work area, and its travel always continues beyond the actual monitor boundary before the sheet disappears.

Swarm can use several edges at once; each creature picks one of them at random, and a random point along it. A window that is alone on its workspace (the first to open, the last to close) uses `lone_window_sides`: a comma-separated list such as `"top,left"`, or `"all"`, which overrides anything listed with it. Otherwise a tiled window uses the screen edges facing away from its neighbours, both when it opens and when it closes; a window wedged between neighbours uses its free edges, and floating windows use the nearest edge. Swarm always uses the monitor's own edges.

