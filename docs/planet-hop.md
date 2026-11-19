# Planet Hop

Load Hyprimate before setting plugin options. In a Hyprland Lua config:

```lua
hl.config({ plugin = { hyprimate = { workspace = {
    enabled = true,
    style = "planet_hop",
    options = { planet_hop = {
        duration = 0.8,
        window_mode = "surface",
        overlay_mode = "fade",
        planet_layout = "linear",
        skip_duration_mode = "adaptive",
    } },
} } } })
```

`linear`, `solar_system`, and `dynamic` change where the worlds are, not how the camera travels. Numbered skips use the order of existing numbered workspaces; visiting a named or special composition counts as one hop for timing. `constant` uses `duration`. `distance_scaled` adds `duration × distance_scale` per additional step. `adaptive` grows logarithmically only beyond `adaptive_boundary`. Both scaled modes respect `max_duration` without shortening the base hop.

In `solar_system`, every workspace in Hyprland's inventory has a position. Numbered workspaces follow their numbers, with deterministic variation in spacing, depth and height. Gaps leave space, not invented planets. Named and special workspaces use stable positions of their own. The camera still flies directly between endpoints. `planet_separation` sets the shared spacing; `solar_vertical_spread` (default `0.65`) adjusts its aspect-aware height distribution, and `solar_background_scale` (default `0.75`) sets the inactive planet radius before perspective.

Use `window_mode = "floating"` and adjust `elevation` for rectangular, separately anchored windows. Use `overlay_mode = "hud"` to keep panels screen-aligned. Set `sampling_quality = 0` to reduce tessellation and material sampling without turning off lighting, depth, clouds, stars or atmosphere. These effects and rotation each have their own switch. `depth_source` accepts a grayscale PNG depth map in wallpaper/screen coordinates (top row first); with no usable map, the renderer uses subtle procedural relief, not wallpaper luminance. See the [generated settings table](../README.md) for all ranges, defaults and switches.

The plugin links libpng to decode the configured `depth_source` at runtime. The GPU samples the resulting texture during the hop; frames do not decode the PNG again.

## Capture and safety

The style uses Hyprland's normal monitor-scene render order. A background-layer capture supplies the wallpaper; bottom shell, windows and top shell have independent coverage. A floating window gets native cropped captures before and after its draw—including decorations, shadow and popup extents. Surface transformation never moves a Wayland client. Desktop components painted *inside* a wallpaper client's own surface are wallpaper pixels, not independent overlays.

Inactive worlds use GPU thumbnails, not live application rendering every animation frame. Hyprimate renders at most one inactive workspace per output frame, prioritizing worlds along the route. Each capture uses its owning monitor's current windows, without a workspace switch, panels or client presentation feedback. Its last observed wallpaper is preferred. If a wallpaper client has never shown that workspace, Hyprland exposes only that output's current wallpaper, not the daemon's private per-workspace assignment. A newly captured image fades in over the available wallpaper. Preview detail falls with apparent size; thumbnails are limited to two million texels per journey and released when it ends.

The wallpaper has one angular chart from departure to landing. The globe starts as a flat tangent plane, bends along longitude and latitude, and retains a shadowed unmapped surface beyond the chart. A floating window uses the same bend to locate its tangent plane. Quality affects sampling, not the camera path or the native endpoints.

As the surface flattens, the renderer keeps the wallpaper a few depth-buffer steps behind windows and world overlays. This prevents triangles of wallpaper showing through them in the last animated frames without changing their screen position, opacity, or the native handoff.

`overlay_mode = "hud"` draws the monitor's live shell in screen coordinates over the rendered scene, once. Hyprland applies the layer's own alpha and blur to the pixels behind it, rather than blending a bar captured over the old wallpaper. Opaque pixels painted by the bar itself remain opaque. While a shell layer is fading out as a native snapshot with blur already baked in, the compositor keeps control.

The compositor changes workspace once. The departing native image is captured before the change. Near the endpoints, Hyprimate refreshes content; during flight it reuses GPU textures. If the composition for an inactive source no longer exists or the monitor shell changed, the original capture stays on its planet. The destination is refreshed before touchdown. The cursor and lock screen are not planetary layers.

When transforming the scene would invalidate native protection (for example a locked session or `no_screen_share` content), the plugin leaves the compositor in control. A monitor must have rendered a normal frame before a capture can start. Output changes, unsupported capture stages, and framebuffer allocation failures return to native presentation rather than showing a partial capture. Capture memory scales with physical pixels; `floating` additionally needs room for visible window crops.

## Renderer profiling

```sh
EGL_PLATFORM=surfaceless build/planet-renderer-tests --profile 2560 1440 120
EGL_PLATFORM=surfaceless build/planet-renderer-tests --profile 3840 2160 120
```

The profiler sweeps a complete journey with cached workspace layers. It measures all three quality settings and individual environmental effects. It reports completed wall time and, where `GL_EXT_disjoint_timer_query` is available, GPU time. It does not measure compositor capture, vsync, or recording overhead.
