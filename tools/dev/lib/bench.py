"""Benchmark runs and baseline/candidate comparison with simple, assumption-light statistics."""

from __future__ import annotations

import json
import math
import os
import platform
import statistics
import sys
from pathlib import Path

from .common import (BUILD_ROOT, ROOT, Report, ToolError, build, configure, git_state, run, tail, tool_version)

BENCH_DIR = BUILD_ROOT / "bench"


def environment() -> dict:
    cpu = "unknown"
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    governor = None
    gov_path = Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
    if gov_path.exists():
        governor = gov_path.read_text().strip()
    load = os.getloadavg()
    return {"cpu": cpu, "cpus": os.cpu_count(), "governor": governor, "loadavg": load,
            "kernel": platform.release(), "compiler": tool_version("c++"), "git": git_state()}


def noise_warnings(env: dict) -> list[str]:
    warnings = []
    if env["governor"] not in (None, "performance"):
        warnings.append(f"CPU governor is '{env['governor']}', not 'performance': frequency scaling adds variance")
    if env["loadavg"][0] > max(1.0, (env["cpus"] or 1) * 0.25):
        warnings.append(f"load average {env['loadavg'][0]:.1f} on {env['cpus']} CPUs: other work is competing for cores")
    return warnings


def bench_binary(directory: Path) -> Path:
    return directory / "bench" / "hyprimate-bench"


def prepare_bench(label: str | None = None, source: Path | None = None, cxxflags_extra=()) -> Path:
    directory = configure("default", label=label, source=source, cxxflags_extra=cxxflags_extra)
    built = build(directory, ["hyprimate-bench"])
    if built.returncode:
        raise ToolError("hyprimate-bench failed to build:\n" + tail(built.stdout, 40))
    return bench_binary(directory)


def publish_run(report: Report, out: Path, data: dict, env: dict, status: str) -> int:
    out.parent.mkdir(parents=True, exist_ok=True)
    data["environment"] = env
    out.write_text(json.dumps(data, indent=1))
    warnings = noise_warnings(env)
    for line in warnings:
        print(f"warning: {line}", file=sys.stderr)
    report.add("bench", status, results=str(out.relative_to(ROOT)), noise_warnings=warnings,
               summary=f"{len(data['scenarios'])} scenarios; "
                       + ("a scenario is nondeterministic" if status == "fail" else "deterministic"))
    return report.write()


def cmd_bench_run(args) -> int:
    report = Report("bench-run")
    try:
        binary = prepare_bench()
    except ToolError as error:
        report.add("bench", "tool_failed", summary=str(error))
        return report.write()
    BENCH_DIR.mkdir(parents=True, exist_ok=True)
    out = BENCH_DIR / f"{args.name}.json"
    cmd = [binary, "--samples", str(args.samples), "--warmup", str(args.warmup), "--json", out]
    if args.filter:
        cmd += ["--filter", args.filter]
    env = environment()
    result = run(cmd, log=BUILD_ROOT / "logs" / "bench-run.log")
    print(result.stdout, end="")
    if not out.exists():
        report.add("bench", "tool_failed", summary="benchmark produced no results", output=tail(result.stdout))
        return report.write()
    data = json.loads(out.read_text())
    status = "fail" if result.returncode == 1 else "tool_failed" if result.returncode else "pass"
    return publish_run(report, out, data, env, status)


# ------------------------------------------------------------------------------------ comparison
def mann_whitney_p(a: list[float], b: list[float]) -> float:
    """Two-sided Mann-Whitney U p-value, normal approximation with tie correction (fine for n >= ~8)."""
    n1, n2 = len(a), len(b)
    if n1 < 3 or n2 < 3:
        return 1.0
    pooled = sorted([(v, 0) for v in a] + [(v, 1) for v in b])
    ranks = [0.0] * len(pooled)
    tie_term = 0.0
    i = 0
    while i < len(pooled):
        j = i
        while j + 1 < len(pooled) and pooled[j + 1][0] == pooled[i][0]:
            j += 1
        average = (i + j) / 2 + 1
        for k in range(i, j + 1):
            ranks[k] = average
        t = j - i + 1
        tie_term += t ** 3 - t
        i = j + 1
    r1 = sum(rank for rank, (_, group) in zip(ranks, pooled) if group == 0)
    u1 = r1 - n1 * (n1 + 1) / 2
    n = n1 + n2
    mean = n1 * n2 / 2
    variance = n1 * n2 / 12 * ((n + 1) - tie_term / (n * (n - 1)))
    if variance <= 0:
        return 1.0
    z = (abs(u1 - mean) - 0.5) / math.sqrt(variance)
    return math.erfc(max(z, 0) / math.sqrt(2))


def compare_scenario(base: dict, cand: dict, threshold: float, alpha: float, noise_cv: float) -> dict:
    timer = base.get("timer")
    native = timer is not None or cand.get("timer") is not None
    if native and (timer != cand.get("timer") or base.get("windows") != cand.get("windows")):
        return {"name": base["name"], "verdict": "incompatible",
                "note": "different profiler metrics or window counts"}
    a, b = base["ns_per_run"], cand["ns_per_run"]
    if native:
        result = {"name": base["name"], "timer": timer}
        if not base["deterministic"] or not cand["deterministic"]:
            return result | {"verdict": "inconclusive", "note": "nondeterministic workload"}
        if base["checksum"] != cand["checksum"]:
            return result | {"verdict": "behavior-changed"}
        if min(len(a), len(b)) < 8:
            return result | {"verdict": "inconclusive", "note": "fewer than 8 batches per side"}
    med_a, med_b = statistics.median(a), statistics.median(b)
    ratio = med_b / med_a if med_a else float("nan")
    p = mann_whitney_p(a, b)
    cv = max(statistics.pstdev(a) / statistics.mean(a), statistics.pstdev(b) / statistics.mean(b))
    change = ratio - 1.0
    if base["checksum"] != cand["checksum"] and base["deterministic"] and cand["deterministic"]:
        verdict = "behavior-changed"  # a deterministic workload produced different output: not a timing question
    elif base.get("threaded") or cand.get("threaded"):
        verdict = "inconclusive"  # wall time includes worker scheduling
    elif p < alpha and abs(change) >= threshold and cv <= noise_cv:
        verdict = "regression" if change > 0 else "improvement"
    elif abs(change) >= threshold:  # shifted but too noisy (cv above --noise-cv) or not supported by the samples
        verdict = "inconclusive"  # large median shift the samples do not support
    else:
        verdict = "no-measurable-change"
    statistics_row = {"name": base["name"], "verdict": verdict, "median_ratio": round(ratio, 4),
                      "p_value": round(p, 5), "noise_cv": round(cv, 3),
                      "allocations": [base["allocations"], cand["allocations"]],
                      "alloc_bytes": [base["alloc_bytes"], cand["alloc_bytes"]]}
    if native:
        return statistics_row | {"timer": timer, "baseline_median_us": round(med_a / 1e3, 3),
                                 "candidate_median_us": round(med_b / 1e3, 3)}
    return statistics_row | {"baseline_median_us_per_frame": round(med_a / base["frames"] / 1e3, 3),
                             "candidate_median_us_per_frame": round(med_b / cand["frames"] / 1e3, 3)}


def compare_files(base_path: Path, cand_path: Path, threshold: float = 0.03, alpha: float = 0.01,
                  noise_cv: float = 0.10) -> list[dict]:
    base = {s["name"]: s for s in json.loads(base_path.read_text())["scenarios"]}
    cand = {s["name"]: s for s in json.loads(cand_path.read_text())["scenarios"]}
    rows = [compare_scenario(base[n], cand[n], threshold, alpha, noise_cv) for n in base if n in cand]
    for name in sorted(set(base) ^ set(cand)):
        rows.append({"name": name, "verdict": "unmatched", "note": "present on one side only"})
    return rows


def cmd_bench_compare(args) -> int:
    report = Report("bench-compare")
    try:
        rows = compare_files(Path(args.baseline), Path(args.candidate), args.threshold / 100.0, args.alpha, args.noise_cv)
    except (OSError, KeyError, ValueError) as error:
        report.add("bench-compare", "tool_failed", summary=f"cannot read results: {error}")
        return report.write()
    for row in rows:
        if "timer" in row and "median_ratio" in row:
            print(f"{row['name']:<26} {row['verdict']:<22} x{row['median_ratio']:.3f}  p={row['p_value']:.4f}  "
                  f"{row['baseline_median_us']:.2f}->{row['candidate_median_us']:.2f} us ({row['timer']})")
        elif "median_ratio" in row:
            print(f"{row['name']:<26} {row['verdict']:<22} x{row['median_ratio']:.3f}  p={row['p_value']:.4f}  "
                  f"{row['baseline_median_us_per_frame']:.2f}->{row['candidate_median_us_per_frame']:.2f} us/frame  "
                  f"allocs {row['allocations'][0]}->{row['allocations'][1]}")
        else:
            detail = row.get("note") if row["verdict"] != "unmatched" else None
            print(f"{row['name']:<26} {row['verdict']}" + (f" ({detail})" if detail else ""))
    verdicts = {r["verdict"] for r in rows}
    status = ("fail" if verdicts & {"regression", "behavior-changed"}
              else "advisory" if verdicts & {"inconclusive", "incompatible"} else "pass")
    counts = {v: sum(1 for r in rows if r["verdict"] == v) for v in sorted(verdicts)}
    report.add("bench-compare", status, rows=rows, counts=counts,
               threshold_percent=args.threshold, alpha=args.alpha,
               summary=", ".join(f"{n} {v}" for v, n in counts.items()))
    return report.write()
