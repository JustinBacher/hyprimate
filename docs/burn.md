# Burn

Select the material with `hl.config({plugin={hyprimate={window={style="burn"}}}})`. Opening, movement and closing respect the existing `window.open.enabled`, `window.movement.enabled` and `window.close.enabled` switches. The generated [configuration reference](../README.md) lists the ranges, units and defaults.

For example, to burn the window in place but carry debris toward the upper right:

```lua
hl.config({ plugin = { hyprimate = { window = {
    style = "burn",
    options = { burn = { wind_direction = 315, wind_strength = 240 } },
    close = { options = { burn = {
        closing_style = "consume", debris_direction = "wind", duration = 1.6,
    } } },
} } } })
```

`duration` controls burn speed; a shorter time is faster. `curl` bends the sheet locally at the front instead of contracting the entire window. Wind acts on detached material, not the location at which the sheet ignites. Wallpaper recovery has its own clock: `scorch_lifetime` is the cooling period, `healing_duration` is the subsequent restoration time. Neither setting prolongs the client's lifetime.

## Where the material comes from

Burn samples the compositor's live window image on opening and its retained fadeout image on closing. A detached flake carries source coordinates from that image; the client need not remain mapped. Local bends use a depth-tested mesh, not a simulated rigid sheet. The intact foreground and cooler print come from the same texture.

The wallpaper pass samples the rendered background before other windows, not the wallpaper file or a screenshot of the desktop. It combines a per-pixel opening rim with a 96×64 heat and damage field per affected output/workspace. Returning to a workspace shows its damage at its current age. Pinned damage belongs to the output. At most eight such fields are resident; a saturated budget drops new damage rather than evicting a still-visible trail.

This is an OpenGL effect. Fullscreen clients follow the framework's existing native path. If Hyprland cannot supply a compatible fadeout (including a fadeout that requires backdrop blur), Burn does not take ownership. Detached closing material is clipped to its originating output. The overlay does not reconstruct covered wallpaper from window pixels.

## Inspect on a private desktop

Build the plugin against the installed Hyprland, then run:

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure -R burn
/usr/bin/python tests/nested/burn_evidence.py build/burn-review --record
```

Use a new output directory for each run. The harness starts a private virtual KWin on its own D-Bus session with nested Hyprland, loads `build/hyprimate.so` only there, paints a labelled contour-map background, and maps real GTK clients with printed text and graphics. It needs `dbus-run-session`, KWin, Hyprland, `hyprctl`, `wayland-info`, `grim`, GTK 3, gtk-layer-shell, PyGObject, pycairo and Pillow. `--record` also needs `wf-recorder`; omit it for PNGs only. The matrix saves the full output at intermediate and settled stages without changing the active compositor.

If Aquamarine cannot allocate the second virtual output on your driver, add `--single-output`. The harness still checks workspace isolation, but omits the second-monitor comparison and records `other_output: null` in the manifest.

- Compare `*-open.png` with `*-native.png`: the same print should appear first at the center, framed by burned **wallpaper**, and fit the native geometry when settled.
- Inspect the area behind the moving window in `*-trail.png`, then the same area in the close and healed frames. A fresh scorch should retain map contours and labels. Changing workspace must not move it onto another workspace or monitor.
- Compare `consume-fall-close-mid.png` and `curl-natural-close-mid.png` at native resolution. Look for desaturated but readable print, changes in the silhouette of the surviving sheet, and detached pieces moving separately from the fire front. Use the recording to evaluate flutter, not just a still.
- `measurements.json` compares settled/native, exposed trail/native wallpaper, other workspace/output, healed and post-unload images. A native endpoint match alone does not validate an animation.

For interactive inspection from a running graphical session, `tests/nested/run.sh --no-build --style burn` starts the project's disposable nested compositor. Use Alt+Space to float a window, Alt+drag to move it, and Alt+C to close it before its trail cools.
