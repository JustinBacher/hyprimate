# Cube studies · fold and tesseract revision 03

This document records the four-variant visual proposal. Only `solid` and `inside_out` shipped as production options; `dimensional_fold` and `tesseract` remain preview-only. See the [generated configuration reference](../../README.md) for supported settings and [production verification](production.md) for rendering the approved pair with the real material. The study scripts do not read live captures or load a plugin.

Solid, inside-out and their parameter studies remain the approved revision 02. Revision 03 changes only the dimensional-fold and tesseract designs.

## Original proposal (includes preview-only options)

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
  In R3 the two primary leaves begin nearly closed (20°). The destination opens from behind the source to a shared 112° angle; the source then tucks behind it. Each additional leaf articulates at the outside edge of its neighbor. The whole assembly sits farther back while open if a captured neighbor needs room; absent captures do not change the framing. The eye stays fixed.
- **`tesseract.next_workspace_start_size`** is the destination/source cell-width ratio at 26% path progress. At 0 the insert has zero area at reveal and grows from a point afterward. At 1 it starts at the outer cell's width but is still recessed. All sizes use the same axial exchange: four panels connect the aperture of the receding source to the rear of the advancing destination. The aperture opens once. This is a 3D construction inspired by nested cells, not a 4D rotation.

Use the established config generator and validators after approval, not a new parser: invalid choices rejected; bounded floats clamped, non-finite reads take the schema minimum. During review the renderer just binds typed Python arguments.

## Reproduce the movies

Requires Python with NumPy, Pillow and ModernGL (EGL), a C++20 compiler, DejaVu Sans, and FFmpeg with libx264, drawtext and xstack. From the repository root:

```sh
python tools/cube-preview/render.py /path/to/review
python tools/cube-preview/parameters.py /path/to/review
```

To produce only the two revised films, their parameter tests and the historical comparisons, keep both archived directories intact and use a new output directory:

```sh
python tools/cube-preview/render.py R3 --only dimensional-fold tesseract
python tools/cube-preview/parameters.py R3 --only dimensional-fold tesseract
python tools/cube-preview/revision_comparisons.py R1 R2 R3
```

`inside-out-vs-fold.mp4` uses the approved room movie on the left. The plans below the movies recover the room eye from its drawn walls and measure the fold angle from the same world coordinates as its panels, not their perspective silhouettes. `tesseract-history.mp4` places the two archived encodes beside the revision, synchronized by shot. The earlier film's backward shot plays its forward geometry in reverse; the other two use a fresh backward switch.

The individual reels and the four-way comparison use the existing `direction_between` and `Transition` C++ code, the rotation family's configured **default** duration, and the controller's smoothstep curve. No live Hyprland curve is resolved. All four sample the same path clock, first in real time, then at quarter speed. A fresh backward switch exchanges the two capture owners and reflects travel, not lettering. An in-flight return keeps the original geometry and captures, and passes the presented position and velocity to `Transition::retarget`.

With all variants selected, `parameters.mp4` has five four-second studies. The two-variant R3 command renders just three: neighbor inventory, nested tilt/zoom and nested start size. Each briefly holds a common pose for comparison. Video compression is lossy; exact endpoints refer to the uncompressed offscreen output.
