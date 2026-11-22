# Cube studies · revision 02

Visual proposal only. These keys are **not registered**. The scripts read no live compositor configuration, workspace list or captures, and do not load a plugin.

## Proposed Hyprland Lua shape

```lua
hl.config({ plugin = { hyprimate = { workspace = {
    enabled = true,
    style = "cube",
    options = { cube = {
        variant = "solid",      -- solid | inside_out | dimensional_fold | tesseract
        duration = 0.6,         -- seconds; proposed range 0.1–3, like rotation
        curve = "default",      -- native named curve; see preview timing below
        solid = { cube_tilt = 0, zoom = 0.25 },
        inside_out = { cube_tilt = 0, zoom = 0.45 },
        dimensional_fold = { show_other_workspaces = false },
        tesseract = { cube_tilt = 0, zoom = 0.25, next_workspace_start_size = 0.32 },
    } },
} } } })
```

Public keys and enum values would use underscores, following `workspace.options.rotation` and `workspace.options.planet_hop`. Hyphenated names in the movies are display labels. No user direction option, layered variant, or global tilt or zoom.

- **`cube_tilt`** is a signed angle from −20° to +20°. Positive tips the visible workspace face upward (its upper edge goes farther away); negative tips it downward. Zero keeps the horizon level. Influence vanishes at each endpoint. The room rotates the viewing direction in place rather than orbiting above or below the walls.
- **`solid.zoom` / `tesseract.zoom`** are extra peak camera retreat, measured in full desktop widths, range 0–1. Zero keeps geometric clearance; 1 adds one desktop width at peak. The camera returns to its original framing on landing.
- **`inside_out.zoom`** is a fraction from 0 to 1 of the extra *safe interior retreat*. Zero retains a modest view of the inner corner. One uses the full conservative interior allowance, not one exterior desktop width. No value takes the eye through a wall.
- **`dimensional_fold.show_other_workspaces`** defaults off. The demonstration uses two real entries from its synthetic inventory on either side of the journey: 01 Fern, **02 Mountain → 03 Desert**, 04 Plum. Neighbor textures are 384×216; missing images leave the corresponding planes absent. Proposed live behavior is to select existing eligible workspaces in native placement order, never fill numeric gaps, use only permitted budgeted inventory captures, and omit neighbors when unavailable or revoked. No live inventory traversal or capture is implemented here.
- **`tesseract.next_workspace_start_size`** is the destination/source cell-width ratio at 26% path progress. At 0 the destination has zero area at reveal and unfolds from a point afterward. At 1 the cell is already full width but remains recessed, changes orientation, and exchanges places with the old cell. Both retain the desktop aspect ratio; nearer faces can appear larger on screen. The mechanism is a staged 3D cutaway with an x–w rotation of the sleeve rails, not a rigid projection of a whole 4D tesseract.

Use the established config generator and validators after approval, not a new parser: invalid choices rejected; bounded floats clamped, non-finite reads take the schema minimum. During review the renderer just binds typed Python arguments.

## Reproduce the movies

Requires Python with NumPy, Pillow and ModernGL (EGL), a C++20 compiler, DejaVu Sans, and FFmpeg with libx264, drawtext and xstack. From the repository root:

```sh
python tools/cube-preview/render.py /path/to/review
python tools/cube-preview/parameters.py /path/to/review
```

The individual reels and the four-way comparison use the existing `direction_between` and `Transition` C++ code, the rotation family's configured **default** duration, and the controller's smoothstep curve. No live Hyprland curve is resolved. All four sample the same path clock, first in real time, then at quarter speed. A fresh backward switch exchanges the two capture owners and reflects travel, not lettering. An in-flight return keeps the original geometry and captures, and passes the presented position and velocity to `Transition::retarget`.

`parameters.mp4` has five four-second studies: exterior tilt/zoom, interior tilt/zoom, neighbor inventory, nested tilt/zoom and nested start size. Within each study the viewports run in sync and briefly hold a common pose for comparison. Video compression is lossy; exact endpoints refer to the uncompressed offscreen output.
