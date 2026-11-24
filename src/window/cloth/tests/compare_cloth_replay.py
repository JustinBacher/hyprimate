#!/usr/bin/env python3
"""Compare a public-API cloth replay to the frozen display reference."""
import csv
import gzip
import math
import statistics
import sys
from pathlib import Path

# Tolerances are expressed in the output metric's units; see ClothReplay.md.
TOLERANCES = {
    "time": 1e-9,
    "opacity": 0.01,
    "xmin": 0.01,
    "xmax": 0.01,
    "ymin": 0.01,
    "ymax": 0.01,
    "depth": 0.02,
    "hem_lag_x": 0.02,
    "hem_lag_y": 0.02,
    "hem_lag_z": 0.02,
    "spread": 0.02,
    "vx": 0.05,
    "vy": 0.05,
    "vz": 0.05,
    "speed_rms": 0.05,
    "centerx": 0.02,
    "centery": 0.02,
    "centerz": 0.02,
    "center_vx": 0.05,
    "center_vy": 0.05,
    "center_vz": 0.05,
    "header_left": 0.02,
    "header_right": 0.02,
    "obstacle_vertices": 3.0,
    "obstacle_penetration": 1.0,
    "paired_pose_delta": 0.02,
}
EVENTS = {
    "active_end": lambda row: row["active"] == "0",
    "closed": lambda row: row["closed"] == "1",
    "fully_outside": lambda row: row["fully_outside"] == "1",
    "obstacle_response_onset": lambda row: float(row["paired_pose_delta"]) > 0.005,
}
EVENT_TOLERANCE_SAMPLES = 1.0


def load(path):
    path = Path(path)
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt", newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames:
            raise ValueError(f"{path}: missing CSV header")
        rows = {}
        fields = set(reader.fieldnames)
        for row in reader:
            key = (row["scenario"], int(row["frame"]))
            if key in rows:
                raise ValueError(f"{path}: duplicate row {key}")
            rows[key] = row
        return fields, rows


def median_interval(rows):
    times = sorted({float(row["time"]) for row in rows})
    gaps = [right - left for left, right in zip(times, times[1:]) if right > left]
    return statistics.median(gaps) if gaps else 1.0 / 120.0


def event_time(rows, predicate):
    hits = [float(row["time"]) for row in rows if predicate(row)]
    return min(hits) if hits else None


def compare(reference_path, candidate_path):
    reference_fields, reference = load(reference_path)
    candidate_fields, candidate = load(candidate_path)
    failures = []
    if reference_fields != candidate_fields:
        failures.append(f"CSV columns differ: reference={sorted(reference_fields)}, candidate={sorted(candidate_fields)}")
    if reference.keys() != candidate.keys():
        missing = sorted(reference.keys() - candidate.keys())[:8]
        extra = sorted(candidate.keys() - reference.keys())[:8]
        failures.append(f"sample keys differ: missing={missing}, extra={extra}")
        return failures

    for key, expected in reference.items():
        actual = candidate[key]
        for field, tolerance in TOLERANCES.items():
            try:
                before = float(expected[field])
                after = float(actual[field])
            except (KeyError, ValueError) as error:
                failures.append(f"{key} {field}: invalid value ({error})")
                continue
            if not math.isfinite(before) or not math.isfinite(after):
                failures.append(f"{key} {field}: non-finite value")
            elif abs(after - before) > tolerance:
                failures.append(
                    f"{key} {field}: reference={before:.9g}, candidate={after:.9g}, "
                    f"delta={abs(after - before):.4g} > {tolerance:g}"
                )
        try:
            baseline_crossings = int(expected["crossings"])
            current_crossings = int(actual["crossings"])
        except (KeyError, ValueError) as error:
            failures.append(f"{key} crossings: invalid value ({error})")
            continue
        if baseline_crossings != 0 or current_crossings != 0:
            failures.append(
                f"{key} crossings: hard invariant requires 0; "
                f"reference={baseline_crossings}, candidate={current_crossings}"
            )

    scenario_names = sorted({scenario for scenario, _ in reference})
    for scenario in scenario_names:
        baseline_rows = [row for (name, _), row in sorted(reference.items()) if name == scenario]
        current_rows = [row for (name, _), row in sorted(candidate.items()) if name == scenario]
        max_event_delta = median_interval(baseline_rows) * EVENT_TOLERANCE_SAMPLES
        for event_name, predicate in EVENTS.items():
            before = event_time(baseline_rows, predicate)
            after = event_time(current_rows, predicate)
            # Compare the first actual sheet response in explicitly blocked cases.
            if event_name == "obstacle_response_onset" and not scenario.endswith("b"):
                continue
            if before is None and after is None:
                continue
            if before is None or after is None:
                failures.append(f"{scenario} {event_name}: event missing in one replay (reference={before}, candidate={after})")
            elif abs(after - before) > max_event_delta + 1e-9:
                failures.append(
                    f"{scenario} {event_name}: reference={before:.9f}s, candidate={after:.9f}s, "
                    f"delta={abs(after - before):.6g}s > {max_event_delta:.6g}s"
                )

    for scenario in scenario_names:
        rows = [row for (name, _), row in sorted(candidate.items()) if name == scenario]
        is_move_close = scenario.startswith("move") and scenario.endswith("_close") and not scenario.endswith(("_before_close", "_after_close"))
        if scenario.startswith("close") or is_move_close:
            if rows[-1]["closed"] != "1" or float(rows[-1]["opacity"]) != 0:
                failures.append(f"{scenario}: replay must finish closed with zero opacity")
        if scenario.startswith("open"):
            if rows[-1]["active"] != "0" or float(rows[-1]["opacity"]) != 1:
                failures.append(f"{scenario}: replay must finish settled and inactive")

    handoff_positions = (
        "opacity", "xmin", "xmax", "ymin", "ymax", "depth", "hem_lag_x", "hem_lag_y",
        "hem_lag_z", "spread", "centerx", "centery", "centerz", "header_left", "header_right",
    )
    handoff_velocities = ("vx", "vy", "vz", "speed_rms", "center_vx", "center_vy", "center_vz")
    for prefix in ("move0", "move1", "move2"):
        before = candidate.get((f"{prefix}_before_close", 0))
        after = candidate.get((f"{prefix}_after_close", 0))
        if before is None or after is None:
            failures.append(f"{prefix}: missing movement-to-close handoff sample")
            continue
        for field in handoff_positions:
            if abs(float(after[field]) - float(before[field])) > 1e-6:
                failures.append(f"{prefix} handoff {field}: visible pose discontinuity")
        for field in handoff_velocities:
            if abs(float(after[field]) - float(before[field])) > 1e-5:
                failures.append(f"{prefix} handoff {field}: velocity discontinuity")
        if before["active"] != after["active"] or before["closed"] != after["closed"]:
            failures.append(f"{prefix} handoff: lifecycle changed across transition start")
    for edge in range(4):
        clear_name = f"close{edge}u"
        blocked_name = f"close{edge}b"
        clear_rows = [row for (name, _), row in candidate.items() if name == clear_name]
        blocked_rows = [row for (name, _), row in candidate.items() if name == blocked_name]
        if not clear_rows or not blocked_rows:
            failures.append(f"{blocked_name}: missing blocked-versus-clear witness")
            continue
        clear_delta = max(float(row["paired_pose_delta"]) for row in clear_rows)
        blocked_delta = max(float(row["paired_pose_delta"]) for row in blocked_rows)
        if clear_delta != 0:
            failures.append(f"{clear_name}: clear reference unexpectedly has an obstacle delta")
        if blocked_delta <= 0.005:
            failures.append(f"{blocked_name}: obstacle did not measurably alter the sheet pose")


    return failures


def main(argv):
    if len(argv) != 3:
        print(f"usage: {argv[0]} REFERENCE.csv[.gz] CANDIDATE.csv[.gz]", file=sys.stderr)
        return 2
    try:
        failures = compare(argv[1], argv[2])
    except (OSError, ValueError, KeyError) as error:
        print(f"cloth replay comparison failed to load: {error}", file=sys.stderr)
        return 2
    if failures:
        for failure in failures[:100]:
            print(f"FAIL: {failure}")
        if len(failures) > 100:
            print(f"... and {len(failures) - 100} more failures")
        return 1
    print("cloth replay comparison passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
