# Cloth solver replay reference

`ClothReplay.cpp` is a development-only deterministic trace driver for the public `ClothFlutter` model API. It writes display-time samples to CSV on stdout and setup/open-recording wall times to stderr. `compare_cloth_replay.py` compares a candidate CSV to the frozen baseline without requiring Jolt in the candidate build.

## Frozen baseline

- Source: HEAD `3e21e5f28d331ac773ac9f6726375b60d15bf7ba`, recorded in `build/cloth-jolt-reference/revision` before the solver cutover.
- Frozen core archive: `build/cloth-jolt-reference/libhyprimate-core.a` (SHA-256 `5d1b38b4d8075fe38265e4fe421b0f6dcd9043801b04b5fa9379482923cebfb6`).
- Jolt 5.6.0 archive: `build/_deps/joltphysics-build/libJolt.a` (SHA-256 `bb328d3353f23d228c6731f7bb328808fc538ce55940267084a9a5fc32345212`).
- The baseline binary is linked only to these frozen core/Jolt archives. Do not rebuild them from the post-cutover worktree.
- Tracked compressed display-metric fixture: `cloth-jolt-reference.csv.gz`.
- The frozen Jolt capture was repeated and its uncompressed CSV matched byte-for-byte (`cmp`).

The native solver still uses the 57×57 grid and eight substeps per 120 Hz tick. The
island labels and hinge-tip bits in `../ClothSolveOrder.hpp` encode the reference's
constraint partition and arithmetic order, not animation samples. They were extracted
from the fixed rest grid after `Optimize()` in the pinned Jolt version: label each
vertex by its disjoint group (otherwise seam group `d`); for each outgoing horizontal,
mesh-diagonal, and vertical hinge, store whether its two tip vertex indices are in
descending order. The native solver reconstructs the constraints from the mesh and
builds levels that preserve the relative order of constraints sharing a vertex.
This ordering, contact timing, and float evaluation matter: changing them changes
how folds propagate even when the nominal material coefficients are unchanged.

For a future compute backend, independent islands can run concurrently and
synchronize before the seam passes. The sleeve is a separate small ordered solve;
its columns supply pinned targets and do not receive forces back from the sheet.
Measure the cost of synchronization and GPU-native mesh consumption before adopting
compute. The CPU solver has no graphics or compute requirement.

Rebuild the standalone baseline replay with one compiler/linker invocation:

```sh
c++ -std=c++26 -O2 -g -DNDEBUG -pthread -I src \
  src/window/cloth/tests/ClothReplay.cpp \
  build/cloth-jolt-reference/libhyprimate-core.a \
  build/_deps/joltphysics-build/libJolt.a \
  -o /tmp/cloth-replay-jolt
```

Capture the compact gzip fixture; the variable wall-clock sidecar stays local under the build directory:

```sh
mkdir -p build/cloth-jolt-reference
set -o pipefail
/tmp/cloth-replay-jolt 2> build/cloth-jolt-reference/cloth-jolt-reference-timing.txt \
  | gzip -n > src/window/cloth/tests/cloth-jolt-reference.csv.gz
```

For a candidate wired as `cloth-replay` in a build directory:

```sh
cmake --build build -j 2 --target cloth-replay
build/cloth-replay > /tmp/cloth-replay-candidate.csv
python3 src/window/cloth/tests/compare_cloth_replay.py \
  src/window/cloth/tests/cloth-jolt-reference.csv.gz \
  /tmp/cloth-replay-candidate.csv
```

The replay uses fixed seeds and logical geometry. It covers 0.4/0.8/2.0-second opening durations (flat, medium, and strong flutter, including a portrait aspect ratio); horizontal and vertical drags with reversals; resize and live springiness/flutter/gravity retuning; 120Hz and 60Hz movement input; movement-to-close handoff; and clear/obstructed closes at all four monitor edges. Movement publication is deliberately paused at the expected snapshot count before each input and resumed only until that count is reached. Transitions wait for full recording before display-time playback samples. Wall-clock timing output is informational and excluded from numeric parity.

## Metrics and comparison envelope

Samples include fixed display timestamps, lifecycle/opacity markers, projected sheet bounds, normalized depth range, normalized hem-to-header lag, header spread and endpoints, mean and RMS velocity, a center sample and its velocity, header-order crossings, obstacle-AABB vertex occupancy/penetration proxy, first fully-outside marker, and per-frame blocked-versus-clear pose delta. `active_end`, `closed`, full-exit, and obstacle-response onset are compared within one sample interval; for the 60Hz trace that interval is 1/60s. Sample key/schema changes fail.

Per-sample numeric tolerances are fixed in `compare_cloth_replay.py`:

- Projected bounds, center/hem coordinates, depth, spread, and header endpoints: 0.01–0.02 normalized sheet units.
- Mean, center, and RMS velocities: 0.05 normalized units/second.
- Opacity: 0.01; obstacle-AABB occupancy: 3 vertices; maximum AABB overlap depth: 1 logical pixel.
- Header-order crossings are a hard invariant: zero in every sample. Each blocked-close trace must differ from its clear twin by more than 0.005 normalized units at some sample. Movement-to-close handoff must preserve pose within 1e-6 normalized units and velocity within 1e-5 units/second. Open traces must settle inactive at opacity 1; close traces must finish closed at opacity 0.
The blocked obstacles are adjacent side walls that touch, but do not overlap, the window and are outside that edge's exit corridor; baseline maximum paired-pose deltas were Left 0.105485, Right 0.145042, Top 0.013968, Bottom 0.040465 normalized units.

These are pre-integration engineering gates, not paired-clip-validated perceptual guarantees. Keep the limits unchanged during solver tuning; if baseline captures or paired compositor review show they are not meaningful, Main must agree on a replacement envelope before tuning against it.

## Applicability gaps

The public API has no accessor for raw solver-tick recordings, exact worker tick counts/timing distributions, the internal `outside_frame`, fallback-use state, or collision contact events. Therefore this fixture compares **displayed** states at fixed timestamps (plus controlled movement snapshot publication), not raw vertex-by-vertex solver ticks; full-exit timing is inferred from displayed vertex positions. Obstacle-AABB overlap is only a geometric proxy; the paired blocked/clear pose delta is the actual observable response to the retained obstacle, not a contact-depth measurement. The API exposes displayed velocity, including the movement-to-close handoff samples.

This replay cannot measure compositor presentation/frame times, startup-to-first-visible frame, multi-window load, missed live compositor publications, or visual artifacts hidden by renderer/shading/fallback. It does not replace native Hyprland A/B clips or compositor lifecycle checks. Timing lines capture setup-to-first-publication and complete recording wall time only; no stable p50/p95/p99 tick metric is available through this interface.

## Cutover verification

The native CPU solver passed the frozen 1,874-sample comparison. The largest
observed error across the sampled projected bounds, fold depth, hem lag, and
header spread was 0.00039 normalized sheet units.

On the same Hyprland 0.57.0 build, a private 60 Hz nested compositor exercised
open, move/reverse, close, and unload during an active opening. Paired captures
showed the same unfolding and settling. Timing was similar at the output:
single-window draw-interval p99 ranged from 18.24–18.63 ms with Jolt and
17.83–18.33 ms with the CPU solver; four-window p99 ranged from 19.24–19.63 ms
and 19.09–20.73 ms, respectively (three runs each). Four-window opening worker
p99 increased; the nested backend supplied callback intervals, not hardware
presentation timestamps. These observations do not establish a speedup.

## CPU-only optimization

Batch construction reuses scratch buffers across islands. On SSE2 hosts, a
packed divide evaluates the three coordinates together with the same IEEE
rounding as scalar division. The solver still disables floating-point
contraction and retains its original constraint order.

On the same workstation, the optimized build reproduced the previous solver's
CSV byte for byte. Interleaved open-recording runs used 5% less process CPU time
and 77 instead of 323 C++ allocations per recording. In a separate four-worker
model run, the median of six worker-wall p99 measurements fell from 6.40 to
5.29 ms. These are model timings, not compositor frame times.

On SSE2, the hinge pass also projects four vertex-disjoint constraints in
parallel within each level. It preserves each lane's float evaluation order
and uses the scalar pass for the remaining hinges and on non-SSE2 hosts.
All 1,874 displayed samples matched the previous CPU solver's CSV byte for
byte. The single-open model benchmark used 22% fewer user-space CPU cycles
in interleaved runs on the same workstation.
In the four-worker model benchmark, median recording time was 625 ms versus
788 ms for the previous CPU solver and 734 ms for frozen Jolt (six runs per
variant). The median worker-wall p99 was 4.18 ms, 5.04 ms, and 4.49 ms,
respectively. These are not compositor frame times.
