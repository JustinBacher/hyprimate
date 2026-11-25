# Cloth solver replay reference

`ClothReplay.cpp` drives the public `ClothFlutter` API and writes deterministic
display-time samples to CSV. Setup and recording wall times go to stderr and
are excluded from the comparison.

The separate `cloth-lease-failure-tests` target covers solver and recording
faults, including rejection after a frame has been published. An active
playback can claim one diagnostic per run when it enters fallback. A retained
copy can still claim it after the original playback is superseded; dropping
the last playback or stopping the runtime closes the claim. Cancellation that
wins this race is silent. A new run can retry.

## Frozen baseline

The tracked `cloth-reference.csv.gz` records the accepted whole-mesh CPU
solver. Its 57×57 grid runs eight substeps per 120 Hz tick. Constraints are
built from the mesh and processed as hinge, stretch, and attachment passes.
Within the first two passes, each level contains vertex-disjoint constraints
and preserves the relative order of constraints that share a vertex. The
hinge pass processes four constraints together on SSE2 hosts, with a scalar
tail and fallback. A future compute backend can parallelize within each level
but must not assume levels are independent.

Build the replay and compare against the frozen fixture:

```sh
cmake --build build -j 2 --target cloth-replay
build/cloth-replay > /tmp/cloth-replay-candidate.csv
python3 src/window/cloth/tests/compare_cloth_replay.py \
  src/window/cloth/tests/cloth-reference.csv.gz \
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

These are regression limits, not perceptual guarantees. Only refresh the
fixture for a deliberately accepted motion change; do not loosen limits to
make a candidate pass. After reviewing a change, freeze its validated trace
with `gzip -n -c /tmp/cloth-replay-candidate.csv`.

## Applicability gaps

The public API has no accessor for raw solver-tick recordings, exact worker tick counts/timing distributions, the internal `outside_frame`, fallback-use state, or collision contact events. Therefore this fixture compares **displayed** states at fixed timestamps (plus controlled movement snapshot publication), not raw vertex-by-vertex solver ticks; full-exit timing is inferred from displayed vertex positions. Obstacle-AABB overlap is only a geometric proxy; the paired blocked/clear pose delta is the actual observable response to the retained obstacle, not a contact-depth measurement. The API exposes displayed velocity, including the movement-to-close handoff samples.

This replay cannot measure compositor presentation/frame times, startup-to-first-visible frame, multi-window load, missed live compositor publications, or visual artifacts hidden by renderer/shading/fallback. It does not replace native Hyprland A/B clips or compositor lifecycle checks. Timing lines capture setup-to-first-publication and complete recording wall time only; no stable p50/p95/p99 tick metric is available through this interface.

## Whole-mesh cutover

The whole-mesh schedule and independent half-angle evaluation intentionally
change the transient motion. Paired 60 Hz nested Hyprland captures showed
similar openings and pixel-identical settled frames, but different folds
during fast reversals. The capture did not resolve the visible close; replay
showed differences including right-edge close fold depth. The new fixture
records the accepted behavior rather than widening replay tolerances.

Interleaved single-open model runs used about 16% fewer user-space CPU cycles
than the preceding CPU solver on the same workstation. In six runs per
variant with four concurrent opens, median recording time fell from 647 ms
to 513 ms and median worker wall-time p99 fell from 4.13 ms to 3.38 ms.
These are model measurements, not compositor frame-time measurements.
