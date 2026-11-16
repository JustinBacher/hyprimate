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
                enabled = true,
                style = "liquid",
                options = {
                    swarm = {
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
                movement = {
                    enabled = true,
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

Use `hyprimate` under `plugin` in `hl.config`; the matching `hyprctl` keys begin with `plugin:hyprimate:`. Each nested Lua table maps to colon-separated segments of the key, for example `window.movement.options.liquid.viscosity` is `plugin:hyprimate:window:movement:options:liquid:viscosity`.

Style settings under `window.options.<style>` apply to each animation unless its matching `window.<animation>.options.<style>` setting is explicitly set. When neither is set, each animation keeps its own default. Shared values are clamped to the receiving animation’s range. Optional settings are omitted from the example so they do not override inheritance.

| Setting | Type / default | Meaning |
|---|---|---|
| `plugin:hyprimate:window:options:liquid:strength` | float / unset | Shared liquid strength setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:options:liquid:viscosity` | float / unset | Shared liquid viscosity setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:liquid:elasticity` | float / unset | Shared liquid elasticity setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:liquid:momentum` | float / unset | Shared liquid momentum setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:liquid:variation` | float / unset | Shared liquid variation setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:cloth:strength` | float / unset | Shared cloth strength setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:options:cloth:springiness` | float / unset | Shared cloth springiness setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:cloth:flutter` | float / unset | Shared cloth flutter setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:cloth:shading` | float / unset | Shared cloth shading setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:cloth:shine` | float / unset | Shared cloth shine setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:cloth:variation` | float / unset | Shared cloth variation setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:bubble:strength` | float / unset | Shared bubble strength setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:options:bubble:microbubbles` | float / unset | Shared bubble microbubbles setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:bubble:trail` | float / unset | Shared bubble trail setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:bubble:shape` | string / unset | Shared bubble shape setting. Optional shared value; when unset, each animation uses its own default. Allowed: `pillow`, `ellipse`, `circle`. |
| `plugin:hyprimate:window:options:bubble:roundness` | float / unset | Shared bubble roundness setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:bubble:shape_speed` | float / unset | Shared bubble shape speed setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 100–6000. |
| `plugin:hyprimate:window:options:liquid:color` | color / unset | Shared liquid color setting. Optional shared value; when unset, each animation uses its own default. |
| `plugin:hyprimate:window:options:liquid:default_animation_edge` | string / unset | Shared liquid default animation edge setting. Optional shared value; when unset, each animation uses its own default. Allowed: `bottom`, `top`, `left`, `right`. |
| `plugin:hyprimate:window:options:liquid:edge_bounds` | string / unset | Shared liquid edge bounds setting. Optional shared value; when unset, each animation uses its own default. Allowed: `screen`, `work_area`. |
| `plugin:hyprimate:window:options:liquid:emerge_drag` | float / unset | Shared liquid emerge drag setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:paper:duration` | float / unset | Shared paper duration setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0.1–3. |
| `plugin:hyprimate:window:options:paper:crumpliness` | float / unset | Shared paper crumpliness setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:paper:shading` | float / unset | Shared paper shading setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:paper:variation` | float / unset | Shared paper variation setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:cloth:duration` | float / unset | Shared cloth duration setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0.1–4. |
| `plugin:hyprimate:window:options:swarm:lone_window_sides` | set / unset | Shared swarm lone window sides setting. Optional shared value; when unset, each animation uses its own default. Comma-separated list of: `top`, `bottom`, `left`, `right`; `all` selects every one and overrides the rest. |
| `plugin:hyprimate:window:options:bubble:duration` | float / unset | Shared bubble duration setting. Optional shared value; when unset, each animation uses its own default. Finite values clamped to 0.1–3. |
| `plugin:hyprimate:window:options:liquid:side` | string / unset | Shared liquid side setting. Optional shared value; when unset, each animation uses its own default. Allowed: `auto`, `nearest`, `origin`, `top`, `bottom`, `left`, `right`. |
| `plugin:hyprimate:window:options:liquid:splash` | boolean / unset | Shared liquid splash setting. Optional shared value; when unset, each animation uses its own default. |
| `plugin:hyprimate:window:enabled` | boolean / `true` | Master switch: when false, no movement, open or close deformation runs. |
| `plugin:hyprimate:window:style` | string / `"liquid"` | Material for movement, open and close: liquid deforms edges with a viscous field, paper crumples into a ball, cloth drapes and flutters, swarm breaks the window into creature-shaped pieces, bubble inflates a soap film into the window, sloshes it as a fluid film while it moves and ruptures it on close. Paper windows move undeformed. Invalid names are rejected. Allowed: `liquid`, `paper`, `cloth`, `swarm`, `bubble`. |
| `plugin:hyprimate:window:options:swarm:sprite` | string / `"spider"` | Swarm-only sprite used to draw each window piece. Allowed: `spider`, `cat`. |
| `plugin:hyprimate:window:options:swarm:count` | int / `250` | Swarm-only number of creatures a window breaks into; each carries one piece of the window, so fewer creatures are larger. Clamped to 1–400. |
| `plugin:hyprimate:window:options:swarm:speed` | float / `1400` | Swarm-only creature cruise speed in logical pixels per second. Finite values clamped to 100–6000. |
| `plugin:hyprimate:window:options:swarm:stagger` | float / `0.6` | Swarm-only seconds over which creatures reach their places in random order when a window opens, or let go in random order when it closes, and half that when movement shakes them loose; zero lands or releases them all together. Finite values clamped to 0–3. |
| `plugin:hyprimate:window:options:swarm:cohesion` | float / `0.5` | Swarm-only pull that keeps nearby creatures travelling together; zero lets each find its own way. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:swarm:separation` | float / `0.5` | Swarm-only personal space while travelling: creatures step around those in their way and bump off each other instead of passing through; zero lets them cross freely. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:swarm:agility` | float / `0.5` | Swarm-only acceleration and grip: higher holds formation through faster window movement and turns more sharply; lower breaks apart sooner and trails further. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:swarm:settling` | float / `0.5` | Swarm-only eagerness to lock back into the window: higher regroups and merges sooner; lower mills about longer first. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:swarm:wander` | float / `0.5` | Swarm-only meander and hesitation while travelling; zero walks steadily along its route. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:options:swarm:variation` | float / `0.5` | Swarm-only per-creature difference in speed, grip and how far its route curves; zero sends every creature straight and alike. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:enabled` | boolean / `true` | Deform windows while they move. Does not enable or disable open or close. |
| `plugin:hyprimate:window:movement:options:liquid:strength` | float / unset (fallback `0.65`) | Liquid-only movement deformation intensity; zero moves windows undeformed. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:movement:options:liquid:viscosity` | float / unset (fallback `0.75`) | Liquid-only internal flow resistance; higher is thicker and more cohesive. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:liquid:elasticity` | float / unset (fallback `0.5`) | Liquid-only movement stiffness and recovery speed; higher snaps back faster. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:liquid:momentum` | float / unset (fallback `0.5`) | Liquid-only movement inertia after a stop; zero lands without overshoot. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:liquid:variation` | float / unset (fallback `0.5`) | Liquid-only per-window and per-movement randomness; zero moves windows identically. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:cloth:strength` | float / unset (fallback `0.65`) | Cloth-only movement deformation intensity; zero moves windows undeformed. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:movement:options:cloth:springiness` | float / unset (fallback `0`) | Horizontal springiness of the curtain header; zero keeps the top stiff, higher values allow more gathering and rebound. The lower curtain stays free-flowing. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:cloth:flutter` | float / unset (fallback `0.5`) | Cloth-only movement ripple strength; zero removes the varying gusts while motion still bends the sheet. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:cloth:shading` | float / unset (fallback `0.7`) | Cloth-only light and shadow on movement folds; zero draws the folds unlit. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:cloth:shine` | float / unset (fallback `0`) | Cloth-only highlight strength on the folds; zero removes highlights. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:cloth:variation` | float / unset (fallback `0.5`) | Cloth-only per-window movement variation; zero uses the same motion for every window. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:bubble:strength` | float / unset (fallback `0.65`) | Bubble-only movement deformation: how far the trailing film lags, ripples and overshoots; zero moves windows undeformed. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:movement:options:bubble:microbubbles` | float / unset (fallback `0.5`) | Bubble-only rate at which small soap bubbles form on a moving window's stretched film, slide toward its trailing edge and detach; zero forms none. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:bubble:trail` | float / unset (fallback `0.5`) | Bubble-only amount of thin soap film a fast-moving window leaves behind on the screen before it drains and breaks up; zero leaves none. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:bubble:shape` | string / unset (fallback `"pillow"`) | Bubble-only shape surface tension pulls a moving window toward: pillow rounds the corners and gently bulges the sides, keeping the window recognisably rectangular; ellipse rounds it into an ellipse of the same proportions; circle into a disc whatever its proportions. Invalid names are rejected. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Allowed: `pillow`, `ellipse`, `circle`. |
| `plugin:hyprimate:window:movement:options:bubble:roundness` | float / unset (fallback `1`) | Bubble-only how far a window moving at shape_speed contracts toward its shape before spreading back into its rectangle when it stops; zero keeps the rectangle. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:bubble:shape_speed` | float / unset (fallback `1200`) | Bubble-only window speed in logical pixels per second at which the contraction toward its shape is full; slower windows contract proportionally less. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 100–6000. |
| `plugin:hyprimate:window:open:enabled` | boolean / `true` | Animate windows as they open (see style); false opens without it. Movement is unaffected. |
| `plugin:hyprimate:window:open:options:liquid:strength` | float / unset (fallback `0.65`) | Liquid-only pop-out size and deformation intensity; zero opens without the pop-out. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:open:options:liquid:color` | color / unset (fallback `0xff000000`) | Liquid-only edge liquid colour (ARGB); alpha zero draws no liquid. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. |
| `plugin:hyprimate:window:open:options:liquid:default_animation_edge` | string / unset (fallback `"bottom"`) | Preferred edge for the first window on a workspace; later windows use their placement. Invalid edges fall back to a valid edge. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Allowed: `bottom`, `top`, `left`, `right`. |
| `plugin:hyprimate:window:open:options:liquid:edge_bounds` | string / unset (fallback `"screen"`) | Use monitor edges (screen) or edges inside reserved areas (work_area) for the pop-out. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Allowed: `screen`, `work_area`. |
| `plugin:hyprimate:window:open:options:liquid:emerge_drag` | float / unset (fallback `0.5`) | Share of pop-out travel fed to the movement field as drag; zero is a rigid pop. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:liquid:elasticity` | float / unset (fallback `0.5`) | Open-only stiffness and recovery speed of the pop-out; higher snaps back faster. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:liquid:momentum` | float / unset (fallback `0.5`) | Open-only inertia of the pop-out; zero lands without overshoot. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:paper:duration` | float / unset (fallback `0.6`) | Paper-only seconds for the ball to uncrumple into the window. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0.1–3. |
| `plugin:hyprimate:window:open:options:paper:crumpliness` | float / unset (fallback `0.6`) | Paper-only crumple tightness; higher crumples into a smaller, more creased ball. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:paper:shading` | float / unset (fallback `0.7`) | Paper-only light and shadow on the creases; zero draws the folds unlit. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:paper:variation` | float / unset (fallback `0.5`) | Paper-only per-window randomness of which crumple plays and its mirroring; zero crumples every window identically. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:cloth:duration` | float / unset (fallback `1.0`) | Cloth-only seconds for the gathered curtain panel to spread from its top edge and settle flat. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0.1–4. |
| `plugin:hyprimate:window:open:options:cloth:flutter` | float / unset (fallback `0.5`) | Cloth-only gust strength; zero removes flutter while gravity still moves the sheet. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:cloth:shading` | float / unset (fallback `0.7`) | Cloth-only light and shadow on the folds; zero draws the folds unlit. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:cloth:shine` | float / unset (fallback `0`) | Cloth-only highlight strength on the folds; zero removes highlights. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:cloth:variation` | float / unset (fallback `0.5`) | Cloth-only per-window variation; zero uses the same motion for every window. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:swarm:lone_window_sides` | set / unset (fallback `"all"`) | Swarm-only screen sides the creatures enter from when the window is the first on its workspace; later windows enter from the side they appear on (see Edge choice). Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Comma-separated list of: `top`, `bottom`, `left`, `right`; `all` selects every one and overrides the rest. |
| `plugin:hyprimate:window:open:options:bubble:duration` | float / unset (fallback `0.62`) | Bubble-only seconds for inflation, the outer film's rupture, flattening, splat and elastic settlement. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0.1–3. |
| `plugin:hyprimate:window:close:enabled` | boolean / `true` | Animate windows as they close (see style); false closes natively. Movement is unaffected. |
| `plugin:hyprimate:window:close:options:liquid:strength` | float / unset (fallback `0.65`) | Liquid-only drain deformation intensity; zero closes natively. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1.5. |
| `plugin:hyprimate:window:close:options:liquid:color` | color / unset (fallback `0xff000000`) | Liquid-only edge liquid colour (ARGB); alpha zero draws no liquid. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. |
| `plugin:hyprimate:window:close:options:liquid:default_animation_edge` | string / unset (fallback `"bottom"`) | Preferred edge for the last window on a workspace when side is auto; other windows use their placement. Invalid edges fall back to a valid edge. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Allowed: `bottom`, `top`, `left`, `right`. |
| `plugin:hyprimate:window:close:options:liquid:side` | string / unset (fallback `"auto"`) | Shared liquid/cloth exit: side of appearance (auto), nearest valid edge, opening origin, or a fixed edge. Invalid preferred edges fall back to the normal resolver. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Allowed: `auto`, `nearest`, `origin`, `top`, `bottom`, `left`, `right`. |
| `plugin:hyprimate:window:close:options:liquid:edge_bounds` | string / unset (fallback `"screen"`) | Shared liquid/cloth edge choice: monitor edges (screen) or edges inside reserved areas (work_area). Cloth travels beyond the actual screen before disappearing. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Allowed: `screen`, `work_area`. |
| `plugin:hyprimate:window:close:options:liquid:splash` | boolean / unset (fallback `true`) | Small edge-liquid splash where a closing window disappears; false closes without liquid. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. |
| `plugin:hyprimate:window:close:options:liquid:elasticity` | float / unset (fallback `0.5`) | Close-only stiffness and recovery speed of the drain; higher snaps back faster. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:liquid:momentum` | float / unset (fallback `0.5`) | Close-only dive acceleration; higher advances the dive faster. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:paper:duration` | float / unset (fallback `0.5`) | Paper-only seconds to crumple the window into a ball and vanish. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0.1–3. |
| `plugin:hyprimate:window:close:options:paper:crumpliness` | float / unset (fallback `0.6`) | Paper-only crumple tightness; higher crumples into a smaller, more creased ball. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:paper:shading` | float / unset (fallback `0.7`) | Paper-only light and shadow on the creases; zero draws the folds unlit. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:paper:variation` | float / unset (fallback `0.5`) | Paper-only per-window randomness of which crumple plays and its mirroring; zero crumples every window identically. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:cloth:duration` | float / unset (fallback `0.9`) | Cloth-only seconds for the top rod to carry the curtain beyond the selected screen edge. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0.1–4. |
| `plugin:hyprimate:window:close:options:cloth:flutter` | float / unset (fallback `0.5`) | Cloth-only gust strength; zero removes flutter while gravity still moves the sheet. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:cloth:shading` | float / unset (fallback `0.7`) | Cloth-only light and shadow on the folds; zero draws the folds unlit. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:cloth:shine` | float / unset (fallback `0`) | Cloth-only highlight strength on the folds; zero removes highlights. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:cloth:variation` | float / unset (fallback `0.5`) | Cloth-only per-window variation; zero uses the same motion for every window. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:swarm:lone_window_sides` | set / unset (fallback `"all"`) | Swarm-only screen sides the creatures leave through when the window is the last on its workspace; otherwise they leave through the side the window sits on (see Edge choice). Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Comma-separated list of: `top`, `bottom`, `left`, `right`; `all` selects every one and overrides the rest. |
| `plugin:hyprimate:window:close:options:bubble:duration` | float / unset (fallback `0.42`) | Bubble-only seconds for the film to thin, tear open, retract into ligaments and droplets, and for the droplets to evaporate. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0.1–3. |
| `plugin:hyprimate:window:movement:options:swarm:sprite` | string / unset (fallback `"spider"`) | Swarm-only sprite used to draw each window piece. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Allowed: `spider`, `cat`. |
| `plugin:hyprimate:window:movement:options:swarm:count` | int / unset (fallback `250`) | Swarm-only number of creatures a window breaks into; each carries one piece of the window, so fewer creatures are larger. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Clamped to 1–400. |
| `plugin:hyprimate:window:movement:options:swarm:speed` | float / unset (fallback `1400`) | Swarm-only creature cruise speed in logical pixels per second. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 100–6000. |
| `plugin:hyprimate:window:movement:options:swarm:stagger` | float / unset (fallback `0.6`) | Swarm-only seconds over which creatures reach their places in random order when a window opens, or let go in random order when it closes, and half that when movement shakes them loose; zero lands or releases them all together. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–3. |
| `plugin:hyprimate:window:movement:options:swarm:cohesion` | float / unset (fallback `0.5`) | Swarm-only pull that keeps nearby creatures travelling together; zero lets each find its own way. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:swarm:separation` | float / unset (fallback `0.5`) | Swarm-only personal space while travelling: creatures step around those in their way and bump off each other instead of passing through; zero lets them cross freely. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:swarm:agility` | float / unset (fallback `0.5`) | Swarm-only acceleration and grip: higher holds formation through faster window movement and turns more sharply; lower breaks apart sooner and trails further. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:swarm:settling` | float / unset (fallback `0.5`) | Swarm-only eagerness to lock back into the window: higher regroups and merges sooner; lower mills about longer first. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:swarm:wander` | float / unset (fallback `0.5`) | Swarm-only meander and hesitation while travelling; zero walks steadily along its route. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:movement:options:swarm:variation` | float / unset (fallback `0.5`) | Swarm-only per-creature difference in speed, grip and how far its route curves; zero sends every creature straight and alike. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:swarm:sprite` | string / unset (fallback `"spider"`) | Swarm-only sprite used to draw each window piece. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Allowed: `spider`, `cat`. |
| `plugin:hyprimate:window:open:options:swarm:count` | int / unset (fallback `250`) | Swarm-only number of creatures a window breaks into; each carries one piece of the window, so fewer creatures are larger. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Clamped to 1–400. |
| `plugin:hyprimate:window:open:options:swarm:speed` | float / unset (fallback `1400`) | Swarm-only creature cruise speed in logical pixels per second. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 100–6000. |
| `plugin:hyprimate:window:open:options:swarm:stagger` | float / unset (fallback `0.6`) | Swarm-only seconds over which creatures reach their places in random order when a window opens, or let go in random order when it closes, and half that when movement shakes them loose; zero lands or releases them all together. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–3. |
| `plugin:hyprimate:window:open:options:swarm:cohesion` | float / unset (fallback `0.5`) | Swarm-only pull that keeps nearby creatures travelling together; zero lets each find its own way. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:swarm:separation` | float / unset (fallback `0.5`) | Swarm-only personal space while travelling: creatures step around those in their way and bump off each other instead of passing through; zero lets them cross freely. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:swarm:agility` | float / unset (fallback `0.5`) | Swarm-only acceleration and grip: higher holds formation through faster window movement and turns more sharply; lower breaks apart sooner and trails further. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:swarm:settling` | float / unset (fallback `0.5`) | Swarm-only eagerness to lock back into the window: higher regroups and merges sooner; lower mills about longer first. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:swarm:wander` | float / unset (fallback `0.5`) | Swarm-only meander and hesitation while travelling; zero walks steadily along its route. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:open:options:swarm:variation` | float / unset (fallback `0.5`) | Swarm-only per-creature difference in speed, grip and how far its route curves; zero sends every creature straight and alike. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:swarm:sprite` | string / unset (fallback `"spider"`) | Swarm-only sprite used to draw each window piece. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Allowed: `spider`, `cat`. |
| `plugin:hyprimate:window:close:options:swarm:count` | int / unset (fallback `250`) | Swarm-only number of creatures a window breaks into; each carries one piece of the window, so fewer creatures are larger. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Clamped to 1–400. |
| `plugin:hyprimate:window:close:options:swarm:speed` | float / unset (fallback `1400`) | Swarm-only creature cruise speed in logical pixels per second. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 100–6000. |
| `plugin:hyprimate:window:close:options:swarm:stagger` | float / unset (fallback `0.6`) | Swarm-only seconds over which creatures reach their places in random order when a window opens, or let go in random order when it closes, and half that when movement shakes them loose; zero lands or releases them all together. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–3. |
| `plugin:hyprimate:window:close:options:swarm:cohesion` | float / unset (fallback `0.5`) | Swarm-only pull that keeps nearby creatures travelling together; zero lets each find its own way. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:swarm:separation` | float / unset (fallback `0.5`) | Swarm-only personal space while travelling: creatures step around those in their way and bump off each other instead of passing through; zero lets them cross freely. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:swarm:agility` | float / unset (fallback `0.5`) | Swarm-only acceleration and grip: higher holds formation through faster window movement and turns more sharply; lower breaks apart sooner and trails further. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:swarm:settling` | float / unset (fallback `0.5`) | Swarm-only eagerness to lock back into the window: higher regroups and merges sooner; lower mills about longer first. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:swarm:wander` | float / unset (fallback `0.5`) | Swarm-only meander and hesitation while travelling; zero walks steadily along its route. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
| `plugin:hyprimate:window:close:options:swarm:variation` | float / unset (fallback `0.5`) | Swarm-only per-creature difference in speed, grip and how far its route curves; zero sends every creature straight and alike. Overrides the shared setting when explicitly set; otherwise inherits it, or uses this animation’s default if the shared setting is unset. Finite values clamped to 0–1. |
<!-- END GENERATED CONFIGURATION -->

### Edge choice

The first window on a workspace opens from `first_window_side`. Later tiled windows normally open from the screen edge on the side where they appear; floating windows and windows without a clear neighboring window use the nearest valid edge. Set `edge_bounds` to `"work_area"` to use edges inside reserved areas instead of the screen edges.

Liquid and cloth share the existing `window.close.options.liquid.side` and `edge_bounds` controls. For closing windows, `side = "auto"` chooses the side of appearance, `"nearest"` uses the nearest valid edge, and `"origin"` uses the opening edge when available. Fixed `"top"`, `"bottom"`, `"left"` and `"right"` overrides prefer that edge; an invalid preferred edge falls back to the normal resolver. Cloth opens in place but records the same logical opening origin as liquid. Its exit choice can use the work area, and its travel always continues beyond the actual monitor boundary before the sheet disappears.

Swarm can use several edges at once; each creature picks one of them at random, and a random point along it. A window that is alone on its workspace (the first to open, the last to close) uses `lone_window_sides`: a comma-separated list such as `"top,left"`, or `"all"`, which overrides anything listed with it. Otherwise a tiled window uses the screen edges facing away from its neighbours, both when it opens and when it closes; a window wedged between neighbours uses its free edges, and floating windows use the nearest edge. Swarm always uses the monitor's own edges.

