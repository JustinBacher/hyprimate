# Native shader effects — specification 1

Hyprimate loads declarative effect packages written for **OpenGL ES 3.0 and GLSL ES 3.00**. Packages are trusted executable GPU workloads, not sandboxed content. Install only packages you trust. Allocation budgets and pass limits do not bound shader execution time.

There is no Burn My Windows compatibility layer, JavaScript runtime, compute shader support, shader translation, Vulkan backend, or private EGL context. Ported effects must adapt their source, helper functions, parameters, assets, and setup to this specification. No third-party effects are bundled.

## Select opening and closing effects

Existing `window.style` and all native style options retain their meaning. External effects independently override opening and closing, not movement or resizing:

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
} } } })
```

An empty `effect` selects the existing native style for that event. Existing `enabled` switches still apply. Parameters are a JSON object, checked against the selected package; unknown parameters and wrong types are errors. Reload configuration to reread a package at an unchanged path. A failed replacement must not invalidate an existing valid prepared effect or an active animation. Errors identify the package/pass and are reported without repeatedly compiling every draw.

Mapped presentation has one owner. An eligible native move or resize interrupts an external opening effect and hands the same motion sample to the configured built-in style; the motion is not discarded or deferred until the shader finishes. Hyprland's native opening fade remains active so a failed shader draw can return to the correct native raster.

The current integration conservatively declines external effects when native background blur could require an independent geometric matte. Shader RGB is never treated as blur coverage, and the runtime never disables your blur configuration automatically. A fading window can qualify for blur even if its client image is otherwise opaque. Use a native per-window no-blur rule, or explicitly disable native blur, to opt that window into external effects. Correct shader-aware blur coverage is not implemented.

The specification recognizes `open`, `close`, `move`, `resize`, `workspace`, and `wallpaper` event declarations. **Only opening and closing are currently integrated with Hyprland.** Declaring another event does not install that integration. No Niri integration is included.

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

The runtime supplies the vertex shader and fullscreen triangle. It does not allocate an internal full-resolution image for this simple pass. Hyprland's window-transform hook requires a pooled result framebuffer; the reusable GLES executor itself can render into a caller-provided target directly.

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

`hypr_uv` spans [0,1] across the current target, with texture row zero treated as the top. `hypr_window_uv()` maps it through `hypr_domain`; it can be negative or exceed one for expanded final bounds. Intermediate texture passes use a [0,0,1,1] domain.

`hypr_sample_source(uv)` samples window-normalized coordinates, respecting the source subrectangle and texel centers, and returns transparent black outside [0,1]. It is available in vertex shaders too, using explicit level zero sampling. Source images and shader output use **premultiplied alpha**. `hypr_unpremultiply()` and `hypr_premultiply()` are provided for straight-color arithmetic. Auxiliary PNG assets are raw RGBA samples; apply the appropriate alpha convention yourself.

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

Immutable compiled programs and assets are separate from mutable instance state. Concurrent animations never share writable simulation textures or buffers. Existing compatible storage is reused across repeated draws. Resizing changes resource dimensions, clears/reset state, and replays initialization; effects must not assume that state survives a size change. Interruption and teardown retire owned objects only while the owning compositor context is usable, or abandon lost-context handles without calling into a dead context.

The reusable executor accepts borrowed source textures and final render targets. The compositor integration is responsible for source provenance, permissions, session state, resource lifetime, coordinate conversion, damage, and host rendering-state preservation. A full-monitor snapshot must not be exposed as a window sampler: when an exclusive window texture is unavailable, the integration must GPU-crop approved content before exposing it, even though this requires a copy. The runtime never grants arbitrary compositor texture handles to packages.

An owned close is declined before native suppression if the retained snapshot does not contain the complete window capture. Missing offscreen pixels are not synthesized from neighboring desktop content.

The GLES target interface also accepts a nonzero framebuffer-region origin. Viewport and scissor constrain both clearing and drawing to that region, preserving surrounding compositor pixels. `hypr_target_size` and `hypr_uv` remain region-local; GLSL `gl_FragCoord` retains actual framebuffer coordinates.

The caller marks private pooled targets with `Target::clear_before_render`. The executor initializes only the target region before its first final pass, combining this with that pass's declared clear when possible. Later compositing passes retain earlier results. Hyprland always enables this for its private effect work buffers: `clear:false`, blending, or shader `discard` cannot reveal another work-buffer user's old pixels. An explicitly initialized in-place target may instead preserve its caller-provided contents.

## Budgets and failure behavior

Packages are bounded to 32 passes, 32 iterations per pass, 16 textures, 16 buffers, 16 meshes, 64 parameters, and 128 MiB of mutable per-instance storage including both ping-pong sides. A buffer is limited to 32 MiB; a pass is limited to 4,194,304 vertex invocations. Target images are limited to 8192 per dimension and 16,777,216 pixels; declared fixed simulation images and PNG assets to 4096 per dimension. A manifest is limited to 64 KiB, each shader/helper to 1 MiB, assembled shader source to 4 MiB, each encoded PNG to 16 MiB, and aggregate decoded assets to 64 MiB. Actual driver limits may be lower.

Malformed source/metadata, unsupported formats, allocation failures, invalid dependencies, privacy restrictions, and unavailable contexts cause an explicit failure and native fallback. The runtime does not silently replace a requested format or introduce an expensive CPU simulation fallback. These checks do not protect against deliberately nonterminating or excessively expensive trusted shader code.

## Architecture and performance

- `src/shader/Package.*`: native specification, loading, parameter validation, source assembly, resources and pass ordering; no compositor or EGL dependencies.
- `src/shader/GlesRuntime.*`: reusable GLES3 execution within the caller's context; no compositor or EGL ownership.
- `src/window/shader/`: Hyprland event and rendering integration using its existing lifecycle and framebuffer pool.

A future compositor integration must be implemented against its native renderer and ownership rules. These modules do not assume that Niri can reuse Hyprland's capture or hook architecture.

Keep simulation textures small, use ping-pong handles instead of copies, batch instances, and avoid full-resolution scratch passes unless the effect actually needs them. Initialization/compilation/reflection is separate from rendering. Production execution does not read simulation state back to the CPU or call `glFinish`. Statistics expose allocations, upload/copy bytes, pass counts and draws for profiling.

Build dependencies for this runtime are the existing GLES3/libpng stack plus the header-only `nlohmann_json` CMake package (3.11+). Focused verification targets are `shader-package-tests` and `shader-gles-tests`; `shader-runtime-bench` runs the original developer packages through the production executor. Measurements must distinguish CPU submission from GPU execution and exclude initialization unless explicitly reported.
