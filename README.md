# hyprimate

hyprimate is an animation extension for [Hyprland](https://hyprland.org/) that adds custom animations to your desktop. It began as an attempt to capture the fluid motion of Apple's Liquid Glass animations, and is intended to grow with more animation styles over time.

Currently, hyprimate animates windows as they move, open, and close:

- **Liquid** gives moving windows a fluid, elastic feel and animates them in and out from the edge of the screen.
- **Wiggle** is an alternative movement style that emerged while experimenting with Liquid. It brought to mind that old-school wiggly window animation from years ago—if you know, you know.

[Watch the demo](hyprimate-demo-closing.mp4)

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

Hyprland 0.57 has a built-in **wobble** effect for moving and resizing windows, separate from hyprimate's **wiggle** style. Its `decoration.wobble.enabled` setting defaults to `false`. If you enabled it, disable it before loading hyprimate: the two window-deformation effects conflict, and the plugin will refuse to load while both are enabled. You do not need to disable Hyprland's regular window or workspace animations.

The example below explicitly disables Hyprland's wobble and shows all hyprimate settings with their defaults. Add the plugin settings to your Hyprland Lua configuration after the plugin has been loaded.

The movement, open, and close animations can be enabled and configured independently. To use the old-school movement effect, set `window.movement.style` to `"wiggle"`; open and close retain their own settings.

<!-- BEGIN GENERATED CONFIGURATION -->
After the first load has registered the settings, add this Lua configuration:

```lua
hl.config({
    decoration = { wobble = { enabled = false } },
    plugin = {
        ["liquid_glass"] = {
            window = {
                enabled = true,
                strength = 0.65,
                liquid_color = 0xff000000,
                movement = {
                    enabled = true,
                    style = "liquid",
                    options = {
                        liquid = {
                            viscosity = 0.75,
                            elasticity = 0.5,
                            momentum = 0.5,
                            variation = 0.5,
                        },
                        wiggle = {},
                    },
                },
                open = {
                    enabled = true,
                    style = "liquid",
                    options = {
                        liquid = {
                            first_window_side = "bottom",
                            edge_bounds = "screen",
                            emerge_drag = 0.5,
                            elasticity = 0.5,
                            momentum = 0.5,
                        },
                    },
                },
                close = {
                    enabled = true,
                    style = "liquid",
                    options = {
                        liquid = {
                            side = "auto",
                            edge_bounds = "screen",
                            splash = true,
                            elasticity = 0.5,
                            momentum = 0.5,
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
| `plugin:liquid-glass:window:strength` | float / `0.65` | Deformation intensity; zero is undeformed. Finite values clamped to 0–1.5. |
| `plugin:liquid-glass:window:liquid_color` | color / `0xff000000` | Edge liquid colour (ARGB) for open and close; alpha zero draws no liquid. |
| `plugin:liquid-glass:window:movement:enabled` | boolean / `true` | Deform windows while they move. Does not enable or disable open or close. |
| `plugin:liquid-glass:window:movement:style` | string / `"liquid"` | Movement material; invalid names are rejected. Allowed: `liquid`, `wiggle`. |
| `plugin:liquid-glass:window:movement:options:liquid:viscosity` | float / `0.75` | Liquid-only internal flow resistance; higher is thicker and more cohesive. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:liquid:elasticity` | float / `0.5` | Liquid-only movement stiffness and recovery speed; higher snaps back faster. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:liquid:momentum` | float / `0.5` | Liquid-only movement inertia after a stop; zero lands without overshoot. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:movement:options:liquid:variation` | float / `0.5` | Liquid-only per-window and per-movement randomness; zero moves windows identically. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:enabled` | boolean / `true` | Edge pop-out when a window opens; false opens without it. Movement is unaffected. |
| `plugin:liquid-glass:window:open:style` | string / `"liquid"` | Open material; invalid names are rejected. Allowed: `liquid`. |
| `plugin:liquid-glass:window:open:options:liquid:first_window_side` | string / `"bottom"` | Edge the first window on a workspace pops out of; later windows use the side they appear on (see Edge choice). Allowed: `bottom`, `top`, `left`, `right`. |
| `plugin:liquid-glass:window:open:options:liquid:edge_bounds` | string / `"screen"` | Use monitor edges (screen) or edges inside reserved areas (work_area) for the pop-out. Allowed: `screen`, `work_area`. |
| `plugin:liquid-glass:window:open:options:liquid:emerge_drag` | float / `0.5` | Share of pop-out travel fed to the movement field as drag; zero is a rigid pop. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:liquid:elasticity` | float / `0.5` | Open-only stiffness and recovery speed of the pop-out; higher snaps back faster. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:open:options:liquid:momentum` | float / `0.5` | Open-only inertia of the pop-out; zero lands without overshoot. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:enabled` | boolean / `true` | Edge drain when a window closes; false closes natively. Movement is unaffected. |
| `plugin:liquid-glass:window:close:style` | string / `"liquid"` | Close material; invalid names are rejected. Allowed: `liquid`. |
| `plugin:liquid-glass:window:close:options:liquid:side` | string / `"auto"` | Drain by side of appearance (auto), nearest valid edge, or opening edge (origin, falling back to auto). Allowed: `auto`, `nearest`, `origin`. |
| `plugin:liquid-glass:window:close:options:liquid:edge_bounds` | string / `"screen"` | Use monitor edges (screen) or edges inside reserved areas (work_area) for the drain. Allowed: `screen`, `work_area`. |
| `plugin:liquid-glass:window:close:options:liquid:splash` | boolean / `true` | Small edge-liquid splash where a closing window disappears; false closes without liquid. |
| `plugin:liquid-glass:window:close:options:liquid:elasticity` | float / `0.5` | Close-only stiffness and recovery speed of the drain; higher snaps back faster. Finite values clamped to 0–1. |
| `plugin:liquid-glass:window:close:options:liquid:momentum` | float / `0.5` | Close-only dive acceleration; higher advances the dive faster. Finite values clamped to 0–1. |

Empty Lua tables in the layout (no settings registered):

- `window.movement.options.wiggle`: Wiggle movement has no options; this empty table is part of the layout and registers no setting.
<!-- END GENERATED CONFIGURATION -->

### Edge choice

The first window on a workspace opens from `first_window_side`. Later tiled windows normally open from the screen edge on the side where they appear; floating windows and windows without a clear neighboring window use the nearest valid edge. Set `edge_bounds` to `"work_area"` to use edges inside reserved areas instead of the screen edges.

For closing windows, `side = "auto"` chooses the side of appearance, `"nearest"` uses the nearest valid edge, and `"origin"` uses the opening edge when available.
