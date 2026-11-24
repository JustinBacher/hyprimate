# Lava workspace transitions

The whole source desktop superheats and buckles. Wallpaper, imagery and crust deform as a slab of molten rock: the slab flows **right** on a forward switch (**+x**) and **left** on a backward switch (**−x**). It cools and settles into the whole destination desktop. No advancing boundary divides an untouched source from an already-revealed destination.

Enable it under `plugin.hyprimate` in your Hyprland Lua configuration. This example gives drifting plates brighter seams and burns away the outgoing app windows:

```lua
hl.config({
    plugin = {
        ["hyprimate"] = {
            workspace = {
                enabled = true,
                style = "lava",
                options = {
                    lava = {
                        variant = "crust_plates",
                        window_behavior = "burn_up",
                        detail_preservation = 0.90,
                        crust_plates = { seam_glow = 0.80 },
                    },
                },
            },
        },
    },
})
```

`workspace.style` still defaults to `rotation`, and workspace effects remain off unless enabled. Lava does not change window animation settings. The numeric controls below range from 0 to 1. They change how the material looks, not the duration, distance or direction of a workspace switch.

Lava reads Hyprland's inherited `workspacesIn` leaf at each workspace request. Disabling that leaf or `animations.enabled` leaves navigation native. Bézier timing uses the native speed. Spring timing uses the spring's stiffness, damping and mass, ending at the first turning point or settlement so the desktop does not overshoot. Direction follows Hyprland's navigation sign, including explicit native slide orientations; placement changes without a non-instant animation callback use Hyprimate's normal direction resolver.

An uninterrupted turn coasts in one direction through cooling. Outgoing features move downstream; incoming features approach their final positions from upstream as the remaining travel shrinks to zero. A reversal requires another navigation request; cooling does not pull the slab back.

Both endpoints are opaque, complete scenes in monitor coordinates. There is no residual glow, displacement or missing window detail at either endpoint. Reversing retraces the material from its last presented position, without taking a snapshot of the molten frame. A jump connects just its two endpoint workspaces.

## Window behavior

`workspace.options.lava.window_behavior` applies to every `variant`; it does not go inside a variant subtable. It accepts two values:

- **`melt_with_workspace` (default):** windows, wallpaper and shell are one surface. The same folds, cracks and flow carry their detail together.
- **`burn_up`:** visible app-window pixels burn away from the outgoing slab; the incoming windows resolve as the destination cools. Wallpaper and shell still undergo the selected geological deformation. An opaque bar above a window completely shields that area from the window burn; a translucent bar reduces the burn in proportion to its coverage.

The burn uses the captured scene before and after the app-window pass, with the coverage of the layers above it. It follows the visible contribution of overlapping and translucent windows as a group; it neither guesses window colors nor redraws individual clients. A panel painted inside a wallpaper surface is wallpaper, not a window. Neither mode starts a window-close animation or changes any client's state.

With `burn_up`, special-workspace changes use Hyprland's native transition on all outputs. While any special workspace is visible or animating, Lava remains native on all outputs; a monitor also stays native while its special dimming or blur is active. Lava resumes after those surfaces and effects settle.

The cursor and lock screen are not Lava material. Session locks, capture-protected content (such as `no_screen_share` windows), and unsupported capture paths remain under native compositor control. Burn mode needs extra monitor-sized capture planes for each endpoint; they do not grant access to anything the normal workspace capture cannot show.

## Shared controls

Set these eight numbers directly under `workspace.options.lava`, alongside `variant` and `window_behavior`. They apply to all four variants and both window behaviors.

| Setting | Default | Effect of increasing it |
|---|---:|---|
| `viscosity` | 0.72 | Makes the flowing material look thicker and more cohesive. |
| `cooling_rate` | 0.80 | Makes the material cool earlier along the same journey. |
| `crust_amount` | 0.68 | Covers more of the exposed melt with cooled crust. |
| `glow_intensity` | 0.55 | Brightens hot regions. |
| `detail_preservation` | 0.75 | Keeps more of the original desktop detail legible. |
| `trail_smear` | 0.35 | Stretches detail into streaks behind the flow. |
| `edge_drip` | 0.15 | Makes the molten edge sag and drip more. |
| `ash_amount` | 0.08 | Adds dark ash flecks to the surface. |

## Choosing a variant

`variant` selects **one** surface model. Keep its settings in the subtable of the same name. For example, `lava.basalt_flow.fissure_width` only affects `variant = "basalt_flow"`; it does not widen the seams in `crust_plates`. You can leave the other subtables unset or keep their settings for your next switch.

- **`cooling_flow`** is a continuous, viscous sheet. Its leading side swells and its trailing side stretches as the moving material hardens.
- **`basalt_flow`** carries dense, dark crust. Fissures expose the hot material beneath it.
- **`pahoehoe`** rolls the desktop into rope-like folds. Ripples travel along the folds as the slab moves.
- **`crust_plates`** breaks the slab into drifting plates. Imagery stays more rigid on each plate; compression and separation change the molten seams between neighbors.

| Variant | Setting | Default | What it changes |
|---|---|---:|---|
| `cooling_flow` | `leading_bulge` | 0.45 | Swells the leading side of the moving melt. |
| | `trailing_stretch` | 0.40 | Pulls the rear into a longer tail. |
| | `molten_turbulence` | 0.30 | Warps detail inside the molten flow. |
| `basalt_flow` | `crust_thickness` | 0.75 | Thickens the dark basalt around fissures. |
| | `fissure_width` | 0.28 | Widens cracks through the crust. |
| | `ember_visibility` | 0.12 | Makes hot embers stand out against the rock. |
| | `mass_density` | 0.80 | Gives the basalt a denser, heavier appearance. |
| `pahoehoe` | `rope_strength` | 0.65 | Accentuates the rope-like ridges. |
| | `fold_scale` | 0.50 | Sets the size of individual folds. |
| | `ripple_speed` | 0.40 | Moves ripples further along the folds over the same transition. |
| | `surface_roll` | 0.55 | Increases the rolling deformation of the folded surface. |
| `crust_plates` (default) | `plate_size` | 0.55 | Sets the size of individual plates. |
| | `plate_rigidity` | 0.70 | Keeps imagery on each plate more rigid as it moves. |
| | `seam_glow` | 0.60 | Brightens hot seams between plates. |
| | `plate_drift` | 0.35 | Lets neighboring plates move further relative to each other. |
| | `plate_compression` | 0.25 | Presses plates more tightly together. |
| | `submelt_visibility` | 0.45 | Shows more of the molten layer beneath the plates. |

## Inspecting the renderer

With a test-enabled build and a surfaceless GLES driver, run:

```sh
cmake --build build --target lava-renderer-tests
EGL_PLATFORM=surfaceless build/lava-renderer-tests --capture /tmp/hyprimate-lava
EGL_PLATFORM=surfaceless build/lava-renderer-tests --profile
```

Capture uses two synthetic desktops, not a live compositor session. The profile measures warmed, completed draw time at 1920×1080; it does not include compositor capture, display or scanout.
