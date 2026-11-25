# Portable compute evidence and benchmarks

This is a live evidence report, not a claim that the performance or integration criteria are complete. Results below describe individual runs and must not be generalized to other drivers, devices, or later builds. An offscreen microbenchmark is available; no controlled nested comparison or unused-feature regression result was accepted.

## Implementation and provenance

The raster route in [`SlibsBackend.cpp`](../src/compute/SlibsBackend.cpp) adapts the texture creation, input/output binding, draw and transfer sequence of [SLIBS `gpgpu_gles.h`](https://github.com/yui0/slibs/blob/ce1a1bf1186d201fc600b1d34bbd1f9ef217b143/gpgpu_gles.h), pinned at `ce1a1bf1186d201fc600b1d34bbd1f9ef217b143` (©2017–2023 Yuichiro Nakada). Its [per-file license table](https://github.com/yui0/slibs/blob/ce1a1bf1186d201fc600b1d34bbd1f9ef217b143/README.md) identifies that header as public domain. The separately public-domain [`gpgpu_glsl.h`](https://github.com/yui0/slibs/blob/ce1a1bf1186d201fc600b1d34bbd1f9ef217b143/gpgpu_glsl.h) (©2017–2018 Yuichiro Nakada) is corroborating reference, not another compiled backend. See [third-party notices](../THIRD_PARTY_NOTICES.md) for the mapping: the adaptation uses typed immutable textures, checked framebuffer attachments and multiple outputs, nearest sampling, edge clamping and a vertex-ID fullscreen triangle, rather than importing the original header or its context setup.

The [compute guide](compute.md) describes the API, field ABI and runnable examples: [standalone C++](../examples/compute/main.cpp), [stencil](../examples/shaders/compute-stencil/) and [particles](../examples/shaders/compute-particles/). Storage and precision requirements are discussed there with links to the GLES specifications; specification guarantees are not themselves measured driver results.

The original private input snapshot is `/tmp/hyprimate-compute-input.kODNGw/`: `HEAD` records `8269afa1ed3fc19de63d30259ada956561ae59d2`; `tracked-working.diff`, `untracked.sha256`, `source.sha256`, `configuration` and `source/` preserve that input and configuration. A later clean baseline was resealed at `/tmp/hyprimate-compute-clean-base.yHqwaC/` (`source/`, `snapshot`, `HEAD`, `files.z`, `source.sha256`, `prerequisite.diff`): exact HEAD plus the same required `PaperMaterial.cpp` `RawGlState.hpp` include as the candidate; its build directory is `/tmp/hyprimate-compute-clean-base.yHqwaC/build`. The sealed candidate is `/tmp/hyprimate-compute-candidate.qMQLXR/`; its shipping and separate benchmark-comparator builds completed, with snapshot configuration flags matching the clean baseline. No unused-feature baseline A/B is claimed yet; the accepted same-run CPU/native/SLIBS microbenchmark is reported below. The original dirty baseline plugin `/tmp/hyprimate-compute-base.ENDunn/hyprimate.so`, earlier candidate build `/tmp/hyprimate-compute.DOJD5w/`, and earlier CPU-comparator build `/tmp/hyprimate-compute-cpu.1d0xpL/` remain historical temporary artifacts. The recorded configuration uses GCC 16.2.1, CMake 4.4.5, Ninja, `RelWithDebInfo`, and Hyprland 0.57.0. All these local paths are temporary, mutable artifacts; record fresh hashes and source/configuration manifests for every measurement.

The recorded SHA-256 manifest `/tmp/hyprimate-compute-candidate.qMQLXR/artifacts.sha256` identifies these artifacts at capture time:

| Artifact | Path | SHA-256 |
| --- | --- | --- |
| Shipping plugin | `/tmp/hyprimate-compute-candidate.qMQLXR/build/hyprimate.so` | `9552b23dcc2a54587740964f72acbab7beaa05cff9ed80382175ebe6dea1e240` |
| Benchmark-comparator plugin | `/tmp/hyprimate-compute-candidate.qMQLXR/bench-build/hyprimate.so` | `e12f07e3b2a342ed29a3e01b78c93e55956b445114cb244ee7ea1dea072e9524` |
| Clean baseline plugin | `/tmp/hyprimate-compute-clean-base.yHqwaC/build/hyprimate.so` | `d2bfe09de1acbfff4c7e39ae1d93d26614b46a0e1da8e4bfe5fa206d0c78191c` |
| Microbenchmark executable | `/tmp/hyprimate-compute-candidate.qMQLXR/build/compute-bench` | `db9ea69f0584bec3aa330e0e4b792b89ba44e807d9ef793ea8781b2f833d0d96` |

The benchmark tool was subsequently rebuilt after benchmark-only fixes; re-hash the actual executable used in any later measurement rather than assuming the recorded hash still applies.

The initial nested run recorded the original dirty baseline SHA-256 `3f7efe87395899ea106e0f36868ce52a273091e76b2c63f8e1c5ae5022e426a4` and candidate SHA-256 `1cea60a0e4b6812bc3818bd9c93f484cbb587d83f8d69fe474aeac37368f1948` in `/tmp/hyprimate-compute.DOJD5w/evidence/provenance.json`; these identify files at that run, not necessarily current mutable paths. The old pair is not a matched unused-feature A/B and must not be used for that comparison. The newly resealed baseline and candidate are intended for the forthcoming comparison.

## Observed correctness and backend matrix

| Context and device | Route | Observation | Scope |
| --- | --- | --- | --- |
| Default GLES 3.2, NVIDIA RTX 5060 Ti | Auto, native, SLIBS | Compute CTest routes passed. | Standalone example and `compute-tests`; all three route selections exercised. |
| Exact Mesa GLES 3.0, llvmpipe | Auto, SLIBS | Compute CTest routes passed; forced native correctly rejected. | Forced-native rejection checked without resolving native compute entry points. |
| Exact Mesa GLES 3.0, without `GL_EXT_color_buffer_float` | Auto/SLIBS | Restricted-format compute CTest passed. | Confirms tested integer-backed field path does not require that float-render extension. |
| Exact Mesa GLES 3.1 | Auto, native, SLIBS | Compute CTest routes passed. | All three route selections exercised. |

The sealed shipping build passed 9/9 CTest cases: the standalone example, package example proof, four core compute lanes, CPU-only package tests and the default and exact-ES-3.0 shader tests. Its undefined-import check passed: no direct `glDispatchCompute`, `glBindImageTexture` or `glMemoryBarrier` imports were observed. The parent retained the full output at `local://52.log`. Subsequent work on the main tree includes nested tooling and benchmark-only timer-placement and missing-p fixes; the benchmark tool rebuilt successfully, while production files remain unchanged from the sealed 9/9 build. An accepted offscreen microbenchmark is recorded below; nested comparative timing and unused-feature A/B remain pending.

The verification report records 4/4 compute CTest cases passed in `/tmp/hyprimate-compute.DOJD5w`; core cases exercised 21 kernels per route and 64 dispatches per route, including typed arities/lanes, integer extrema, 7×5×3 volume, stencil, particles, numerical field and pixel checks, ownership/off-owner behavior, no-op resolver rejection, host-state restoration and warm counters. These results close the earlier missing-executable CTest-log limitation for this run, but do not establish all planned exotic state, lifecycle, replay, compositor or performance cases. The standalone example independently passed on default GLES 3.2/NVIDIA with native selected (four steps, 153 values checked by explicit offline readback, pixel `[68, 255, 0, 255]`), exact Mesa 3.0/llvmpipe with SLIBS, and exact Mesa 3.1 with native.

An additional current-worktree `compute-extended-state-tests` run passed 1/1 in `/tmp/hyprimate-compute.DOJD5w` (0.30 s; output `local://56.log`), separately from the sealed 9/9 artifact. On GLES 3.2 / NVIDIA 615.78.08 it exercised native and SLIBS devices and real `Program`/`Instance` consumers and replay. It verified fractional indexed viewport 0 and distinct slot 1, scissor and depth preservation; sample-mask 0 and clip-enable preservation with an actual MSAA draw, resolve and pixel; and active EXT window-rectangle rejection preserving fields and counters, followed by pool reuse and zero scrubbing. The run printed no `UNVERIFIED` cases for its supported optional paths. This is not a claim that every planned hostile-state or lifecycle case has been covered.

A separate later current-worktree core CTest run passed 4/4 affected lanes (0.62 s; `local://57.log`) after adding a test-only wrapped native `glCreateShader` fault. On NVIDIA GLES 3.2 and exact Mesa GLES 3.1, one device and batch executed native → SLIBS → native across three shared R32UI 7×5 fields, checking all 35 exact results, two native and one SLIBS route, and no inter-pass transfers. Exact GLES 3.0 reported this mixed-route case `UNVERIFIED` because native is unavailable; its ordinary strict 3.0 lane passed. This later test result is separate from the sealed 9/9 artifact; production code was unchanged.

A separate current-worktree core CTest run passed 4/4 lanes (0.59 s; `local://62.log`) with real allocation-failure and transform-feedback tests; production code was unchanged. A one-shot wrapped `glTexStorage2D` invalid-width fault verified failed allocation returns an error, retires the failed texture name before reuse, preserves old seeded field data/live bytes, and allows a successful retry that starts zeroed. An active unpaused transform-feedback session is declined without caller-state changes. Paused transform feedback without a query allows an actual GPU kernel to run with state preserved; paused active `GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN` query is declined while preserving the query, and a final-count sentinel remains exactly one. Auto/native/SLIBS were exercised where available across NVIDIA GLES 3.2 and Mesa GLES 3.1/3.0, including the no-float-render-extension case; native is unavailable on GLES 3.0. The prior ordinary dispatch counter rose 64→65 because the test includes the successful paused/no-query kernel.

A separate current-worktree `compute-extended-state-tests` run passed 1/1 in 0.21 s on NVIDIA GLES 3.2 with native and SLIBS (`local://63.log`). This stronger package-instance resize allocation-rollback proof initialized a relative 12×8 field and checked a real sampled value/pixel `(64, 48, 32)`. An injected real `glTexStorage` failure during resize preserved the old live bytes, field state and rendered pixel, and removed the failed texture name; an unfaulted retry resized to 24×16 and rendered `(64, 96, 64)`. The GL error assertion passed. Together with the device-level allocation-fault and transform-feedback CTest results above, this closes the reported reachable allocation-failure proof gap; production code was unchanged.

Shader lifecycle tests passed 2/2 on default NVIDIA RTX 5060 Ti and exact Mesa GLES 3.0. Four package example proofs passed real render checks for stencil and particles on both native and SLIBS: the stencil ran 256 warm steps and particles ran 32, with marker, source orientation, off-origin and pixel-oracle checks, and no GPU-to-CPU transfers during warm execution. These are package rendering proofs, not a substitute for private nested-compositor lifecycle and replay coverage. The plugin undefined-symbol check also passed with no new direct GLES 3.1 compute imports observed. These are reported results for the tested build; repeat affected evidence after rebuilding.

The sealed legacy shader regression run completed successfully (exit 0, 62.13 seconds) at `/tmp/hyprimate-compute-candidate.qMQLXR/build/legacy-final` using the sealed shipping plugin SHA-256 `9552b23dcc2a54587740964f72acbab7beaa05cff9ed80382175ebe6dea1e240`. All script assertions passed, including generation pinning and replacement, failed reload, blur/protected cases, post-effect background, move/resize/workspace/reversal, live protection and plugin unload. Receipts showed open isolation with 0 copies versus close with 1 copy / 539,904 bytes. This is legacy shader protection, distinct from the compute-specific nested proofs below.

Compute-specific generation rollback was separately verified on native and SLIBS with the unchanged sealed shipping plugin (both runs exited 0 in 7.77 seconds total; raw output `local://61.log`). Evidence is `/tmp/hyprimate-compute-candidate.qMQLXR/build/compute-reload-target-colors-{native,slibs}/observations.json`. At the same private package path, the test replaced the kernel with one referencing the undefined `rollback_missing_function` and reselected the path through the private configuration. An actual ES 3.00 compile failure was visible in the notification, including the step, error and unique fault name. Existing instance A remained on the old green generation and continued consuming the GPU-field-derived red result; new instance B also used that old valid green/red generation. After restoring the valid step and changing the marker, new instance C rendered magenta `(235, 25, 209)` while A and B remained green. GPU-consumer probes matched red `(242, 25, 5)` with mean error 0.333; prepare, retirement and instance counters were verified after reaping. The parent inspected failure and restored-generation images. This closes the compute compile-failure generation-rollback proof, not allocation-failure rollback.

The initial private nested evidence is `/tmp/hyprimate-compute.DOJD5w/evidence/summary.json` and its sibling per-case logs. It is explicitly incomplete and contains no successful results: all auto/native/SLIBS stencil and particle cases failed topology validation because the output was 1280×720 rather than the requested 1280×960; the exact-ES 3.0 nested cases failed compositor startup with exit -6. The crash report at `/tmp/hyprimate-compute.DOJD5w/evidence/es30-stencil/hyprlandCrashReport3889346.txt` (lines 169–185) identifies a host context-startup failure: DRMFD16 targets node128, Mesa `eglDeviceFromDRMFD` finds no DRM devices and falls back to GBM, then asserts `EGL: failed to initialize a platform display` in `OpenGL.cpp:177`. This is not a compute feature result. Capture-replay no-advance was also reported unverified. These failures are not proof of rendered output or a fallback pass. The separately observed exact-3.0 surfaceless example above must not be conflated with nested compositor coverage.

A later bounded GLES 3.0 startup diagnostic used child-only `GBM_BACKEND=dri` and did create a GLES context: Hyprland first failed at GLES 3.2, retried at 3.0, and reported Mesa 26.2.5 / llvmpipe (LLVM 23.1.3). This is context-startup evidence only, not a usable nested compositor or compute proof: the GBM buffer was null, allocation of a 1280×720 XR24 buffer failed, the swapchain failed, and output remained 0×0/refused to render. The accompanying log reports `DRM_IOCTL_MODE_CREATE_DUMB` permission denied and no virtual DRM device (only NVIDIA render nodes). See `/tmp/hc-es30-dri-gbm-gtx0bi89/private-hyprland.log` lines 969–973 and `hyprland.log` lines 71–74. Preserve the original startup failure separately; neither attempt establishes GLES 3.0 nested compute behavior.

The sealed shipping artifact also passed all six reachable hardware nested fixtures: stencil under auto, native and SLIBS in `/tmp/hyprimate-compute-candidate.qMQLXR/build/evidence-final-identity/summary.json`, and particle lifecycle under the same three routes in `/tmp/hyprimate-compute-candidate.qMQLXR/build/particles-explicit-outputs/{native,auto,slibs}/result.json`. The particle run took 116.69 seconds; the parent inspected opening and post-active-unload images, observing the marker/deformed source followed by the normal printed native client. An earlier particle failure was traced to checking secondary-output readiness before explicitly configuring its mode, not to a numerical mismatch; the harness now configures the mode before waiting for dimensions and starts the child with one output. These six fixture passes and the separate legacy pass do not establish usable exact-GLES-3.0 nested rendering or nested capture-replay dispatch observability. Core `Instance` counter and pixel replay checks did pass.

An initial microbenchmark attempt failed because its harness retained a delete-pending program. The benchmark fixture was corrected and rebuilt; the accepted run and its limits are documented below.

## Accepted microbenchmark: quiet-lane offscreen run

The accepted result is `/tmp/hyprimate-compute-candidate.qMQLXR/build/micro-verified.json` (126 rows). It used `/dev/dri/renderD128`, NVIDIA Corporation GeForce RTX 5070 Ti, GLES 3.2 NVIDIA 615.78.08, and a 64×64 final sampled target. Each row records 8 warm-up ticks and 120 measured ticks, with a one-tick fence completion boundary. All 126 rows report `oracle_validation=cpu_oracle_and_render_match`; each has 120 submitted and 120 retired samples and `timer_status=valid`.

Provenance: executable SHA-256 `aafd3c18f20e6f761867f9cb32f1aef41d25753e766a902adcaa58fe98aa4741` (`/tmp/hyprimate-compute-candidate.qMQLXR/build/compute-bench`); benchmark source SHA-256 `b801ab9588d122e4695c36eaea35cc302259acbdbcf7c00bb0cb78b7ab084373` (`/tmp/hyprimate-compute-candidate.qMQLXR/source/src/compute/tests/ComputeBench.cpp`). The matched plugin hashes are in `artifacts.sha256` above. Raw rows and timer data are retained at the JSON path above.

Representative warm tables below select dependent workloads: 256² stencil × 8 steps, 16,384 particles × 1 step, and 1/4/8 concurrent instances. Values are actual median / p95 / p99 in microseconds from the JSON. `Submit` is CPU-side tick submission, `GPU` is the asynchronous timer interval, and `Fence` is the one-tick completion boundary; these are distinct measurements, not interchangeable latency estimates.

#### CPU comparator

| Workload | Instances | CPU simulation | Tick submit (full comparator path) | GPU interval | Fence completion |
| --- | ---: | ---: | ---: | ---: | ---: |
| Stencil 256² × 8 | 1 | 470.813 / 512.842 / 524.581 | 493.312 / 537.901 / 552.620 | 45.120 / 45.792 / 46.208 | 548.830 / 593.629 / 609.298 |
| Stencil 256² × 8 | 4 | 1880.452 / 2028.386 / 2091.394 | 1959.829 / 2107.563 / 2174.941 | 175.072 / 176.224 / 176.544 | 2145.282 / 2292.136 / 2359.125 |
| Stencil 256² × 8 | 8 | 3765.263 / 4079.171 / 4218.346 | 3923.017 / 4236.906 / 4396.871 | 348.608 / 351.616 / 357.280 | 4289.404 / 4595.653 / 4757.317 |
| Particles 16,384 × 1 | 1 | 87.827 / 89.437 / 91.506 | 221.422 / 226.862 / 229.412 | 81.408 / 96.704 / 97.568 | 314.379 / 327.208 / 332.898 |
| Particles 16,384 × 1 | 4 | 347.307 / 357.147 / 362.497 | 878.729 / 906.867 / 921.616 | 320.512 / 376.864 / 381.568 | 1218.295 / 1265.584 / 1270.503 |
| Particles 16,384 × 1 | 8 | 691.465 / 707.844 / 708.744 | 1751.496 / 1763.456 / 1770.476 | 946.944 / 1579.936 / 1688.352 | 2227.109 / 2397.483 / 2454.071 |

CPU simulation is reported separately from the full comparator tick submission. The JSON does not expose an isolated CPU upload-plus-presentation duration: do not estimate it by subtracting independent percentile summaries. The full path includes final upload and sampled rendering; the fence and GPU timer include their distinct GPU boundaries.

#### Native and SLIBS GPU routes

| Workload | Instances | Route | Submit (median / p95 / p99) | GPU interval (median / p95 / p99) | Fence (median / p95 / p99) |
| --- | ---: | --- | ---: | ---: | ---: |
| Stencil 256² × 8 | 1 | Native | 25.230 / 26.949 / 29.498 | 35.072 / 35.296 / 35.360 | 70.917 / 72.468 / 75.157 |
| Stencil 256² × 8 | 1 | SLIBS | 54.298 / 58.108 / 60.077 | 42.528 / 42.688 / 42.816 | 108.096 / 112.526 / 115.696 |
| Stencil 256² × 8 | 4 | Native | 92.336 / 100.536 / 129.385 | 128.384 / 129.760 / 130.176 | 185.323 / 192.543 / 221.082 |
| Stencil 256² × 8 | 4 | SLIBS | 174.863 / 179.964 / 207.633 | 160.512 / 164.992 / 168.384 | 288.209 / 325.818 / 339.048 |
| Stencil 256² × 8 | 8 | Native | 181.263 / 193.363 / 195.203 | 280.992 / 282.784 / 283.264 | 348.197 / 350.907 / 352.877 |
| Stencil 256² × 8 | 8 | SLIBS | 335.008 / 341.058 / 345.318 | 340.576 / 378.176 / 380.224 | 489.103 / 503.002 / 505.382 |
| Particles 16,384 × 1 | 1 | Native | 10.680 / 11.610 / 11.829 | 16.128 / 31.648 / 32.032 | 36.149 / 51.798 / 52.818 |
| Particles 16,384 × 1 | 1 | SLIBS | 26.109 / 27.909 / 31.269 | 18.944 / 34.432 / 34.912 | 55.547 / 70.428 / 72.477 |
| Particles 16,384 × 1 | 4 | Native | 28.539 / 29.859 / 32.598 | 42.944 / 106.816 / 108.288 | 81.587 / 145.375 / 147.195 |
| Particles 16,384 × 1 | 4 | SLIBS | 59.068 / 64.487 / 72.367 | 58.272 / 122.688 / 124.480 | 130.346 / 193.673 / 198.093 |
| Particles 16,384 × 1 | 8 | Native | 53.878 / 59.908 / 64.117 | 80.160 / 207.392 / 210.208 | 145.664 / 271.770 / 276.720 |
| Particles 16,384 × 1 | 8 | SLIBS | 100.917 / 105.296 / 106.476 | 110.528 / 240.192 / 243.232 | 227.602 / 354.427 / 361.137 |

#### Cold setup and dependency sensitivity

Cold values are single initial-sample timings, not percentiles. Columns are parse / initialize / prepare / allocate-and-seed / first submit / first fence-complete, all in microseconds.

| Workload, 1 instance | Route | Cold parse / init / prepare / allocate+seed | First submit / first fence |
| --- | --- | ---: | ---: |
| Stencil 256² × 8 | CPU | 33.579 / 391.306 / 0.020 / 174.704 | 601.779 / 672.996 |
| Stencil 256² × 8 | Native | 25.909 / 1178.207 / 232.012 / 224.372 | 188.664 / 293.770 |
| Stencil 256² × 8 | SLIBS | 27.079 / 867.978 / 308.859 / 186.433 | 237.342 / 357.987 |
| Particles 16,384 × 1 | CPU | 14.629 / 875.059 / 0.020 / 242.411 | 375.617 / 500.102 |
| Particles 16,384 × 1 | Native | 17.750 / 554.160 / 161.614 / 495.012 | 152.444 / 292.559 |
| Particles 16,384 × 1 | SLIBS | 13.989 / 297.970 / 295.579 / 475.562 | 179.874 / 326.148 |

For 1 instance, the dependent-minus-independent median GPU interval difference was +17.120 µs (native) and −0.160 µs (SLIBS) for stencil, and +0.320 µs (native) and 0.000 µs (SLIBS) for particles. These are incremental differences between the tested sequences, not isolated or exact driver barrier costs; the CPU comparator differences were +0.064 µs and +0.320 µs respectively.

Warm statistics show zero compiles and zero allocations in every measured row. CPU mode reports upload bytes (for example, 31,457,280 bytes over 120 ticks for each 1-instance representative), with zero copy and readback bytes. Native/SLIBS rows report zero upload, copy and readback bytes; their results stay GPU-resident and are sampled for the final target. Native dispatches and barriers and SLIBS raster dispatches are reported per row in the JSON. The timing lane used the selected RTX 5070 Ti; no physical-display latency was measured.

At the tested sizes on this device, native had lower median submission and GPU interval than SLIBS for the selected stencil and particle cases; the CPU comparator separately incurs CPU simulation and uploads. This is a workload-bounded observation, not a universal route ranking or statistically established speedup. Very small work can favor avoiding dispatch overhead: the one-element dispatch baseline recorded 5.750 µs median CPU-mode submit, 8.790 µs native and 24.789 µs SLIBS. Treat that as one measured baseline, not a general crossover threshold.

The simple reproducibility invocation for this benchmark executable is:

```sh
EGL_PLATFORM=surfaceless /tmp/hyprimate-compute-candidate.qMQLXR/build/compute-bench \
  --render-node /dev/dri/renderD128 \
  --json /tmp/hyprimate-compute-candidate.qMQLXR/build/micro-verified.json
```

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

The nested control experiment ended unsuccessfully after 2814.35 seconds; its raw `/tmp/hyprimate-compute-candidate.qMQLXR/build/benchmark-final/summary.json` retains 96 same-binary controls: 64 compute controls across four cells, all with GPU timing available, and 32 idle controls. The shipping idle-pair CV was 0.0804082589 (within the ≤0.10 gate), but drift was 0.0330964865 (above the required <0.03), so the run stopped with no accepted comparisons. All 64 compute-control latency measurements were unavailable because the recorder lacked `WAYLAND_DEBUG=client`, required by its protocol-ready detection.

After that run, a recorder-only `WAYLAND_DEBUG=client` fix enabled a functional latency diagnostic. Its first attempt hit an ffmpeg broken pipe because the recorder lacked VFR decode flags and duplicated CFR frames; the successful second attempt used the construction-matched `fps_mode=passthrough`, `enc_time_base=1:1000`, and bounded threads. `/tmp/hyprimate-compute-candidate.qMQLXR/build/latency-vfr-diagnostic/result.json` records 71 lossless frames and one unique alignment: request at 421424045348844 ns, first changed frame at 421424056021785 ns, a 10.672941 ms request-to-first-change observation. The marker measured RGB (20, 235, 168) with mean error 0 at 421424390535095 ns. This is a functional, unreserved diagnostic, not a comparative/statistical sample; it is not physical-display latency and does not repair the earlier control data.

All 192 retained compute style-profile rows in the first failed control report estimated zero missed refreshes; the larger workload cell was not triggered. Idle CPU was approximately 0.00006 core fraction, or roughly 250 µs over the 4-second window, making a 3% difference roughly 7.5 µs.

The rebooked run stopped after 183.57 seconds and 16 same-binary one-instance stencil controls: `/tmp/hyprimate-compute-candidate.qMQLXR/build/benchmark-rebooked/summary.json` records CV 0.02680693638 (within the ≤0.10 gate), but drift 0.03149038312 (above the required <0.03). It contains no accepted comparisons. All 16 request-to-first-change latencies were measured after the recorder fixes, but are not an accepted comparative result. The planned 32-second idle window was not reached in this run; the timing lane was released.

The 4.1-second CPU sample versus 5.07-second profiler-span discrepancy was traced to GPU sampler teardown occurring before profiler stop. The harness now stops profiling after its final CPU/memory sample and before sampler teardown. Functional diagnostic `/tmp/hyprimate-compute-candidate.qMQLXR/build/profile-window-diagnostic/result.json` validates this boundary: CPU window 8.116338096 s, profiler span 8.132120690 s, 487 draws, GPU timing available, and 71 VFR frames with one unique alignment. Its request-to-first-change observation was 8.814592 ms; the marker matched RGB (20, 235, 168) exactly. This is a functional diagnostic, not an accepted timing-lane comparison. No production runtime defect is known from the allocation, transform-feedback or resize proofs.

Two further retained attempts also stopped at their unchanged control gates, without accepted comparisons. `/tmp/hyprimate-compute-candidate.qMQLXR/build/benchmark-extended/summary.json` stopped after 16 one-instance stencil controls: CV 0.1334641049 exceeded the ≤0.10 gate, although drift 0.0031762355 was below 0.03. `/tmp/hyprimate-compute-candidate.qMQLXR/build/benchmark-isolated/summary.json` retained 96 controls; all four compute control cells passed, but the baseline idle same-binary control failed with CV 0.0348021495 and drift 0.0310390528. Its gate receipt is `benchmark-isolated/idle-1-baseline-control-a-noise.json`. The same-binary idle medians were 0.005423% and 0.005260% of one core, a difference of approximately 52 µs CPU time over a nominal 32-second window; this is control noise, **not a measured feature regression**.

For the isolated attempt the child was pinned to CPU 5, ancillary work to CPUs 0–4 and 6–10, and the RTX 5070 Ti was initially quiet after the user's approved TTS pause; TTS was subsequently verified running and unpaused. The failed attempts remain available as evidence. No nested comparison or unused-feature A/B passed the gates, no timing run is pending, and no further timing is authorized after the scope form was cancelled.

## Missing measurements and interpretation

| Evidence | Status |
| --- | --- |
| Offscreen CPU/native/SLIBS microbenchmark, cold and warm | Measured in the accepted quiet-lane run above (126 validated rows; 120 submitted and retired samples per row). Nested workload comparisons and matched unused-feature A/B remain pending. |
| Dependent versus independent sequences, submissions, timer queries and transfer counters | Incremental offscreen sequence comparisons and counters are reported above; do not interpret them as exact driver barrier cost. Nested control runs failed their gates and yielded no accepted nested comparison. |
| Nested one/four-instance performance, frame pacing, compositor CPU and memory | No accepted comparison: retained control attempts failed their noise gates. No timing run is pending. |
| Unused built-in and unchanged-package baseline versus candidate | No accepted A/B: the isolated same-binary baseline idle control failed the drift gate, so no feature regression can be inferred. |
| Request-to-first-changed-frame and physical-display latency | Functional, unreserved diagnostics observed 10.672941 ms and 8.814592 ms request-to-first-change; the first control run's 64 attempts were unavailable, while all 16 rebooked stencil controls measured latency. No comparative/statistical latency result or physical-display latency measurement is available. |
| Remaining reachable correctness gaps and nested capture-replay dispatch observability | Reported reachable allocation-failure, transform-feedback and package-instance resize rollback proofs now pass. Nested capture-replay dispatch observability remains unverified; usable GLES 3.0 nested rendering is blocked by the host GBM/DRM limits described above. |

For each workload report cold parse/compile/allocation/first step separately from steady state, sample counts and distribution (median, p95 and p99), instance count and workload size. A CPU submission interval ends when commands are submitted; a fence completion at a batch boundary includes queued GPU work and waiting, not isolated dispatch or exact barrier cost. Asynchronous GPU timer queries measure GPU intervals only when available and non-disjoint. Nested present intervals and missed refreshes describe compositor pacing, not photons at a physical display. Never relabel one boundary as another.

Use matched, counterbalanced batches with same-binary controls; investigate noise or drift instead of discarding inconvenient samples. Report variability, statistical uncertainty and practical effect sizes; a nonsignificant difference is not evidence of equivalence. Do not combine software and hardware series. The tables above are descriptive results from this accepted quiet-lane run (120 samples per row), not independent counterbalanced batches or a statistical equivalence test; they do not establish nested compositor performance, unused-feature regression, or physical-display latency.

## Provisional route guidance

Persistent neighbor fields or particles consumed directly by a GPU draw are candidates for compute because they can avoid repeated host transfers; that is an architectural reason to test them, not a measured speedup. Small UI state, host-visible physics, or a workload already served by a cheap fragment pass may favor CPU or existing raster approaches. Choose native or SLIBS by measured workload, context capability and correctness; forced routes are useful for diagnosis, while auto selection can differ per kernel. Revisit these recommendations only after matched results and integration evidence are available.
