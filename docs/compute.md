# Portable GPU compute

Use a field when an effect's simulation has persistent, independently writable elements and a later GPU pass can consume the results. The kernel runs in the same GLES context as that pass. It needs neither an extra context nor CPU readback. Existing effects do not opt in automatically.

## Run the examples

Install or point your Hyprland effect settings at either of the v1 packages in `examples/shaders/`. Use absolute paths in your configuration:

```lua
local examples = "/absolute/path/to/hyprimate/examples/shaders/"
hl.config({plugin = {hyprimate = {
    window = {
        open = {effect = examples .. "compute-stencil/effect.json", effect_duration = 850,
                effect_parameters = '{"diffusion":0.20}'},
        close = {effect = examples .. "compute-particles/effect.json", effect_duration = 850},
    },
}}})
```

The stencil also declares `wallpaper`; set `wallpaper.effect` to the same manifest and `wallpaper.effect_duration = 0` for a continuously evaluated, per-output example. The two asymmetric impulses diffuse away; select a new effect generation to restart them. See [the shader guide](shaders.md) for selection, reload, capture and wallpaper behavior. The particles example advances 16,384 seeded positions and velocities and draws instanced triangles by fetching the new fields in the vertex shader. Neither package downloads simulation data.

The standalone example builds with the existing test EGL dependencies. From the repository root:

```sh
B=$(mktemp -d /tmp/hyprimate-compute.XXXXXX)
cmake -S . -B "$B" -G Ninja -DBUILD_TESTING=ON
cmake --build "$B" -j2 --target compute-example
EGL_PLATFORM=surfaceless "$B/compute-example" auto
```

[`examples/compute/main.cpp`](../examples/compute/main.cpp) shows the complete C++ lifecycle: supply a resolver and owner-current check, prepare a kernel, allocate fields, upload initial data, then open a batch with a `KernelSet` span of references to its prepared kernels, dispatch a feedback chain, sample its result into an off-origin color target, and release in the owner context. The example uses one explicit offline readback to compare all elements, not to implement presentation. `native` and `slibs` arguments force a route. The Hyprland adapter instead reads `HYPRIMATE_COMPUTE_BACKEND=auto|native|slibs` from the compositor process environment when it prepares a compute package. Forcing a route fails if unavailable; it does not silently select another. Unset defaults to `auto`. None of these settings changes how the context is created.

## Fields and passes

Example from [the stencil manifest](../examples/shaders/compute-stencil/effect.json):

```json
"fields": {
  "energy": {"type": "float", "domain": [256, 256], "ping_pong": true, "lifetime": "persistent"}
}
```

Specify **exactly one** of a fixed positive integer `domain` of one to three axes (missing axes are 1), or `scale` for a capture-relative XY domain using the texture sizing rule (Z is 1). Types are `float`, `vec2`–`vec4`, `int`, `ivec2`–`ivec4`, `uint` and `uvec2`–`uvec4`. Fields and textures share one resource namespace and resource limit. Fields default to persistent, without ping-pong. When ping-pong is enabled, input `.read` and output `.write` name distinct sides; successful iterations publish the new side once. A transient field must have an earlier producer in the same phase before it is read.

The stencil update uses this pass:

```json
{"name": "step", "phase": "update", "kernel": "step.glsl", "iterations": 8,
 "inputs": {"previous": "energy.read"}, "outputs": ["energy.write"]}
```

Only `initialize` and `update` may run kernels. Only private fields may be kernel inputs and outputs. Raster sources, mesh, blocks, blend, clear and feedback declarations do not apply to kernels. Input aliases determine the available read helpers; ordered output references determine entry arguments. Outputs must have equal logical domains. Inputs may have different types and sizes. A kernel cannot read and write the same field side. Ordinary render passes may read fields but cannot write them, and fields cannot be used as environment state or emissions. A package must still render `output`; standalone compute does not require a presentation pass.

## Kernel ABI

Write GLSL ES 3.00-compatible arithmetic and helpers and one entry point. From [the stencil step](../examples/shaders/compute-stencil/step.glsl):

```glsl
void kernel(ivec3 id, inout float energy) {
    float c = hc_read_previous(id);
    float left = hc_read_previous(id - ivec3(1, 0, 0));
    float right = hc_read_previous(id + ivec3(1, 0, 0));
    float down = hc_read_previous(id - ivec3(0, 1, 0));
    float up = hc_read_previous(id + ivec3(0, 1, 0));
    energy = c + diffusion * (left + right + down + up - 4.0 * c);
}
```

Each call sees a valid logical output coordinate. Outputs start at typed zero, including when the kernel returns early. The wrapper stores them once after return. `hc_domain` is the output domain; `hc_index(id)` gives its x-fastest linear index. Each input has `hc_size_ALIAS` and two overloads of `hc_read_ALIAS`: `ivec3` coordinates and a signed linear `int`. Both give typed zero outside the input's own domain. In a render pass, the same helpers are injected for field bindings in both stages. For example, [`particles.vert`](../examples/shaders/compute-particles/particles.vert) uses `hc_read_position(gl_InstanceID)` and `hc_read_velocity(gl_InstanceID)`.

Package parameters appear as uniforms under their declared names. A manifest `bool` is an `int` 0/1 inside a kernel; use `enabled != 0`. The frame uniforms are `hypr_delta`, `hypr_elapsed`, `hypr_progress` (float), `hypr_iteration`/`hypr_iterations` (int, zero-based iteration), and `hypr_seed` (vec4). Kernels do not inherit other package shader bodies or arbitrary renderer resources.

No preprocessor directives, stages or resources may be declared in the kernel body. `gl_` builtins, raw internal `hc_` names, samplers/images, derivatives, atomics, shared buffers, barriers, scatter writes, and backend-specific builtins are not portable. Put ordinary helper functions in the kernel file. A bad entry or token reports the kernel and offending line; driver errors name the stage and include its log. The body is checked as ES 3.00 even when the selected implementation compiles it into an ES 3.10 compute shader. These checks describe a language boundary, **not a watchdog or sandbox**: only run trusted sources, and keep work per element bounded.

## Coordinates, data and lifetime

For logical `(x, y, z)`, the index is `i = x + X * (y + Y * z)`. The physical backing uses one immutable 2D texture. When natural rows fit, it uses width `X`; otherwise it packs into longer or shorter rows and pads the last row. Storage row zero is the GL bottom row. Do not infer a field's geometry from window UV or a packed texture's physical extent. Use the logical helper, and explicitly map top-first coordinates if your effect uses them. Raw integer storage must not be read with ordinary `sampler2D` interpolation.

Scalar fields use `R32I` (signed) or `R32UI` (unsigned/float); vector fields use the equivalent `RGBA32I`/`RGBA32UI`, with unused lanes zero. Float lanes use `floatBitsToUint` and `uintBitsToFloat` at the storage boundary, not UNORM quantization and not a float color attachment. Existing v1 float textures have not changed. Portable requirements are deterministic, defined integer operations and finite highp float arithmetic; there is no promise of matching NaN payloads, signed zero, subnormals, or transcendental results bit for bit. The format/precision requirements come from [ES 3.0 tables 3.13 and framebuffer rules](https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf), [ESSL 3.00 §8.3](https://registry.khronos.org/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf) and [ES 3.1 image format and dispatch rules](https://registry.khronos.org/OpenGL/specs/es/3.1/es_spec_3.1.pdf); actual candidates and formats are probed on the supplied driver. No `EXT_color_buffer_float` requirement is imposed on fields.

`Device::capabilities()` exposes context/runtime limits, candidate availability and failure reasons, and format support. `Kernel::backend()` identifies the selected route for that descriptor, not a device-wide promise; `fallback_reason()` explains an automatic fallback. `Field::layout()` exposes width, height, logical bytes and actual padded storage bytes. Each axis/physical extent and byte product is checked before GL. Physical textures are capped at 8,192 per dimension and 16,777,216 texels including padding; use the reported device limit when smaller. Package field storage (both ping-pong sides and padding) is charged to the same 128 MiB per-instance budget as other resources.

The C++ handles are move-only and tied to one live device generation, owner context and thread. Use one batch for a consecutive sequence, then let it end before returning to ordinary GL draws. After a successful dispatch, swap the `FieldPair`; a failed dispatch must not advance it. A single field suffices for independent full overwrites. Allocated or re-leased fields start at zero. Release in the owner context or abandon without touching GL on context loss. A new context cannot adopt old names.

CPU uploads and blocking readbacks use exactly `X * Y * Z * lanes * 4` host-native bytes, x fastest, without storage padding. For GPU-to-GPU duplication between matching fields, use `Device::copy`; do not copy to present a field. `Device::sample` establishes texture visibility and returns a read-only level-zero view for an immediate same-context draw. Bind the view as `hc_tex_ALIAS`, set `hc_size_ALIAS` to the logical domain, and compile its helpers with `sample_source(alias, type)`. Do not retain the name across writes, swaps, pool return or release. For instance replays, package draws sample the last committed field state and do not advance it.

## Choosing a route

Compare steady-state submission and GPU completion separately from first compile/allocate. Dependent GPU steps can incur visibility barriers; a CPU comparator must include the upload and identical sampled presentation if its output is drawn. Tiny fields, UI state, CPU-visible physics or kernels already covered by a cheap ordinary fragment pass may not justify GPU dispatch. Large persistent neighbor fields and GPU-presented particles avoid repeated CPU/GPU transfers. Measure the actual compositor, workload size, instance count and driver rather than treating one backend as universally faster.
