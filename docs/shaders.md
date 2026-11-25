# Native shader effects — specification 1

Hyprimate loads declarative effect packages written for **OpenGL ES 3.0 and GLSL ES 3.00**. Packages are trusted executable GPU workloads, not sandboxed content. Install only packages you trust. Allocation budgets and pass limits do not bound shader execution time.

There is no Burn My Windows compatibility layer, JavaScript runtime, shader translation, Vulkan backend, or private EGL context. Ported effects must adapt their source, helper functions, parameters, assets, and setup to this specification. No third-party effects are bundled.

## Select effects independently

`window.style` remains the built-in style. The `window.open`, `window.close`,
`window.move` and `window.resize` blocks each accept a separate external effect:

```lua
hl.config({ plugin = { hyprimate = { window = {
    style = "liquid",
    open = {
        effect = "/absolute/path/to/effect.json",
        effect_duration = 600, -- milliseconds; zero uses the native path
        effect_parameters = '{"tint":[0.7,0.9,1.0]}',
    },
    close = {
        effect = "/absolute/path/to/another/effect.json",
        effect_duration = 900,
    },
    move = { effect = "/absolute/path/to/move/effect.json", effect_duration = 350 },
    resize = { effect = "/absolute/path/to/resize/effect.json", effect_duration = 350 },
} } } })
```

An empty `effect` uses the existing built-in style. `effect_duration = 0`
disables that external effect. `window.movement.enabled` gates both move and
resize; opening and closing have their own enable switches. Parameters are a
strictly typed JSON object. Reload configuration to reread assets and GLSL at
an unchanged path. A failed replacement retains the last valid selection.
An active instance pins its program and parameters. Content-identical prepared
generations reuse linked programs and immutable GPU assets in the same context.

Mapped presentation has one owner. A tracked move or resize interrupts opening
and hands the current sample to that event's selected effect or built-in style.
A live resize grab takes precedence over movement; outside a grab, a sample
that changes client-body dimensions is a resize. During ongoing motion progress
is zero. After release/idle it advances from zero to one over `effect_duration`;
continued motion of the same kind reuses the active instance. Changing the kind
or closing ends that instance. Shader opening leaves Hyprland's native fade
active; its alpha is already in the borrowed raster.

`workspace.effect`, `workspace.effect_parameters` and `workspace.effect_duration`
select a transition when `workspace.enabled` is true. The selected native style
supplies its curve and capture/HUD policy and remains the fallback. The two
authorized endpoint images are captured once per pair. Reversal reuses them
and reverses progress instead of swapping sampler bindings. These are snapshots,
not live feeds; changes on the desktop appear at the next pair or on native
handoff. Endpoints use capture-local blur; a native-cache capture cannot be
borrowed by a shader. Native fallback restores the style's normal capture policy.

`wallpaper.effect` and `wallpaper.effect_parameters` select one independently
initialized instance per output. `wallpaper.effect_duration = 0` runs
continuously while visible; a positive duration in milliseconds holds the last
frame without scheduling more redraws. The pass renders over native background
content and below bottom layers and windows. Alpha reveals the native background.
The window/endpoint samplers are still transparent black on wallpaper; the
explicit environment resources below are separate. Authorized workspace
captures can replay procedural wallpapers without advancing them. No Niri
integration is included.

## Optional portable compute

Version 1 packages can declare private typed `fields` and `kernel` passes in
`initialize` or `update`. Kernels have the same logical ABI on GLES 3.0 and
3.1+: Hyprimate uses the current compositor context and can execute the same
source as native compute or as a typed fragment draw. An ordinary `render` pass
may sample a field directly in its vertex or fragment shader, but only a kernel
can write fields. Packages without fields retain the existing texture and
render-pass ABI.

Try [`compute-stencil`](../examples/shaders/compute-stencil/effect.json) as an
open/close effect or a wallpaper, and
[`compute-particles`](../examples/shaders/compute-particles/effect.json) as an
open/close effect. See [Portable GPU compute](compute.md) for the manifest
syntax, run commands, GLSL ABI, and standalone C++ example.

## Original and reactive wallpaper

Version 1 packages opt in with an optional `environment` object. Window
effects can use just `{"environment":{"original":true}}`. A reactive
wallpaper does not require any Hyprimate window effect to be enabled:

```lua
hl.config({plugin={hyprimate={wallpaper={
    source_namespace="my-wallpaper", -- exact layer-shell BACKGROUND namespace
    effect="/absolute/path/to/reactive-water/effect.json",
    effect_duration=1,              -- positive selects reactive finite mode
}}}})
```

An empty `source_namespace` permits only the unobscured stock Hyprland image.
An explicit namespace permits one eligible, opaque, output-covering
BACKGROUND root in standard SDR. The host texture must be a normal 2D image
created from copied/synchronous content, not a leased DMA-BUF. Its current
identity, placement and privacy rule are checked when sampled. Popup or
subsurface trees, another BACKGROUND root or fadeout, private layers, custom
color transforms, and unsupported buffer or viewport geometry are declined.
Bottom, top and overlay layers are never inputs. The plugin does not read or
publish the composed desktop, and never makes a screenshot to get wallpaper.
When original is unavailable a source-aware wallpaper leaves native rendering
alone. Window helper lookups return transparent black and availability is false.
For an accepted native texture, `textureSize(hypr_wallpaper_original, 0)`
reports its physical texel dimensions. The UV affine accounts for the host's
stock image crop.
When a root or its lease is replaced, the simulation restarts. A producer
that paints new pixels into the same accepted SHM texture simply changes the
next sample; it does not reset the simulation on every repaint.

| `environment` key | Allowed on | Contract |
| --- | --- | --- |
| `original` | Window, wallpaper | Read-only original wallpaper and its UV affine |
| `processed` | Window | Read-only result explicitly published by the current wallpaper |
| `interactions`, `settle_ms` | Wallpaper | Bounded window interaction batch; positive 1–60000 ms idle tail |
| `publish` | Wallpaper | Render the shader image into a private output-sized texture and composite it natively; write the complete processed image if consumers expect it |
| `channel`, `state` | Wallpaper | Named channel and optional readable state (e.g. `"height.read"`); only a mutable persistent texture produced in update can be exported |
| `channel`, `sample_state` | Window | Read the currently committed output state on the matching channel |
| `channel`, `emit` | Window | Export a persistent update-produced 1×1 RGBA8 texture; all four normalized channels are defined by the pair of packages |

The original sampler is `hypr_wallpaper_original` with `hypr_wallpaper_uv`
(origin and span in backing texture UV). The processed sampler is
`hypr_wallpaper_processed` with a full-output domain.
It is the package's premultiplied layer, not a copy of the layers under it:
transparent pixels remain transparent. A publisher can include the original
image itself, as the burn example does, to expose a complete processed image.
Prefer `hypr_sample_wallpaper_original(uv)` and
`hypr_sample_wallpaper_processed(uv)`:
out-of-bounds and missing inputs are transparent black. Raw samplers must test
their `*_available` flag. Normalized coordinates are top-first in the output's
display orientation; `hypr_output_origin` and `hypr_output_size` are global
logical origin and logical size. `hypr_environment_uv(windowUv)` maps a
window-normalized sample through `hypr_output_rect` into the normalized
output; pass it `hypr_window_uv()` to account for expanded bounds. Output
scale is `hypr_scale` and the physical size of the *current attachment* is
`hypr_target_size` (which may be a reduced simulation size). The host owns
physical output transforms and composition.

`hypr_interaction_count` is at most 24. Each record uses four consecutive
`vec4` elements in `hypr_interactions`: current and preceding output-normalized
client-body rectangles (xy, width, height), their per-second normalized
velocity, and (event, strength, opaque ID, held). Event values are open=0,
close=1, move=2, resize=3. Rectangle differences give movement and resize
direction. They describe native presented bounds, not layout goals, exact
surface quads or shader-deformed vertices. Positions may be outside [0,1];
the receiving output clips the result. Sweep between the rectangles to cover
large frame-to-frame movement. Opening/closing are geometry impulses, not
queries for the progress or duration of the active animation. No titles,
application IDs, pixels or client handles are delivered. The closing impulse
carries geometry only; a destroyed client does not retire an existing trail.
A queued closing impulse is dropped if that output changes workspace
composition before the simulation can receive it.
The host tracks at most 24 clients across 32 outputs. At saturation, excess
clients may not be admitted; a still-live contributor is not evicted in a
way that would prevent revocation of data it already contributed.

The wallpaper pass gathers the batch before updating; following native
window draws consume the newly committed `hypr_environment_state` and
processed wallpaper. Emissions are copied into an output-owned bounded atlas
and are visible on the **next** authoritative output frame, at the matching
interaction index in `hypr_disturbances`; test `hypr_disturbances_available`
and use `texelFetch(hypr_disturbances, ivec2(index,0), 0)`. The blank slot is
zero. No package writes another's attachments or samples the active scene
target. A window must have an admitted interaction on the receiving frame;
emissions by themselves do not wake a wallpaper or create an event.
Window packages may read environmental textures only in render passes, never
in initialize/update. Workspace captures use native window presentation for
those packages. These boundaries prevent an active window from accumulating
pixels derived from an older or newly protected environment.

An environment instance belongs to an output's current workspace composition
and prepared wallpaper generation. It persists after individual windows
stop or die. It never migrates to a new output or workspace. Workspace
changes, output geometry changes, source replacement/revocation, lock, and
protection changes discard affected state rather than resample or transfer
it. Content-identical package reloads preserve the generation. Workspace
snapshots use native wallpaper for environment-enabled packages, not a new
simulation or a published environment from another workspace. Capture and
blur replay never publish disturbances or consume updates. Output removal
and plugin shutdown drop state in the owning host context.

In continuous mode (`effect_duration=0`), time advances with authoritative
output frames. A finite reactive package wakes on a batch, sets
`hypr_progress` back to zero for a new disturbance, then reaches one at
`settle_ms` without GPU readback. Use progress=1 to author exact neutral
behavior. Native damage/capture may still render committed state without
simulating. Arbitrary shader output can extend over the whole output, so
running simulations schedule full output damage; absence of an environment
request adds no collector, atlas, or extra pass.

See [refraction](../examples/shaders/refraction/), [reactive water](../examples/shaders/reactive-water/),
[reactive burn](../examples/shaders/reactive-burn/), and the matching
[environment emitter](../examples/shaders/environment-emitter/).

## Native blur

For a window package that can draw its own coverage, declare `"blur_mask": true`.
Hyprland still generates, selects, damages and composes the background blur.
Hyprimate renders the package's render phase a second time with `hypr_blur_mask`
set and with `hypr_source` replaced by the native rounded rectangle (red = 1
inside, red = 0 outside). Reuse the same vertex deformation, sampling offsets,
discard conditions and animation opacity; output `vec4(weight)`, where red is
the blur coverage in [0,1]. For premultiplied blending this also supplies the
coverage alpha. Do not copy RGB decoration, glows or particle tint into red.
Fully transparent glass can still have nonzero weight. For a dissolve, remove
mask coverage where the window disappears. Initialization and update do not
run during the mask replay; persistent textures and buffers belong to the main
color simulation, not a second simulation.

Packages without this explicit contract use the native presentation when
the window requires blur. Blur is not inferred from color alpha. Custom
vertex deformation and expanded bounds work only if the package draws matching
coverage and declares sufficient bounds. Native policy may further restrict
blur, and protected content never becomes a shader source.

## Minimal package

A directory needs just a manifest and fragment source:

```json
{"version":1,"events":["open","close"],"fragment":"fade.frag"}
```

```glsl
layout(location = 0) out vec4 color;
void main() {
    float amount = hypr_event == 0 ? hypr_progress : 1.0 - hypr_progress;
    color = hypr_sample_source(hypr_window_uv()) * amount;
}
```

The runtime supplies the vertex shader and fullscreen triangle. The GLES
executor can render an eligible final pass directly into a caller-provided
target; the mapped-window transformer still has to return a result texture
to stock Hyprland 0.57.

Paths to source files, helpers, PNG images, and binary buffers are relative to the manifest directory. A manifest path starting with `~/` expands against `HOME`; other relative manifest paths use the compositor's working directory. There is no environment-variable interpolation or shell execution. Files must be regular files. Unknown fields, duplicate keys, unsupported versions, and incompatible declarations are rejected.

## GLSL interface

Sources omit `#version` and standard runtime declarations. Hyprimate prepends `#version 300 es`, high precision for float/int, the native uniforms, declared parameter uniforms, and optional helper files. Both shader stages receive the common uniforms. Fragment stages receive `in vec2 hypr_uv`; custom vertex stages must supply a matching output if it is consumed.

| Uniform | Type | Meaning |
| --- | --- | --- |
| `hypr_progress` | float | Event progress, 0 to 1, for both directions |
| `hypr_elapsed` | float | Seconds elapsed in this animation |
| `hypr_delta` | float | Pending simulation time in seconds; not a draw-call count |
| `hypr_duration` | float | Event duration in seconds |
| `hypr_size` | vec2 | Original window capture size in logical pixels |
| `hypr_target_size` | vec2 | Current pass attachment size in physical pixels |
| `hypr_scale` | float | Output scale |
| `hypr_event` | int | open=0, close=1, move=2, resize=3, workspace=4, wallpaper=5 |
| `hypr_pointer` | vec2 | Event-time pointer in window-normalized coordinates |
| `hypr_pointer_valid` | bool | Whether an event-time pointer was available |
| `hypr_fullscreen` | bool | Host-provided fullscreen state, subject to host eligibility rules |
| `hypr_seed` | vec4 | Stable, per-instance pseudorandom components in [0,1) |
| `hypr_source` | sampler2D | Only the approved, isolated source image |
| `hypr_source_uv` | vec4 | Source UV origin and span within that image |
| `hypr_domain` | vec4 | Window-normalized origin and extent covered by the target |
| `hypr_iteration` | int | Zero-based iteration within this pass |
| `hypr_iterations` | int | Number of iterations of this pass |
| `hypr_geometry`, `hypr_previous_geometry` | vec4 | Current and preceding presented client-body `(x, y, width, height)` in logical pixels; subtract to obtain the geometry delta |
| `hypr_velocity` | vec4 | Change in the geometry components per second; zero while settling or without an interval |
| `hypr_held`, `hypr_settling` | bool | Matching pointer grab and settling phase for move/resize |
| `hypr_direction` | float | Workspace travel direction (`-1` or `+1`); zero when unused |
| `hypr_destination`, `hypr_destination_uv` | sampler2D, vec4 | Second authorized workspace image and its UV rectangle; transparent on wallpaper |
| `hypr_blur_mask` | bool | Render-phase replay to produce native blur coverage, not color |

`hypr_uv` spans [0,1] across the current target, with texture row zero treated as the top. `hypr_window_uv()` maps it through `hypr_domain`; it can be negative or exceed one for expanded final bounds. Intermediate texture passes use a [0,0,1,1] domain.

`hypr_sample_source(uv)` samples window-normalized coordinates, respecting the source subrectangle and texel centers, and returns transparent black outside [0,1]. It is available in vertex shaders too, using explicit level zero sampling. Source images and shader output use **premultiplied alpha**. `hypr_unpremultiply()` and `hypr_premultiply()` are provided for straight-color arithmetic. Auxiliary PNG assets are raw RGBA samples; apply the appropriate alpha convention yourself.

`hypr_sample_destination(uv)` samples the other workspace endpoint. Direction
denotes the orientation of the pinned pair, not the sign of the progress
derivative. The source and destination samplers never bind the current scene.

Shading language syntax is native ES 3.00: use `texture`, explicit fragment outputs, and ordinary vertex inputs. No legacy `texture2D`, Cogl variables, imported shader-language aliases, or upstream helper collection is injected. `helpers` is an optional list of GLSL files, prepended in order; helpers use the same native interface.

External sources cannot request `#extension` features. Optional texture-format capabilities are checked by the host; shader code remains within the native ES 3.00 resource interface.

## Typed parameters

`parameters` maps GLSL uniform identifiers to declarations:

```json
{"parameters":{
  "gain":{"type":"float","default":1.0,"min":0.0,"max":2.0},
  "tint":{"type":"vec3","default":[1.0,1.0,1.0],"min":0.0,"max":1.0},
  "enabled":{"type":"bool","default":true}
}}
```

Types are `float`, `int`, `bool`, `vec2`, `vec3`, and `vec4`. Vector defaults/overrides have exactly the declared component count. Optional numeric bounds apply to each component. Values must be finite and representable. Hyprimate injects each parameter's uniform declaration; do not redeclare it. Names beginning `hypr_` or `gl_` are reserved. Animation instances pin their resolved parameter values. Changing configuration does not mutate another active instance's uniforms.

## Ordered passes

Use `passes` instead of the top-level `fragment` shorthand for advanced effects. The execution order is declarative and fixed; it is not a general-purpose render graph.

Each pass declares:

- `name`: diagnostic identifier.
- `phase`: `initialize`, `update`, or `render`, in that phase order.
- `vertex`: optional custom vertex source; absent uses the fullscreen vertex shader.
- `fragment`: fragment source for a raster pass.
- `inputs`: mapping from shader sampler names to texture references. Declare these sampler uniforms in GLSL yourself.
- `blocks`: mapping from GLSL uniform block names to buffer references. Declare blocks with `layout(std140)` and size them for the buffer contents.
- `outputs`: ordered texture references; locations 0,1,... form multiple render targets. `output` is the caller-provided final target and cannot be combined with internal attachments.
- `mesh`: optional named mesh; absent means a fullscreen triangle.
- `iterations`: bounded repeat count, default one. Use `hypr_delta / float(hypr_iterations)` when each iteration is a physical time substep.
- `clear`: clear attachments before drawing, default true.
- `blend`: `none` (default) or `premultiplied` (`ONE`, `ONE_MINUS_SRC_ALPHA`).

Every raster output needs an unconditional, explicit `layout(location=N) out float/vec2/vec3/vec4 name;` declaration matching its attachment. Integer outputs and missing locations are rejected. Helpers may use ordinary single-line math macros; preprocessing cannot generate, rename, or conditionally declare raster outputs. This keeps output validation deterministic without implementing another GLSL compiler.

Initialization executes once per prepared instance allocation. Update executes once for each authorized pending simulation step. Rendering can execute repeatedly without advancing persistent state. Multiple rendering/capture passes do not accelerate a simulation. Pending time accumulates when no eligible live render occurs. A read-only capture cannot initialize an instance, consume pending update time, or modify persistent resources.

At least one rendering pass must write `output`. Rendering passes may write transient scratch textures but may not modify persistent state. Initialize/update passes cannot write the final compositor target. Pass dependencies must follow declared order; a transient resource must be produced earlier in the same phase before it is read. Sampling a texture attached to the same draw is forbidden.

A transient raster attachment's first writer in a phase must clear it. Later passes can preserve/blend into initialized single storage with `clear:false`. Transient ping-pong destinations must be cleared on every writing pass, because producing the current read side does not initialize the opposite physical storage.

## Texture resources and ping-pong state

`textures` is a map of resource names. A renderable texture accepts:

- `format`: `r8`, `rg8`, `rgba8`, `r16f`, `rg16f`, `rgba16f`, `r32f`, `rg32f`, or `rgba32f`.
- `scale`: fraction of capture pixel dimensions, greater than zero and at most one; default one. This decouples simulation resolution from final output resolution.
- Alternatively `width` and `height`: explicit positive dimensions.
- `lifetime`: `persistent` (default) or `transient`.
- `ping_pong`: allocate independent read/write storage, default false.
- `filter`: `nearest` (default) or `linear`.
- `wrap`: `clamp` (default) or `repeat`.

An asset instead declares `path` to a PNG, plus optional filtering/wrapping. Assets are read-only. The runtime validates actual texture limits, renderable formats, extensions where needed, and framebuffer completeness. Floating-point attachment support is not assumed just because the context is GLES 3.0; unsupported packages fail explicitly.

A ping-pong resource is read as `state.read` and written as `state.write`. After a successful writing pass/iteration, the runtime swaps handles. It does not copy texture contents. Non-ping-pong resources use their bare name. PNG resources cannot be outputs. Multiple outputs must have compatible dimensions and fit actual draw-buffer/attachment limits.

See [the diffusion example](../examples/shaders/diffusion/effect.json): initialization seeds heat; four quarter-resolution update passes diffuse it; the rendering pass reads the current heat state while presenting the original window.

## Buffers, meshes, instancing, and transform feedback

`buffers` declares `bytes`, an optional `data` array of float32 initial values or `path` with raw binary contents, and optional `lifetime`/`ping_pong`. Remaining bytes are initialized to zero. Buffer declarations can supply vertex attributes, user uniform blocks, and transform-feedback output without a GPU-to-CPU round trip.

`meshes` declares `primitive` (`points`, `lines`, `triangles`, `triangle_strip`), `count`, optional `instances` (default one), optional uint32 `indices`, and `attributes`. Each attribute declares `location`, `buffer`, `components` (1–4 float32 values), optional byte `stride`/`offset`, and optional `divisor`. Nonzero divisors advance attribute data per instance. Actual attribute limits and fetched buffer ranges are checked before execution. Custom shaders can also use `gl_VertexID` and `gl_InstanceID` without attribute buffers.

A transform-feedback pass declares `vertex`, a named mesh, and:

```json
{"feedback":{"buffer":"particles.write","varyings":["nextState"]}}
```

Feedback is interleaved into one declared buffer. Rasterization is disabled. The runtime links the named varyings, validates their component counts and output capacity, and swaps a ping-pong destination after the pass. This interface supports non-indexed `points`, `lines`, and `triangles` for feedback. Indexed and triangle-strip geometry remain available for ordinary rendering, not feedback. Feedback is an initialization/update operation, never a render/capture operation.

Feedback passes must name a mesh and emit complete primitives. A transient feedback destination must be filled exactly; partially written transient buffers cannot carry undefined data into later passes.

See [the particle example](../examples/shaders/particles/effect.json): a feedback initialization pass creates particles, another updates their positions, a `std140` block supplies forces, and instanced custom vertex shaders expand the GPU-resident state into quads. No CPU particle update is required.

## Bounds, resizing, and lifetime

`bounds` is `[left, top, right, bottom]`, fractional margins relative to the original capture dimensions. It defaults to zero. Each component must be between zero and two. Expanded bounds feed native damage tracking and result-target allocation; they do not enlarge the source texture or grant access to neighboring desktop content.

Immutable compiled programs and assets are separate from mutable instance state.
Concurrent animations never share writable simulation textures or buffers. A
resize preserves state if all resolved attachment dimensions still match (in
particular if the package uses fixed dimensions and buffers). Otherwise
replacement storage is allocated transactionally, reset, and initialized
again. Output geometry changes recreate a wallpaper instance for that output.
Context-owned resources are retired only while the owning compositor context
is usable; lost-context handles are abandoned without GL calls.

The reusable executor accepts borrowed source textures and final render targets. The compositor integration is responsible for source provenance, permissions, session state, resource lifetime, coordinate conversion, damage, and host rendering-state preservation. A full-monitor snapshot must not be exposed as a window sampler: when an exclusive window texture is unavailable, the integration must GPU-crop approved content before exposing it, even though this requires a copy. The runtime never grants arbitrary compositor texture handles to packages.

An owned close is declined before native suppression if the retained snapshot does not contain the complete window capture. Missing offscreen pixels are not synthesized from neighboring desktop content.

The GLES target interface also accepts a nonzero framebuffer-region origin. Viewport and scissor constrain both clearing and drawing to that region, preserving surrounding compositor pixels. `hypr_target_size` and `hypr_uv` remain region-local; GLSL `gl_FragCoord` retains actual framebuffer coordinates.

The caller marks private pooled targets with `Target::clear_before_render`.
The executor initializes the target region before its first final pass,
combining this with that pass's clear when possible. Later passes may preserve
earlier results. Blending or `discard` cannot expose a previous lease's
pixels. Direct presentation instead blends over the already rendered native
scene and never clears it; disjoint damage scissors preserve untouched tiles.

## Budgets and failure behavior

Packages are bounded to 32 passes, 32 iterations per pass, 16 textures, 16 buffers, 16 meshes, 64 parameters, and 128 MiB of mutable per-instance storage including both ping-pong sides. A buffer is limited to 32 MiB; a pass is limited to 4,194,304 vertex invocations. Target images are limited to 8192 per dimension and 16,777,216 pixels; declared fixed simulation images and PNG assets to 4096 per dimension. A manifest is limited to 64 KiB, each shader/helper to 1 MiB, assembled shader source to 4 MiB, each encoded PNG to 16 MiB, and aggregate decoded assets to 64 MiB. Actual driver limits may be lower.

Malformed source/metadata, unsupported formats, allocation failures, invalid dependencies, privacy restrictions, and unavailable contexts cause an explicit failure and native fallback. The runtime does not silently replace a requested format or introduce an expensive CPU simulation fallback. These checks do not protect against deliberately nonterminating or excessively expensive trusted shader code.

## Architecture and performance

- `src/shader/Package.*`: native specification, loading, parameter validation, source assembly, resources and pass ordering; no compositor or EGL dependencies.
- `src/shader/GlesRuntime.*`: reusable GLES3 execution within the caller's context; no compositor or EGL ownership.
- `src/window/shader/` and `src/shader/{SceneShader,ShaderWallpaper}.hpp`: native presentation bridges. The reusable package and execution files do not own compositor or EGL lifetimes.

A future compositor integration must be implemented against its native renderer and ownership rules. These modules do not assume that Niri can reuse Hyprland's capture or hook architecture.

On Hyprland 0.57, [`IElementRenderer::drawTransformedWindow`](https://github.com/hyprwm/Hyprland/blob/0aa6745f705f2c4b0a5004eb019ef76e5bbdb426/src/render/ElementRenderer.cpp#L693-L846) renders a mapped
window and optional rounded blur source into work buffers, invokes the plugin
with those buffers bound, and unconditionally composites the returned image
with the host texture pass. No public transformer method can take over that
composition. For opted-in blur the plugin prepares both results before
publishing either, so a failed matte draw cannot leave a transformed color
image over a rectangular or missing blur. The native helper still produces its
own undeformed matte for the fallback case.

Close, wallpaper and workspace presentation can write the native scene target
directly when all final draws are consecutive, the first replaces transparent
storage with a non-overdrawing fullscreen triangle or uses source-over, and
later draws use premultiplied source-over without clearing. Final shaders that
use `gl_FragCoord` stay isolated. The target must have matching color
encoding and no mirror attachment, output transform, active render modifier,
or required group opacity. Blur closes and other unsupported presentations use an isolated
result followed by the native texture compositor. Workspace captures are
leased separately from the presentation target, and close still isolates the
approved crop of a monitor-sized snapshot before arbitrary GLSL can sample it.

Each eliminated RGBA8 result avoids a pooled attachment of `4 × width × height`
bytes, a clear, a final effect write to that attachment and a compositor
sampling/composition pass. A full write plus one full sample is `8 × width ×
height` nominal attachment bytes before counting the clear. At 1920×1080 that
is 15.82 MiB per draw; FP16 doubles it. These are estimates, not measured VRAM
traffic: compression, cache hits, damage and tile behavior change physical
bandwidth. Required simulation and capture passes are additional.

Hyprland queues the stock image and BACKGROUND roots before emitting
[`RENDER_POST_WALLPAPER`](https://github.com/hyprwm/Hyprland/blob/0aa6745f705f2c4b0a5004eb019ef76e5bbdb426/src/render/Renderer.cpp#L1186-L1270),
and only then queues bottom layers, native blur and windows. The environmental
pass uses that boundary: borrow the original → update owned ping-pong textures
→ render into the native target if eligible and not publishing. Publication
adds an owned output-sized attachment followed by native composition; later
windows sample the attachment, never the active scene. This is one additional
full write/sample pair at full damage, plus its clear. Coordinated 1×1 signals
use GPU blits (4 bytes per cell copy) and at most 192 bytes of atlas storage
per output, not a fullscreen pass per window.

The normal untransformed close path is therefore isolated snapshot crop →
shader → native scene, instead of isolated crop → shader work buffer → native
texture pass. A mapped window remains native raster → shader work buffer →
native texture pass. With shader blur, color and coverage are both inputs to
the host's composition. On stock 0.57, staging coverage before publishing
the color also means drawing the rounded input instead of waiting for the
host's later matte callback. An arbitrary package's color and matte do not
necessarily sample the same input: the matte samples rounded native coverage
instead of the window color. The plugin does not fuse them through MRT or
replace native background acquisition.

The host's [MONITOR close snapshot](https://github.com/hyprwm/Hyprland/blob/0aa6745f705f2c4b0a5004eb019ef76e5bbdb426/src/render/Renderer.cpp#L3366-L3423) is a separate GPU framebuffer, not a client
buffer lease, so client teardown does not invalidate it. Its dimensions are
the entire output, not the window. Retaining the framebuffer is insufficient
to restrict arbitrary GLSL sampling to the approved crop; the bounded crop is
still needed. An image already equal to the approved rectangle is borrowed
without a copy. A close with incomplete offscreen source pixels is declined.

Paired private KWin/Hyprland measurements on an RTX 5060 Ti at 1280×960,
with a 440×300 client and blur disabled, showed median GPU close scopes of
11.07→2.46 µs for Fade, 24.06→18.62 µs for Diffusion, and 18.37→18.24 µs for
Particles. Those historical numbers used the pooled Particles path; the
current runtime can composite its ordered source-over draws directly. The
scope includes Hyprimate's close pass, not the whole compositor.
See [before](../build/shader-integration/frame-control-before/),
[after](../build/shader-integration/scene-proven/paired-after/), and the
[four-resolution executor run](../build/shader-integration/scene-proven/executor.jsonl).
4K Diffusion used 4.15 MB of mutable storage and five draws per step. The
executor did not allocate or upload after warm-up in these measurements.

The same 440×300 client was also tested on nested 720p, 1080p, 1440p, and
4K outputs. Native renderer CPU wall time from `RENDER_PRE` to
`RENDER_POST` had a p95 of 0.36–0.40 ms without an external effect and
0.80–1.28 ms across the three external effects. These results do not include
GPU completion, buffer commit, or scanout, and do not represent fullscreen
window effects. See `build/shader-integration/nested-{720,1080,1440,2160}/`
for per-effect p50, p95, p99, and maximum wall times.
The private wallpaper renderer scheduled zero frames during a 350 ms quiet
interval after reaching its finite endpoint, versus 21 frames for the
continuous effect at 60 Hz (0.40 ms renderer CPU wall p95 at 800×600).

Two matched 1280×960 closing runs compared `866c59dde2` and this runtime:

| Close GPU median, µs | Baseline repetitions | Current repetitions |
| --- | ---: | ---: |
| Fade | 2.62–3.52 | 2.46–2.53 |
| Diffusion | 18.82–20.86 | 18.82–19.14 |
| Particles | 17.09–20.19 | 8.45 |

The reactive water package also ran twice on 1080p, 1440p and 4K native
targets. Three moving quarter-output-width/height clients drove the
multi-window workload. The full output is shaded; simulations use a quarter
of each dimension. The wallpaper GPU scopes exclude other native passes:

| Primary output | Simulation | Multi-window GPU median, µs | Multi-window submission median, µs |
| --- | ---: | ---: | ---: |
| 1080p | 480×270 | 56.5–57.7 | 96.9–100.0 |
| 1440p | 640×360 | 91.7–92.0 | 96.7–100.7 |
| 4K | 960×540 | 199.2–199.6 | 104.1–107.2 |

Water uses two simulation draws plus one direct final draw at full damage,
two ping-pong allocations totaling 1.04/1.84/4.15 MB, and at most 3072 bytes of
interaction uniform uploads per active update. The original is borrowed, with
zero explicit texture-copy bytes. The native traces showed zero frames in
each 600 ms configured-idle and settled interval, and 90–91 renderer
invocations across two outputs in 750 ms in continuous mode. During the
multi-window run, the renderer wall-time p95 was 0.59–0.77 ms; the longest
sample was 0.84 ms. These are not GPU completion or presentation times. See
`build/shader-environment/{baseline-*,after-*,*-release-*}/` for measurements,
and `build/shader-environment/executor-final-environment.jsonl` for isolated
GLES workloads. These figures do not establish cross-vendor performance.

Preparation is still synchronous for changed packages; only the compositor
thread owns the context. In the initial paired nested run, package parse and validation
took approximately 0.15–0.59 ms, and changed GPU preparation took up to
28.30 ms. Reusing identical content and parameter values skipped GPU
preparation; reload callbacks in that sample took 0.37–1.23 ms for cache hits
versus up to 29.39 ms with changed content. These are CPU wall intervals, not
physical GPU allocation or presentation timestamps.
Environmental package preparation is also synchronous. The private runs
recorded up to 59 ms of GLES preparation on a driver-cold package. Cache
hits skipped GLES preparation. The renderer does not create a second context
or pause for compilation during ordinary interactions.

Keep simulation textures small, use ping-pong handles instead of copies, batch instances, and avoid full-resolution scratch passes unless the effect actually needs them. Initialization/compilation/reflection is separate from rendering. Production execution does not read simulation state back to the CPU or call `glFinish`. Statistics expose allocations, upload/copy bytes, pass counts and draws for profiling.

Build dependencies for this runtime are the existing GLES3/libpng stack plus the header-only `nlohmann_json` CMake package (3.11+). Focused verification targets are `shader-package-tests` and `shader-gles-tests`; `shader-runtime-bench` runs the original developer packages through the production executor. Measurements must distinguish CPU submission from GPU execution and exclude initialization unless explicitly reported.

The private nested integration runners load the specified build artifact and use
their own compositor sockets, home, configuration, and outputs. Give each run a
new artifact directory:

```sh
python3 tests/nested/shader_evidence.py build/shader-window-evidence --plugin build/hyprimate.so
python3 tests/nested/wallpaper_evidence.py build/shader-wallpaper-evidence --plugin build/hyprimate.so
python3 tests/nested/environment_evidence.py build/shader-environment-evidence --plugin build/hyprimate.so
EGL_PLATFORM=surfaceless build/shader-runtime-bench --environment
```

The first runner covers mapped and closing shader effects, shape-aware blur,
movement and resizing, workspace reversal, live privacy revocation and session
lock, and plugin unload/reload. The second checks wallpaper ordering, finite
and continuous presentation, and recreation and removal of a rotated,
fractionally scaled virtual output. Both retain screenshots and numeric checks.
Private compositor logs record `shader workspace final=direct` and
`shader wallpaper final=direct` for the primary output, and
`shader wallpaper final=pooled` for the rotated output. The checks also
compare the full exterior of deformed blur frames and verify that a reversal
frame contains visible content from both captured workspaces.
The environmental runner uses a dedicated SHM BACKGROUND root on each output,
observes pixels outside the window bounds, checks the GPU mailbox and output
draw receipts, and records image comparisons under `measurements.json`.
For renderer timings, build the test-only probe against the pinned Hyprland
headers and run a profile without screenshot capture:

```sh
g++ -std=c++26 -fPIC -shared $(pkg-config --cflags hyprland) \
    tests/nested/ShaderFrameProbe.cpp -o build/frame-probe.so
HYPRIMATE_FRAME_PROBE=build/frame-probe.so \
    python3 tests/nested/environment_evidence.py build/shader-environment-profile \
    --plugin build/hyprimate.so --profile --size 1920 1080
```

Pass `--interactive` to the first runner to also drive held move and resize
grabs directly against the private Wayland socket (requires NumPy for the shared
input fixture).
