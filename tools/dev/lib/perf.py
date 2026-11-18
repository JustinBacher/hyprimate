"""profile (perf / callgrind), memory-profile (heaptrack / allocation counters) and build-profile."""

from __future__ import annotations

import json
from collections import defaultdict
from pathlib import Path

from .bench import environment, noise_warnings, prepare_bench
from .common import (GENERATORS, REPORT_ROOT, ROOT, Report, ToolError, build, configure, run, tail,
                     tool_path)


def workload(args, binary: Path, samples: int, warmup: int) -> list:
    cmd = [binary, "--samples", str(samples), "--warmup", str(warmup)]
    if args.scenario:
        cmd += ["--filter", args.scenario]
    return cmd


# ------------------------------------------------------------------------------------- profile
def cmd_profile(args) -> int:
    report = Report("profile")
    tool = args.tool
    needed = {"perf": "perf", "callgrind": "valgrind"}[tool]
    if tool_path(needed) is None:
        report.add(tool, "unavailable", summary=f"{needed} not installed; see `tools/dev.py doctor --for profile`")
        return report.write(require_tools=True)
    try:
        binary = prepare_bench()
    except ToolError as error:
        report.add(tool, "tool_failed", summary=str(error))
        return report.write()
    out = REPORT_ROOT / "profile"
    out.mkdir(parents=True, exist_ok=True)
    tag = (args.scenario or "all").replace(".", "_")
    if tool == "perf":
        data = out / f"{tag}.perf.data"
        # DWARF unwinding: the production build keeps frame pointers off, and the profile must not need a rebuild.
        def record_with(*event):
            return run(["perf", "record", *event, "-F", str(args.frequency), "--call-graph", "dwarf,16384", "-o", data,
                        "--", *workload(args, binary, args.samples, 3)], log=out / f"{tag}.record.log")
        record = record_with()
        if record.returncode:
            # Hardware cycles can be denied (perf_event_paranoid, VMs); the software clock still samples hotspots.
            record = record_with("-e", "cpu-clock")
        if record.returncode or not data.exists():
            hint = (" -- perf_event_paranoid is too restrictive for even user-space sampling; "
                    "run `sudo sysctl kernel.perf_event_paranoid=1` (temporary) after reviewing the security trade-off"
                    if "perf_event_paranoid" in record.stdout else "")
            report.add("perf", "tool_failed", summary="perf record failed" + hint, output=tail(record.stdout))
            return report.write()
        run(["perf", "report", "-i", data, "--stdio", "--no-children", "--percent-limit", "1",
                   "--sort", "symbol", "-g", "none"], log=out / f"{tag}.flat.txt", quiet=True)
        run(["perf", "report", "-i", data, "--stdio", "--children", "--percent-limit", "2", "-g", "caller,0.5,callee"],
            log=out / f"{tag}.callgraph.txt", quiet=True)
        counters = run(["perf", "stat", "-e", "cycles,instructions,branches,branch-misses,cache-references,cache-misses",
                        "-x,", "--", *workload(args, binary, 5, 1)], quiet=True)
        stat_lines = [l for l in counters.stdout.splitlines() if "," in l and not l.startswith("#")]
        env = environment()
        report.add("perf", "pass", flat_profile=str((out / f"{tag}.flat.txt").relative_to(ROOT)),
                   callgraph=str((out / f"{tag}.callgraph.txt").relative_to(ROOT)), perf_data=str(data.relative_to(ROOT)),
                   counters=stat_lines, noise_warnings=noise_warnings(env),
                   summary="sampled native execution of the unmodified RelWithDebInfo build; "
                           "<not supported> counters mean the CPU/VM exposes no PMU")
    else:
        data = out / f"{tag}.callgrind.out"
        result = run(["valgrind", "--tool=callgrind", f"--callgrind-out-file={data}", "--dump-instr=no",
                      "--collect-jumps=no", *workload(args, binary, 1, 0)], log=out / f"{tag}.callgrind.log")
        if result.returncode or not data.exists():
            report.add("callgrind", "tool_failed", summary="valgrind callgrind failed", output=tail(result.stdout))
            return report.write()
        run(["callgrind_annotate", "--auto=no", "--threshold=95", data], log=out / f"{tag}.annotate.txt", quiet=True)
        report.add("callgrind", "pass", annotated=str((out / f"{tag}.annotate.txt").relative_to(ROOT)),
                   callgrind_out=str(data.relative_to(ROOT)),
                   summary="instruction counts under emulation: ranks functions by retired instructions; "
                           "it does not predict native time (no cache/branch/frequency model beyond Valgrind's simulation)")
    return report.write()


# ------------------------------------------------------------------------------ memory-profile
def cmd_memory_profile(args) -> int:
    report = Report("memory-profile")
    try:
        binary = prepare_bench()
    except ToolError as error:
        report.add("memory-profile", "tool_failed", summary=str(error))
        return report.write()
    out = REPORT_ROOT / "memory-profile"
    out.mkdir(parents=True, exist_ok=True)
    counters_json = out / "allocation-counters.json"
    result = run([binary, "--samples", "3", "--warmup", "1", "--json", counters_json,
                  *(["--filter", args.scenario] if args.scenario else [])], log=out / "allocation-counters.txt")
    if not counters_json.exists():
        report.add("allocation-counters", "tool_failed", summary="benchmark failed", output=tail(result.stdout))
        return report.write()
    rows = [{"scenario": s["name"], "allocations_per_run": s["allocations"], "bytes_per_run": s["alloc_bytes"],
             "allocations_per_frame": round(s["allocations"] / s["frames"], 3)}
            for s in json.loads(counters_json.read_text())["scenarios"]]
    for row in rows:
        print(f"{row['scenario']:<26} {row['allocations_per_run']:>8} allocs  {row['bytes_per_run'] / 1024:>10.1f} KiB  "
              f"{row['allocations_per_frame']:>8.2f}/frame")
    report.add("allocation-counters", "pass", rows=rows, file=str(counters_json.relative_to(ROOT)),
               summary="exact operator new counts per scripted workload (C++ allocations only; Jolt's own allocator "
                       "and malloc() callers are not counted, use heaptrack for those)")
    if tool_path("heaptrack") is None or tool_path("heaptrack_print") is None:
        report.add("heaptrack", "unavailable", summary="heaptrack not installed; allocation counters above are the fallback")
        return report.write(require_tools=args.require_all)
    prefix = out / "heaptrack"
    for stale in out.glob("heaptrack*.zst"):
        stale.unlink()
    # env -u: heaptrack otherwise opens heaptrack_gui after the run and blocks unattended use.
    result = run(["env", "-u", "DISPLAY", "-u", "WAYLAND_DISPLAY", "heaptrack", "-o", prefix, binary, "--samples", "3", "--warmup", "1",
                  *(["--filter", args.scenario] if args.scenario else [])], log=out / "heaptrack.log")
    traces = sorted(out.glob("heaptrack*.zst")) + sorted(out.glob("heaptrack*.gz"))
    if not traces:
        report.add("heaptrack", "tool_failed", summary="heaptrack run failed", output=tail(result.stdout))
        return report.write()
    run(["heaptrack_print", traces[-1], "-n", "12", "--print-leaks", "no"], log=out / "heaptrack-summary.txt", quiet=True)
    report.add("heaptrack", "pass", trace=str(traces[-1].relative_to(ROOT)),
               summary=f"inspect with `heaptrack_gui {traces[-1].relative_to(ROOT)}` or {out.relative_to(ROOT)}/heaptrack-summary.txt")
    return report.write()


# ---------------------------------------------------------------------------------- build-profile
def cmd_build_profile(args) -> int:
    report = Report("build-profile")
    try:
        directory = configure("timetrace")
        run(["cmake", "--build", directory, "--target", "clean"], quiet=True)
        built = build(directory, [*GENERATORS, "hyprimate-core", "hyprimate"] if not args.core_only
                      else [*GENERATORS, "hyprimate-core"])
    except ToolError as error:
        report.add("build-profile", "tool_failed", summary=str(error))
        return report.write()
    if built.returncode:
        report.add("build-profile", "fail", summary="build failed", output=tail(built.stdout, 30))
        return report.write()
    traces = []
    for path in directory.rglob("*.json"):
        if path.name == "compile_commands.json" or "CMakeFiles" not in path.parts:
            continue
        try:
            head = path.read_text(errors="replace")[:80]
        except OSError:
            continue
        if '"traceEvents"' in head:
            traces.append(path)
    if not traces:
        report.add("time-trace", "tool_failed", summary="no -ftime-trace output found")
        return report.write()
    units, headers, templates = {}, defaultdict(float), defaultdict(lambda: [0.0, 0])
    third_party_total = 0.0
    for path in traces:
        events = json.loads(path.read_text()).get("traceEvents", [])
        total = frontend = backend = 0.0
        for event in events:
            name, dur = event.get("name"), event.get("dur", 0) / 1e6
            if name == "Total ExecuteCompiler":
                total = dur
            elif name == "Total Frontend":
                frontend = dur
            elif name == "Total Backend":
                backend = dur
        in_project = str(ROOT / "src") in str(path.parent) or "hyprimate.dir" in str(path) or "hyprimate-core.dir" in str(path)
        if not in_project:
            third_party_total += total
            continue
        units[path.name.removesuffix(".json").removesuffix(".o")] = {"total_s": total, "frontend_s": frontend, "backend_s": backend}
        for event in events:
            name, dur, detail = event.get("name"), event.get("dur", 0) / 1e6, event.get("args", {}).get("detail", "")
            if name == "Source" and detail:
                headers[detail] += dur
            elif name in ("InstantiateClass", "InstantiateFunction") and detail:
                templates[f"{name}:{detail}"][0] += dur
                templates[f"{name}:{detail}"][1] += 1
    ranked = sorted(units.items(), key=lambda kv: -kv[1]["total_s"])[:args.top]
    top_headers = sorted(headers.items(), key=lambda kv: -kv[1])[:args.top]
    top_templates = sorted(templates.items(), key=lambda kv: -kv[1][0])[:args.top]
    print(f"{'translation unit':<42} {'total s':>8} {'frontend':>9} {'backend':>8}")
    for name, row in ranked:
        print(f"{name:<42} {row['total_s']:>8.2f} {row['frontend_s']:>9.2f} {row['backend_s']:>8.2f}")
    print("\nheaviest parsed headers (summed over TUs, inclusive time):")
    for name, dur in top_headers:
        print(f"  {dur:>7.2f}s  {name}")
    print("\nheaviest template instantiations:")
    for name, (dur, count) in top_templates:
        print(f"  {dur:>7.2f}s  x{count:<4} {name[:110]}")
    out = REPORT_ROOT / "build-profile.detail.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"units": units, "headers": dict(top_headers),
                               "templates": {k: {"seconds": v[0], "count": v[1]} for k, v in top_templates},
                               "third_party_total_s": third_party_total}, indent=1))
    extra = {}
    if tool_path("ClangBuildAnalyzer"):
        cba = directory / "cba.bin"
        run(["ClangBuildAnalyzer", "--start", directory], quiet=True)
        run(["ClangBuildAnalyzer", "--stop", directory, cba], quiet=True)
        run(["ClangBuildAnalyzer", "--analyze", cba], log=REPORT_ROOT / "build-profile.cba.txt", quiet=True)
        extra["clangbuildanalyzer"] = str((REPORT_ROOT / "build-profile.cba.txt").relative_to(ROOT))
    report.add("time-trace", "pass", project_translation_units=len(units), third_party_seconds=round(third_party_total, 1),
               slowest=[{"unit": n, **r} for n, r in ranked], detail=str(out.relative_to(ROOT)), **extra,
               summary=f"{len(units)} project TUs; ClangBuildAnalyzer "
                       + ("also run" if extra else "not installed (built-in -ftime-trace aggregation used)"))
    return report.write()
