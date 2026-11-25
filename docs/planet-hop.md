# Planet Hop

Load Hyprimate before setting plugin options. In a Hyprland Lua config:

```lua
local planet_hop = hl.plugin.planet_hop
planet_hop.curve("orbit", { type = "spring", mass = 1, stiffness = 70, damping = 10 })

hl.config({ plugin = { hyprimate = { workspace = {
    enabled = true,
    style = "planet_hop",
    options = { planet_hop = {
        duration = 800,
        curve = "orbit",
        window_mode = "surface",
        overlay_mode = "fade",
        planet_layout = "focused",
    } },
} } } })
```

`duration` is the total time in **milliseconds** from the first planetary presentation to the native landing. It defaults to `800`, accepts fractional values, and has no configured maximum. Set it to `0` to skip Planet Hop and leave the workspace change to Hyprland. Skipping workspaces does not add time. The camera, surface curvature, windows and shell all sample the same curved timeline. A reversal preserves position and outgoing velocity; a request for a third workspace queues the next leg rather than cutting away from the current pair.

`curve` accepts a native `hl.curve` Bézier or spring name. A spring declared with `hl.plugin.planet_hop.curve` is private to Planet Hop and takes precedence for a bare name. Use `spring:name` to explicitly select a native spring. Planet Hop uses Hyprutils' mass, stiffness, damping and step response. Unlike a native property that can overshoot and settle on its own schedule, the camera has two endpoints and a deadline: Planet Hop fits the spring's rising response to the duration, ending at its first peak or its settling tolerance rather than replaying rebounds or a stationary tail.

An unresolved explicit `spring:name` is invalid, not a Bézier name: the last valid curve is retained,
or the native default is used when none exists. Lua curve names and types containing embedded NULs
are rejected rather than truncated.

| `planet_layout` | Worlds and placement |
|---|---|
| `focused` | Only the source and destination; a direct flight framed around that pair. |
| `monitor` | Workspaces belonging to the transitioning monitor, including inactive workspaces. |
| `all` | Workspaces across outputs; placement follows the logical monitor layout with separation in depth. |
| `solar_system` | The same coverage as `all`, on a deterministic three-dimensional spiral with a curved camera flight. |

The scene uses real workspaces, never placeholders for missing workspace numbers. Hidden special workspaces do not populate ordinary journeys. `solar_vertical_spread` stretches the orbits vertically; zero retains the base orbit. Window, overlay, atmosphere and surface controls are independent of workspace coverage.

**Migrating an existing configuration:** if your duration is still in seconds, multiply by 1000 (`0.8` → `800` for Planet Hop, `0.6` → `600` for Rotation). Leave existing millisecond values unchanged. Replace `linear` or `dynamic` **layout** with `focused` to retain the old two-world scope. `linear` remains a valid **curve**. Remove `skip_duration_mode`, `distance_scale`, `max_duration` and `adaptive_boundary`: there is no implicit distance multiplier or second clock. Old layout names are not aliases.

Use `window_mode = "floating"` and adjust `elevation` for rectangular, separately anchored windows. Use `overlay_mode = "hud"` to keep panels screen-aligned. Set `sampling_quality = 0` to reduce tessellation and material sampling without turning off lighting, depth, clouds, stars or atmosphere. These effects and rotation each have their own switch. `depth_source` accepts a grayscale PNG depth map in wallpaper/screen coordinates (top row first); with no usable map, the renderer uses subtle procedural relief, not wallpaper luminance. See the [generated settings table](../README.md) for all ranges, defaults and switches.

The plugin links libpng to decode the configured `depth_source` at runtime. The GPU samples the resulting texture during the hop; frames do not decode the PNG again.

Depth maps must be regular files. Loading opens and validates the same descriptor without waiting on
a FIFO, and a failed load is retried on the next preparation, so installing or repairing a PNG at
the same path does not require changing the setting.

## Capture and safety

The style uses Hyprland's normal monitor-scene render order. A background-layer capture supplies the wallpaper; bottom shell, windows and top shell have independent coverage. A floating window gets native cropped captures before and after its draw—including decorations, shadow and popup extents. Surface transformation never moves a Wayland client. Desktop components painted *inside* a wallpaper client's own surface are wallpaper pixels, not independent overlays.

Inactive worlds use GPU thumbnails, not live application rendering every animation frame. Hyprimate renders at most one inactive workspace per output frame, prioritizing worlds along the route. Each capture uses its owning monitor's current windows, without a workspace switch, panels or client presentation feedback. Its last observed wallpaper is preferred. If a wallpaper client has never shown that workspace, Hyprland exposes only that output's current wallpaper, not the daemon's private per-workspace assignment. A newly captured image fades in over the available wallpaper. Preview detail falls with apparent size; thumbnails are limited to two million texels per journey and released when it ends.

The desktop covers the planet's front hemisphere. Its four edges meet the back hemisphere along the limb; they are not the border of an inset preview. During landing those same vertices flatten into the monitor rectangle. Wallpaper and surface windows keep their desktop UV coordinates, and floating windows use the same surface to locate their tangent planes.

As the surface flattens, the renderer keeps the wallpaper a few depth-buffer steps behind windows and world overlays. This prevents triangles of wallpaper showing through them in the last animated frames without changing their screen position, opacity, or the native handoff.

`overlay_mode = "hud"` draws the monitor's live shell in screen coordinates over the rendered scene, once. Hyprland applies the layer's own alpha and blur to the pixels behind it, rather than blending a bar captured over the old wallpaper. Opaque pixels painted by the bar itself remain opaque. While a shell layer is fading out as a native snapshot with blur already baked in, the compositor keeps control.

The compositor changes workspace once. The departing native image is captured before the change. Near the endpoints, Hyprimate refreshes content; during flight it reuses GPU textures. If the composition for an inactive source no longer exists or the monitor shell changed, the original capture stays on its planet. The destination is refreshed before touchdown. The cursor and lock screen are not planetary layers.

When transforming the scene would invalidate native protection (for example a locked session or `no_screen_share` content), the plugin leaves the compositor in control. A monitor must have rendered a normal frame before a capture can start. Output changes, unsupported capture stages, and framebuffer allocation failures return to native presentation rather than showing a partial capture. Capture memory scales with physical pixels; `floating` additionally needs room for visible window crops.

If the session locks, sharing rules change to protect a window, or a preview workspace changes owners during capture, Hyprimate discards the affected preview set, including cached wallpaper and both sides of a thumbnail crossfade. A retained window tail may still animate on the unlocked local monitor, but it is omitted from captures if its original pixels or its current source window are protected.

A queued frame belongs to one turn on one monitor. Hyprimate checks that turn and the current capture policy again before displaying the texture. A stale pass draws nothing. If the current turn loses authorization or the style fails to draw, the workspace plane clears to black until the native repaint; it never substitutes a saved destination image.

Native rendering can synchronously change the workspace or its settings. A capture interrupted this way cannot finish a replacement turn. Hyprimate submits or aborts only its own offscreen render session; the next normal monitor frame remains under compositor control.

## Renderer profiling

```sh
EGL_PLATFORM=surfaceless build/planet-renderer-tests --profile 2560 1440 120
EGL_PLATFORM=surfaceless build/planet-renderer-tests --profile 3840 2160 120
```

The profiler sweeps a complete journey with cached workspace layers. It measures all three quality settings and individual environmental effects. It reports completed wall time and, where `GL_EXT_disjoint_timer_query` is available, GPU time. It does not measure compositor capture, vsync, or recording overhead.
