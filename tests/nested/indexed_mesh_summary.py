#!/usr/bin/env python3
"""Summarise indexed_mesh_bench.sh output directories (read-only).

usage: indexed_mesh_summary.py DIR [DIR ...] [--style cloth]

Only sessions with a TAG.VALID marker are counted; every attempt (including INVALID ones, with its
reasons) is listed. Per-session numbers come straight from the profiler report; percentiles in the
profiler cover the most recent 512 samples of a series, means/maxima/counts cover the session. Arm
columns are the median and the min..max range of the per-session value across valid sessions, so
nothing here pretends to be a pooled distribution.
"""
import json
import re
import statistics
import sys
from pathlib import Path

STOPWATCH = re.compile(r"gpu-stopwatch (\S+): n=(\d+) mean=([\d.]+)us p50=([\d.]+)us p95=([\d.]+)us "
                       r"max=([\d.]+)us dropped=(\d+)")
UPLOADED = re.compile(r"uploaded=(\d+) expanded=(\d+) index_bytes=(\d+)")
VERTEX_BYTES = 16  # sizeof(SMeshRenderVertex)


def series(report, style, operation):
    for entry in report["styles"]:
        if entry["style"] == style and entry["operation"] == operation:
            return entry
    return None


def ms_us(value):
    return None if value is None else value * 1000.0


def load_session(directory, tag, style):
    row = {"tag": tag}
    report = json.loads((directory / f"{tag}.profile.txt").read_text())
    window = series(report, "sheet", "window-layer")
    fill = series(report, "sheet", "mesh-fill")
    shared = series(report, style, "shared")
    for name, entry, path in (("wl_cpu", window, ("cpu", "draw", "thread_cpu")),
                              ("fill_cpu", fill, ("cpu", "update", "thread_cpu")),
                              ("style_gpu", shared, ("gpu",))):
        node = entry
        for key in path:
            node = node[key] if node else None
        for stat in ("mean_ms", "p50_ms", "p95_ms", "p99_ms", "max_ms"):
            row[f"{name}.{stat[:-3]}"] = ms_us(node.get(stat)) if node else None
        row[f"{name}.n"] = node.get("samples") if node else None
    if shared:
        for name in ("draw_intervals", "presentation_callback_intervals"):
            node = shared.get(name) or {}
            for stat in ("mean_ms", "p50_ms", "p95_ms", "p99_ms", "max_ms"):
                row[f"{name}.{stat[:-3]}"] = node.get(stat)
    counters = directory / f"{tag}.counters.txt"
    if counters.exists():
        text = counters.read_text(errors="replace")
        for label, n, mean, p50, p95, mx, dropped in STOPWATCH.findall(text):
            row[f"gpu[{label}].n"] = int(n)
            row[f"gpu[{label}].mean"] = float(mean)
            row[f"gpu[{label}].p50"] = float(p50)
            row[f"gpu[{label}].p95"] = float(p95)
            row[f"gpu[{label}].max"] = float(mx)
            row[f"gpu[{label}].dropped"] = int(dropped)
        match = UPLOADED.search(text)
        if match:
            uploaded, expanded, index_bytes = map(int, match.groups())
            row["bytes.expanded"] = expanded * VERTEX_BYTES
            row["bytes.indexed"] = uploaded * VERTEX_BYTES + index_bytes
            row["bytes.index_only"] = index_bytes
    cpu = directory / f"{tag}.cpu.txt"
    if cpu.exists():
        values = dict(item.split("=") for item in cpu.read_text().split())
        ticks = int(values["clk_tck"])
        wall = max(int(values["wall_s"]), 1)
        row["compositor_cpu_pct"] = 100.0 * (int(values["utime_ticks"]) + int(values["stime_ticks"])) / ticks / wall
    workload = directory / f"{tag}.workload.txt"
    row["workload"] = workload.read_text().strip() if workload.exists() else ""
    return row


def mode_of(tag):
    return tag.split("-")[0]


def fmt(values, digits=1):
    values = [v for v in values if v is not None]
    if not values:
        return "-"
    med = statistics.median(values)
    if len(values) == 1:
        return f"{med:.{digits}f}"
    return f"{med:.{digits}f} [{min(values):.{digits}f}..{max(values):.{digits}f}]"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    style = "cloth"
    if "--style" in sys.argv:
        style = sys.argv[sys.argv.index("--style") + 1]
        args = [a for a in args if a != style]
    for arg in args:
        directory = Path(arg)
        print(f"\n## {directory}")
        attempts = sorted(p.name[: -len(".runner.log")] for p in directory.glob("*.runner.log"))
        valid = [t for t in attempts if (directory / f"{t}.VALID").exists()]
        print(f"attempts={len(attempts)} valid={len(valid)} invalid={len(attempts) - len(valid)}")
        for tag in attempts:
            if tag not in valid:
                inv = directory / f"{tag}.INVALID"
                print(f"  INVALID {tag}: {inv.read_text().strip().replace(chr(10), '; ') if inv.exists() else '(no marker)'}")
        rows = {t: load_session(directory, t, style) for t in valid}
        if not rows:
            continue
        workloads = {r["workload"].split(" seconds=")[0] + " " + " ".join(r["workload"].split()[2:]) for r in rows.values()}
        print("workloads:", "; ".join(sorted(workloads)))
        keys = ["wl_cpu.mean", "wl_cpu.p50", "wl_cpu.p95", "wl_cpu.p99", "wl_cpu.max", "fill_cpu.mean", "fill_cpu.p95",
                "gpu[window-layer].p50", "gpu[window-layer].mean", "gpu[window-layer].p95", "style_gpu.p50",
                "style_gpu.mean", "style_gpu.p95", "style_gpu.p99", "style_gpu.max",
                "draw_intervals.p50", "draw_intervals.p95", "draw_intervals.p99", "draw_intervals.max",
                "presentation_callback_intervals.p95", "compositor_cpu_pct", "bytes.expanded", "bytes.indexed"]
        extra = sorted({k for r in rows.values() for k in r if k.startswith("gpu[") and k.endswith(".p50") and "window-layer" not in k})
        modes = sorted({mode_of(t) for t in rows})
        print("\n| metric (us unless noted) | " + " | ".join(f"{m} (n={sum(mode_of(t) == m for t in rows)})" for m in modes) + " |")
        print("|---|" + "---|" * len(modes))
        for key in keys + extra:
            cells = []
            for mode in modes:
                vals = [r.get(key) for t, r in rows.items() if mode_of(t) == mode]
                cells.append(fmt(vals, 0 if key.startswith("bytes") else 1))
            if any(c != "-" for c in cells):
                print(f"| {key} | " + " | ".join(cells) + " |")
        print("\nper session:")
        for tag, r in sorted(rows.items()):
            print(f"  {tag}: wl_cpu mean={r.get('wl_cpu.mean')} p95={r.get('wl_cpu.p95')} | style_gpu mean={r.get('style_gpu.mean')} "
                  f"p50={r.get('style_gpu.p50')} | draw_int p99={r.get('draw_intervals.p99')} max={r.get('draw_intervals.max')}")


if __name__ == "__main__":
    main()
