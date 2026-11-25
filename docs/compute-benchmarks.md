# Portable compute evidence and benchmarks

This is a live evidence report, not a claim that the performance or integration criteria are complete. Results below describe individual runs and must not be generalized to other drivers, devices, or later builds. No comparative timing data is available yet; no backend ranking or speedup has been established.

## Implementation and provenance

The raster route in [`SlibsBackend.cpp`](../src/compute/SlibsBackend.cpp) adapts the texture creation, input/output binding, draw and transfer sequence of [SLIBS `gpgpu_gles.h`](https://github.com/yui0/slibs/blob/ce1a1bf1186d201fc600b1d34bbd1f9ef217b143/gpgpu_gles.h), pinned at `ce1a1bf1186d201fc600b1d34bbd1f9ef217b143` (©2017–2023 Yuichiro Nakada). Its [per-file license table](https://github.com/yui0/slibs/blob/ce1a1bf1186d201fc600b1d34bbd1f9ef217b143/README.md) identifies that header as public domain. The separately public-domain [`gpgpu_glsl.h`](https://github.com/yui0/slibs/blob/ce1a1bf1186d201fc600b1d34bbd1f9ef217b143/gpgpu_glsl.h) (©2017–2018 Yuichiro Nakada) is corroborating reference, not another compiled backend. See [third-party notices](../THIRD_PARTY_NOTICES.md) for the mapping: the adaptation uses typed immutable textures, checked framebuffer attachments and multiple outputs, nearest sampling, edge clamping and a vertex-ID fullscreen triangle, rather than importing the original header or its context setup.

The [compute guide](compute.md) describes the API, field ABI and runnable examples: [standalone C++](../examples/compute/main.cpp), [stencil](../examples/shaders/compute-stencil/) and [particles](../examples/shaders/compute-particles/). Storage and precision requirements are discussed there with links to the GLES specifications; specification guarantees are not themselves measured driver results.

The private baseline input snapshot is `/tmp/hyprimate-compute-input.kODNGw/`: `HEAD` records `8269afa1ed3fc19de63d30259ada956561ae59d2`; `tracked-working.diff`, `untracked.sha256`, `source.sha256`, `configuration` and `source/` preserve the input and configuration. It includes original working changes, not just a clean HEAD. The recorded configuration uses GCC 16.2.1, CMake 4.4.5, Ninja, `RelWithDebInfo`, and Hyprland 0.57.0. Baseline plugin: `/tmp/hyprimate-compute-base.ENDunn/hyprimate.so`; candidate build: `/tmp/hyprimate-compute.DOJD5w/`; separate CPU-comparator build directory: `/tmp/hyprimate-compute-cpu.1d0xpL/`. These are temporary, mutable local paths, not permanent published artifacts. Record fresh artifact hashes and source/configuration manifests for every measurement.

The initial nested run recorded the baseline SHA-256 `3f7efe87395899ea106e0f36868ce52a273091e76b2c63f8e1c5ae5022e426a4` and candidate SHA-256 `1cea60a0e4b6812bc3818bd9c93f484cbb587d83f8d69fe474aeac37368f1948` in `/tmp/hyprimate-compute.DOJD5w/evidence/provenance.json`; these identify files **at that run**, not necessarily the current mutable paths. The baseline and candidate have not been established as matched for the unused-feature A/B: original dirty `HyprglassCompat` and test changes must be resealed or mirrored appropriately before timing. Do not interpret the recorded pair as an unused-regression comparison.

## Observed correctness and backend matrix

| Context and device | Route | Observation | Scope |
| --- | --- | --- | --- |
| Default GLES 3.2, NVIDIA RTX 5060 Ti | Native | Standalone `compute-example` passed. | Four steps, 153 values checked by explicit offline readback; sampled pixel `[68, 255, 0, 255]`. |
| Exact Mesa GLES 3.0, llvmpipe | SLIBS | Standalone `compute-example` passed. | Same example and checks; software result, not hardware performance evidence. |
| Exact Mesa GLES 3.1 | Native | Standalone `compute-example` passed. | Same example and checks; not a complete backend conformance suite. |

These example observations are reported by the verification run; they do not establish the full capability limits or the correctness of all workloads. Package tests also passed in that run. Full compute backend, state-restoration, lifecycle, replay and integration proofs remain pending. In particular, `/tmp/hyprimate-compute.DOJD5w/Testing/Temporary/LastTest.log` records missing `compute-tests` executables despite containing `Test Passed` lines; those entries are **not** executable test passes.

The initial private nested evidence is `/tmp/hyprimate-compute.DOJD5w/evidence/summary.json` and its sibling per-case logs. It is explicitly incomplete and contains no successful results: all auto/native/SLIBS stencil and particle cases failed topology validation because the output was 1280×720 rather than the requested 1280×960; the exact-ES 3.0 nested cases failed compositor startup with exit -6. Capture-replay no-advance was also reported unverified. These failures are not proof of rendered output or a fallback pass. The separately observed exact-3.0 surfaceless example above must not be conflated with nested compositor coverage.

## Reproduction and evidence collection

From the repository root, with prerequisites and the intended matched source snapshots available, the planned private build and targeted checks are:

```sh
B=$(mktemp -d /tmp/hyprimate-compute.XXXXXX)
cmake -S . -B "$B" -G Ninja -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
  -DHYPRLAND_EXECUTABLE=/usr/bin/Hyprland
cmake --build "$B" -j2 --target hyprimate shader-package-tests shader-gles-tests compute-tests compute-example compute-bench
ctest --test-dir "$B" -V --no-tests=error \
  -R '^(compute(-es30|-es31)?-tests|shader-package-tests|shader-gles(-es30)?-tests)$'
EGL_PLATFORM=surfaceless "$B/compute-example" auto
```

Run the separately registered `compute-es30-no-float-tests` as well when present. Strict CTest lanes use the harness's Mesa vendor and version environment; a skip or absent executable is not a pass. For a standalone constrained example, use the Mesa EGL vendor path and software rendering, explicitly selecting the context version and route:

```sh
env EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=true \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  MESA_GLES_VERSION_OVERRIDE=3.0 "$B/compute-example" slibs
env EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=true \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
  MESA_GLES_VERSION_OVERRIDE=3.1 "$B/compute-example" native
```

For nested evidence and the legacy regression lane, after preparing a matched baseline in `$BASE`:

```sh
/usr/bin/python3 tests/nested/compute_evidence.py "$B/evidence" \
  --plugin "$B/hyprimate.so" --baseline "$BASE/hyprimate.so" \
  --render-node /dev/dri/renderD128
/usr/bin/python3 tests/nested/shader_evidence.py "$B/legacy" --plugin "$B/hyprimate.so"
```

Inspect summaries, process logs and captured images; do not infer success from the existence of an evidence directory. Verify the actual renderer and device in each run. The planned microbenchmark invocation is:

```sh
EGL_PLATFORM=surfaceless "$B/compute-bench" --render-node /dev/dri/renderD128 --json "$B/micro.json"
```

These are reproduction/collection commands, not a statement that they have all succeeded. Coordinate a quiet CPU/GPU timing lane first; build baseline and candidate with matched compiler, configuration and inputs, avoid concurrent compilers or nested sessions during timing, and confirm that the offscreen and nested renderers refer to the same physical GPU. Preserve JSON, raw outputs, commands, artifact hashes, source snapshot, driver/context information and device identity. The planned nested `--benchmark` lane requires a separate test-only CPU comparator and matching GPU benchmark artifact; it must not add a user-selectable CPU backend to the shipping plugin.

## Missing measurements and interpretation

| Evidence | Status |
| --- | --- |
| CPU vs native vs SLIBS microbenchmarks, cold and warm | Not yet measured. Compare identical workloads and final sampled presentation; include CPU simulation alone and CPU plus final upload and presentation. |
| Dependent versus independent dispatches, barriers, submissions, GPU timer queries and memory/transfer counters | Not yet measured. Report query unavailability or disjoint samples rather than substituting CPU time for GPU time. |
| Nested one/four-instance comparisons, frame pacing, process CPU and memory | Not yet measured; initial nested correctness attempt failed as above. Verify workload and renderer before timing. |
| Unused built-in and unchanged-package baseline versus candidate | Not yet measured; current inputs are not a matched A/B. Include idle and verify zero warm compute activity. |
| Request-to-first-changed-frame and physical-display latency | Not yet measured. Nested monotonic screencopy timestamps can measure the former, not physical-display latency. |
| Full numerical, mixed-backend dependency, hostile-state, replay and lifecycle evidence | Pending; a standalone example is not a substitute. |

For each workload report cold parse/compile/allocation/first step separately from steady state, sample counts and distribution (median, p95 and p99), instance count and workload size. A CPU submission interval ends when commands are submitted; a fence completion at a batch boundary includes queued GPU work and waiting, not isolated dispatch or exact barrier cost. Asynchronous GPU timer queries measure GPU intervals only when available and non-disjoint. Nested present intervals and missed refreshes describe compositor pacing, not photons at a physical display. Never relabel one boundary as another.

Use matched, counterbalanced batches with same-binary controls; investigate noise or drift instead of discarding inconvenient samples. Report variability, statistical uncertainty and practical effect sizes; a nonsignificant difference is not evidence of equivalence. Do not combine software and hardware series. No relative workload table can be populated honestly until the corresponding controlled runs exist.

## Provisional route guidance

Persistent neighbor fields or particles consumed directly by a GPU draw are candidates for compute because they can avoid repeated host transfers; that is an architectural reason to test them, not a measured speedup. Small UI state, host-visible physics, or a workload already served by a cheap fragment pass may favor CPU or existing raster approaches. Choose native or SLIBS by measured workload, context capability and correctness; forced routes are useful for diagnosis, while auto selection can differ per kernel. Revisit these recommendations only after matched results and integration evidence are available.
