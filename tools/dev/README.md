# Development toolchain

One entry point, `python3 tools/dev.py COMMAND`, drives the diagnostic builds. Python 3 is a
development dependency: the default CMake build, hyprpm install, and Nix package build do not
require it.
The install build still verifies that the plugin's imports are provided by Hyprland and its
libraries, using `nm`, `ldd`, and standard shell tools.

How isolation works: `tools/dev/CMakeLists.txt` is a wrapper project. It adds the unmodified root project as a
subdirectory and adds the `hyprimate-bench` target. `dev.py` configures it in `build/dev/<profile>/` and passes
sanitizer/coverage/trace flags on the command line, so a profile can never leak into `build/` (the production tree).
Reports are written to `build/dev/reports/<command>.json`, logs to `build/dev/logs/`.

The dev wrapper enables `BUILD_TESTING` by default. For tests configured directly from the
repository root, pass `-DBUILD_TESTING=ON`. After editing `config/settings.json` or its generator,
build the `update-config` target in a test-enabled build and commit the refreshed headers in
`config/generated/` along with any generated documentation changes. CTest's
`config-generated-check` rejects stale headers and documentation. The production build copies
the checked-in headers and does not run the Python generator.

Exit codes: `0` ok (advisories allowed) · `1` blocking finding (failing test, new blocking diagnostic, benchmark
regression, removed export) · `2` usage · `3` tool/environment failure (missing or crashed tool; says nothing about the
code). A defect wins over a tool failure when both occur; both appear in the JSON report.

## Commands

| Command | What it does | Typical cost | Agent-safe | Needs |
|---|---|---|---|---|
| `doctor [--for CMD]` | Probe tools, versions, Hyprland headers, `perf_event_paranoid`; prints install hints | 1 s | yes | – |
| `check [--no-plugin] [--tidy]` | Format ratchet, build tests + bench + plugin (default flags), `ctest` | 40 s warm, minutes cold | yes | cmake, ninja, clang-format |
| `analyze [--only a,b] [--all]` | clang-tidy (existing config), cppcheck (`check_unused.sh`) by default; `scan-build`, `iwyu`, `codeql` opt-in | ~2 min; scan-build +minutes | yes (read output) | clang-tidy, cppcheck; optional the rest |
| `sanitize [asan\|ubsan\|all]` | Test executables under ASan+LSan / UBSan (abort on first report), symbolized | build + ~1 min each | yes | clang, llvm-symbolizer |
| `memcheck [--only RE]` | Test executables under Valgrind Memcheck, XML in `build/dev/reports/memcheck/` | 10–50× slowdown | yes | valgrind |
| `coverage [--update-baseline]` | LLVM source-based coverage: text, HTML, lcov, JSON summary (lines, regions, branches, functions) | build + ~1 min | yes | llvm-cov, llvm-profdata |
| `bench run / compare / list` | Deterministic animation workloads; JSON with every sample; Mann-Whitney comparison | ~1 min | yes, on a quiet machine | – |
| `profile [--tool perf\|callgrind] [--scenario S]` | CPU hotspots, call graphs, HW counters (perf); instruction-level ranking (callgrind) | 1–5 min | yes | perf / valgrind |
| `memory-profile [--scenario S]` | Exact `operator new` counts per workload; heaptrack trace when installed | 1 min | yes | (heaptrack) |
| `build-profile [--core-only]` | Clean `-ftime-trace` build; slowest TUs, headers, template instantiations | clean build | yes | clang |
| `compare-codegen` | Baseline vs candidate binary: sections, symbols, exports, per-function code and stack frames, ABI, optional benchmarks | 2 builds | yes (verdict needs review) | llvm-objdump/readelf/nm |

`doctor --for COMMAND` shows what that command needs. Nothing is installed for you; hints are Arch package names.
Optional tools that are missing produce status `unavailable` (exit 0) unless `--require-all` is given.

## Which checks need what kind of environment

* **Everything instrumented runs the standalone test executables only** (`*-tests`, linked against `hyprimate-core`).
  That is the Hyprland-independent part of the code: liquid/edge/paper/cloth/swarm/bubble/smoke models and the window
  tracker, plus the EGL material tests on a surfaceless context. It is **not** compositor integration coverage:
  `main.cpp`, `PluginRuntime`, renderers, live styles and close passes link Hyprland and are exercised by no test.
* **The plugin is never sanitized or loaded.** ASan/UBSan runtimes must come first in a process and the host
  `Hyprland` binary is not instrumented; an unstable instrumented plugin must not run in your session. Use a nested,
  disposable compositor if full integration runs are ever needed.
* `perf` samples the unmodified RelWithDebInfo benchmark binary with DWARF unwinding (the production build omits frame
  pointers and is not rebuilt for profiling). `perf_event_paranoid=2` gives user-space samples only.
* **Callgrind/Memcheck numbers are instrumented, emulated costs.** Instruction counts rank functions; they do not
  predict native time. Only `bench` on the default build measures native time.

## Baselines, categories and policy

`analyze` and `check` classify findings against `tools/dev/baselines/*.json` (line-agnostic: file + check + message →
count, so edits above a finding do not read as "resolved + new"):

| Category | Meaning | Effect |
|---|---|---|
| existing | in the baseline | reported, never hidden |
| new | count above baseline | **blocking** for clang-tidy and format; advisory for cppcheck, scan-build, iwyu, codeql |
| resolved | count below baseline | reported; refresh the baseline to keep the gain |
| deferred | matches `policy.json` → `deferred` | counted, never blocks |
| approved | matches `policy.json` → `approved`, plus the `NOLINT` inventory | counted; `NOLINT` without an explanation is flagged |
| confirmed defect | sanitizer/valgrind report, failing test, any *new* clang-tidy `error` (the `WarningsAsErrors` set) | blocking |
| tool failure | exit 3 | not a code verdict |

Update a baseline only after reviewing the diff: `python3 tools/dev.py analyze --update-baseline tidy|cppcheck|all`,
`check --update-baseline format`. `git diff tools/dev/baselines/` is the review artifact.

Recorded state (see `recorded_at` in each baseline): taken at `b3a5bd6` **with a dirty working tree** (uncommitted smoke
work present). clang-tidy recorded 365 unique project diagnostics there (2 are existing `misc-const-correctness` errors in
the uncommitted smoke tests). The earlier-reported "110 warnings" baseline is not recorded anywhere in the repository
and does not match this tree; the committed baseline here is the measured one, and the discrepancy is stated rather than
reconciled. Likewise the earlier cert-err33-c report (25 printf/fprintf + 9 snprintf in `BubbleMaterial.cpp`) does not
reproduce at this commit (6 findings, all in tests; the `snprintf` result in `BubbleMaterial.cpp` is checked). The rule
is kept **deferred** in `policy.json` until its owner confirms; no call site was touched. `BubbleMaterial.cpp` is listed
under `frozen_files`; no tool edits sources. `check_unused.sh` predates the smoke shaders; `dev.py` builds all four
generator targets before it runs.

Format: 42 files do not conform to `.clang-format` at HEAD (972 violations); that is baselined as a per-file ratchet, so
new violations fail while existing ones are listed.

## Static analysis

* **clang-tidy**: `tools/check_clang_tidy.py` against `build/dev/clang/compile_commands.json` (clang, as the script
  requires). `.clang-tidy` is unchanged.
* **cppcheck**: `tools/check_unused.sh` unchanged. Findings of `missingIncludeSystem`/`checkersReport` are configuration
  noise and excluded from the baseline.
* **scan-build** (Clang Static Analyzer): `analyze --only scan-build`. The build tree is configured under scan-build's
  compiler wrappers (the real flags), analyzes `hyprimate-core` and `hyprimate` from a clean state, and
  writes HTML to `build/dev/reports/scan-build-html/`. `clang-analyzer-*` is also part of the clang-tidy policy, so
  scan-build mostly adds path-sensitive depth; expect overlap and treat it as advisory.
* **include-what-you-use**: `analyze --only iwyu` runs `iwyu_tool.py` on the compilation database and writes
  recommendations to `build/dev/reports/iwyu.txt`. It never edits files. `--keep=*hyprland/*` leaves Hyprland's own
  include layout alone. Review by hand; IWYU suggests internal headers of libstdc++ and Hyprland that are not part of
  our include style.
* **CodeQL**: `analyze --only codeql [--codeql-suite SUITE]` builds a fresh database from the real build and writes SARIF
  to `build/dev/reports/codeql.sarif` (default suite: `cpp-security-and-quality`; use
  `codeql/cpp-queries:codeql-suites/cpp-code-scanning.qls` for the smaller default set). It needs the local CodeQL CLI
  and its query packs (licence terms apply; GitHub's terms restrict CodeQL on some private code). There is no CI job for
  it on purpose: it is optional and must not hard-depend on a hosted service.

## Benchmarks

`bench run` builds `hyprimate-bench` (links `hyprimate-core` unchanged; the counting `operator new` exists only in that
executable) and times complete scripted workloads: `geometry.edge_resolve`, `edge.open_close`, `liquid.move_settle`,
`paper.close`, `bubble.open/close/move_settle`, `smoke.open/close/move_settle`, `cloth.record_open/playback/unshaded_mesh`,
`swarm.open/move_settle`. Inputs are fixed seeds and fixed 60/144 Hz steps; each scenario returns a checksum, so a
nondeterministic scenario fails the run and a baseline/candidate **output** difference is reported as `behavior-changed`
rather than as a speed result. Reported: median/p90/p99/max per frame, allocation count and bytes per run, all samples
in the JSON, plus machine metadata (CPU, governor, load) and noise warnings.

What they are not: these are CPU-side model costs of the Hyprland-independent code, in a tight loop with warm caches.
They are not frame times, not compositor performance, and say nothing about GPU work. `cloth.record_open` measures a
worker-thread simulation as wall time and is always reported `inconclusive` in comparisons. Allocation counts cover C++
`operator new` only (use heaptrack for direct `malloc` callers).
`cloth.unshaded_mesh` isolates construction of the unlit vertex buffer from a fixed projected sheet. It does not include recording, depth sorting or GPU submission.

Comparing: `bench run --name before`, change code, `bench run --name after`, then
`bench compare build/dev/bench/before.json build/dev/bench/after.json`. Verdicts per scenario: `improvement` /
`regression` need a median change ≥ 3% (`--threshold`), Mann-Whitney p < 0.01 and sample noise (coefficient of variation) ≤ 10% (`--noise-cv`); a large but unsupported shift is
`inconclusive`; otherwise `no-measurable-change`. Use a quiet machine (governor `performance`, low load); `dev.py` warns
when it is not. Interleave rather than run sequentially if you suspect drift: `compare-codegen --run-bench` does.

The [cloth replay](../../src/window/cloth/tests/ClothReplay.md) compares displayed motion,
contact response and handoff continuity against frozen reference traces. It is separate
from both the model throughput benchmark and the live compositor profiler.

## Live per-style profiling

The loaded plugin exposes an opt-in IPC command. Profiling is off by default; no
configuration changes or profiler dependencies are required:

```sh
hyprctl hyprimate:profile start
# Exercise window opening, closing, movement, and/or workspace transitions.
hyprctl hyprimate:profile report | jq
hyprctl hyprimate:profile stop > build/live-profile.json
```

Use `hyprctl -i INSTANCE ...` to select a disposable nested compositor rather than
your desktop. `start` starts a fresh session. `reset` clears samples and preserves
whether collection was enabled. `report` reads without clearing; `stop` freezes the
report. Loading the rebuilt plugin is required before these commands exist.

Reports group samples by domain, style and operation. Window setup distinguishes
`open`/`close`; mapped updates/drawing use `shared` because opening, movement and
residual dynamics can overlap. Workspace operations use `transition`.

* **CPU:** calling-thread CPU and elapsed wall time, separated into setup, update,
  draw and worker phases. Paper and Cloth worker simulation is measured on its
  worker thread. These are instrumented scope samples, not end-to-end frame times;
  do not sum percentiles or interpret samples as one refresh's total cost.
* **GPU:** asynchronous GLES `GL_EXT_disjoint_timer_query` measurements of actual
  render/capture commands, including offscreen work. Queries never wait for the
  GPU. Unsupported timers, disjoint epochs and dropped samples are explicit.
  Nested scopes or an existing caller-owned elapsed query are skipped. A series
  with no samples means unmeasured, not zero cost. Pending queries at stop are
  discarded; let several frames render before stopping to collect recent results.
  Capture scopes are inclusive: window GPU work nested inside a workspace capture
  is charged to that workspace scope, not separately to the window style. Timings
  are not additive. Run this as the sole GPU-timer profiler: GLES disjoint status is
  read-to-clear and cannot be safely shared with an independent timer collector.
* **Pacing:** per-output draw intervals and real Aquamarine presentation intervals
  associated with styles being drawn. Multiple instances/passes of the same style
  count once per output frame; offscreen captures do not count as display. Output
  presentation is shared by concurrent styles, not exact per-image scanout
  attribution. Idle gaps between separate effects are excluded. Intervals over
  1.5 refresh periods are late; missed-refresh counts are estimates, particularly
  with VRR and nested backends.
  Backends without presentation timestamps retain successful event counts and
  expose a separate `presentation_callback_intervals` series. Callback arrival is
  a scheduling proxy, never relabeled as hardware presentation time.
* **Memory:** sampled compositor RSS, virtual size, start baseline and RSS delta.
  RSS observed during a style is **shared process memory**, not that style's heap.
  Known per-animation resource peaks are separate lower bounds: window state,
  sampled mesh/publication/recording storage; workspace retained color attachments
  and sampled state/accounting buffers. Borrowed native textures and shared history
  are excluded. Driver overhead, depth/stencil, mipmaps, unsampled allocations and
  private renderer caches are not a complete heap or physical VRAM measurement.
  Previews created before profiling began can be absent from resource accounting.
  Resource samples are peaks for one animation, not summed simultaneous instances.

Sampling storage is bounded: up to 96 style/operation rows, 16 outputs and the most
recent 512 samples per series. Percentiles use retained samples; counts, means and
maxima cover the session. RSS is sampled at most every 250 ms plus report/stop.
Collector storage size is reported and excluded from the initial RSS baseline.
Report generation itself and instrumentation have overhead; collect on a quiet
machine and avoid repeatedly requesting reports during a measured animation.

For comparisons, use the same monitor resolution/refresh, window geometry,
settings and concurrency. Start a fresh session per style, repeat the same actions,
then save each report. An idle or plugin-disabled run measures compositor memory
baseline, but does not manufacture per-style timing samples.

The separate Planet Hop renderer profiler is documented in
[`docs/planet-hop.md`](../../docs/planet-hop.md#renderer-profiling). It is not a
replacement for live capture/compositing measurements.

## Binary and code-generation comparison

`compare-codegen --baseline REF [--candidate REF|WORKTREE]` exports each side with `git archive` (no worktree
metadata), builds `hyprimate` with the **same** compiler, flags, `RelWithDebInfo` optimisation and target through the
same wrapper, adding only `-ffile-prefix-map` so paths do not leak into the binaries, and then compares:

* ELF section sizes and content hashes (`llvm-readelf`), total size, `DT_NEEDED`;
* dynamic exports (`llvm-nm -D`): removed exports block; `abidiff` (libabigail) and `bloaty` run too when installed;
* per function (`llvm-objdump --symbolize-operands`, nops and data-address annotations stripped, hashed): instruction
  count, byte size, mnemonic deltas and the `sub rsp` frame size when the prologue is unambiguous (flagged unreliable
  with realignment or dynamic allocation);
* functions matching `policy.json` → `hot_functions` are listed first.

Verdict classes: **1** identical generated code · **2** different code, no regression demonstrated · **3** measurable
improvement · **4** measurable regression (blocking; zero-cost policy approval required) · **5** inconclusive. Without
benchmark evidence the best outcome for changed code is class 2: a larger frame or different assembly is a recorded
fact, not a slowdown. Add evidence with `--run-bench` (alternating baseline/candidate rounds, merged samples) or
`--bench-baseline/--bench-candidate` from earlier runs. Use `--baseline-bin/--candidate-bin` for prebuilt binaries
(you then own toolchain equivalence).

The earlier `resolve_edge_frame` frame growth (`0xF8` → `0x108`) is recorded in
`baselines/codegen-observations.json` and is attached to any report in which that function changed. It is not re-measured
here and has no performance evidence either way; scenario `geometry.edge_resolve` exists to supply it.

## Coverage

`coverage` builds with `-fprofile-instr-generate -fcoverage-mapping` (clang), runs every test executable with
per-process profile files, merges them, and writes `build/dev/reports/coverage/` (`summary.txt`, `summary.json`,
`coverage.lcov`, `html/`). Excluded: `_deps`, system headers, generated headers, test sources. No
threshold is enforced; `--update-baseline` records an informational baseline and later runs print the delta. Limits:
source-based coverage of `src/` only as reached by the standalone tests; plugin glue is absent; branch coverage counts
compiler-visible branches including template instantiations, so figures are not comparable with gcov.

## Troubleshooting

* `tool error: required tool not found`: run `doctor --for COMMAND` and install the named package.
* `cmake configure failed`: `pkg-config hyprland` must resolve (the root project requires it); see
  `build/dev/logs/configure-*.log`.
* ASan false positives from Mesa/EGL in the material tests: rerun with `--exclude material` and report the library, do not
  suppress project findings.
* Benchmarks noisy: set the CPU governor to `performance`, close other workloads, raise `--samples`.
* Unsupported `perf` counters (VMs): `perf stat` prints `<not supported>`; hotspots from sampling still work.

## CI

`.github/workflows/dev-check.yml` (pull requests, pushes to main): `doctor` + `check` in an Arch container (Hyprland
headers come from the `hyprland` package, so the container's Hyprland version must match the project's expected one).
`dev-analysis.yml` (manual/weekly): analyze, sanitize, memcheck, coverage, build-profile as independent, non-blocking
jobs. Performance measurement is not run in CI. These workflows have not been executed on GitHub from this checkout.
