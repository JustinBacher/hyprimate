#!/usr/bin/env python3
"""Hyprimate development toolchain: one entry point over the isolated diagnostic builds.

Nothing here changes the production build. Every instrumented or analysis build lives in build/dev/<profile>, is
configured by tools/dev/CMakeLists.txt (which adds the unmodified root project as a subdirectory) and receives its
flags on the command line. Reports are written to build/dev/reports/<command>.json.

Exit codes: 0 ok (advisories allowed) | 1 blocking finding (defect, regression) | 2 usage | 3 tool/environment failure.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import tempfile
from pathlib import Path
from subprocess import SubprocessError
from time import perf_counter_ns

sys.path.insert(0, str(Path(__file__).resolve().parent / "dev"))

from lib import bench, codegen, compositor_bench, perf, runtime, static  # noqa: E402
from lib.common import BUILD_ROOT, EXIT_TOOL, Report, ToolError, build, configure, tail  # noqa: E402


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="tools/dev.py", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")

    p = sub.add_parser("doctor", help="verify tools and environment; --for COMMAND narrows what is required")
    p.add_argument("--for", dest="command_for", metavar="COMMAND")
    p.set_defaults(func=static.cmd_doctor)

    p = sub.add_parser("check", help="fast validation: format-check, build, ctest (default profile)")
    p.add_argument("--no-plugin", action="store_true", help="skip building the plugin shared object")
    p.add_argument("--tidy", action="store_true", help="also run the clang-tidy ratchet")
    p.add_argument("--update-baseline", choices=["format", "tidy", "all"],
                   help="record current findings as the baseline (review the diff first)")
    p.set_defaults(func=static.cmd_check)

    p = sub.add_parser("analyze", help="static analysis: clang-tidy, cppcheck (default); scan-build, iwyu, codeql")
    p.add_argument("--only", help="comma list of: tidy,cppcheck,scan-build,iwyu,codeql")
    p.add_argument("--all", action="store_true", help="run every analyzer that is installed")
    p.add_argument("--require-all", action="store_true", help="exit 3 if a selected analyzer is not installed")
    p.add_argument("--update-baseline", choices=["tidy", "clang-tidy", "cppcheck", "scan-build", "codeql", "all"],
                   help="record the current findings as the baseline for that analyzer (review the diff first)")
    p.add_argument("--codeql-suite", default="codeql/cpp-queries:codeql-suites/cpp-security-and-quality.qls")
    p.set_defaults(func=static.cmd_analyze)

    p = sub.add_parser("sanitize", help="ASan/UBSan runs of the standalone test executables (isolated builds)")
    p.add_argument("kind", nargs="?", choices=["asan", "ubsan", "all"], default="all")
    p.add_argument("--exclude", help="regex of test names to skip")
    p.add_argument("--timeout", type=float, default=600)
    p.set_defaults(func=runtime.cmd_sanitize)

    p = sub.add_parser("memcheck", help="Valgrind Memcheck over the test executables (default build)")
    p.add_argument("--only", help="regex of test names to run")
    p.add_argument("--exclude", help="regex of test names to skip")
    p.add_argument("--timeout", type=float, default=1800)
    p.set_defaults(func=runtime.cmd_memcheck)

    p = sub.add_parser("coverage", help="LLVM source-based coverage of src/ from the test executables")
    p.add_argument("--update-baseline", action="store_true", help="record current totals as informational baseline")
    p.add_argument("--timeout", type=float, default=900)
    p.set_defaults(func=runtime.cmd_coverage)

    p = sub.add_parser("profile", help="CPU profile a benchmark scenario with perf (default) or callgrind")
    p.add_argument("--tool", choices=["perf", "callgrind"], default="perf")
    p.add_argument("--scenario", help="scenario name or substring (see `bench list`)")
    p.add_argument("--samples", type=int, default=200, help="perf: repetitions of the scenario")
    p.add_argument("--frequency", type=int, default=999)
    p.set_defaults(func=perf.cmd_profile)

    p = sub.add_parser("memory-profile", help="allocation analysis: operator-new counters; heaptrack when installed")
    p.add_argument("--scenario")
    p.add_argument("--require-all", action="store_true")
    p.set_defaults(func=perf.cmd_memory_profile)

    p = sub.add_parser("build-profile", help="compile-time analysis from clang -ftime-trace (ClangBuildAnalyzer if present)")
    p.add_argument("--top", type=int, default=12)
    p.add_argument("--core-only", action="store_true", help="skip the plugin target")
    p.set_defaults(func=perf.cmd_build_profile)

    p = sub.add_parser("bench", help="animation benchmarks")
    bsub = p.add_subparsers(dest="bench_command", required=True)
    q = bsub.add_parser("run", help="build and run the benchmark suite; write build/dev/bench/NAME.json")
    q.add_argument("--name", default="latest")
    q.add_argument("--filter")
    q.add_argument("--samples", type=int, help="batches per scenario (core: 30; native: 2 for determinism smoke)")
    q.add_argument("--warmup", type=int, help="unmeasured batches per scenario (core: 5; native: 0)")
    q.add_argument("--windows", type=int, choices=(4, 16), default=16,
                   help="simultaneous native close animations in windows.many (default 16)")
    q.epilog = ("Native timing uses existing compositor profiler scopes. Private-session wall includes startup, "
                "fixture control and waiting, excludes building, and is not the performance statistic. "
                "Burn and Planet visual witnesses use grim and Pillow. "
                "For performance comparisons use at least eight batches per side on a quiet machine.")
    q.set_defaults(func=run_bench)
    q = bsub.add_parser("compare", help="compare two result files")
    q.add_argument("baseline")
    q.add_argument("candidate")
    q.add_argument("--threshold", type=float, default=3.0, help="minimum median change, percent (default 3)")
    q.add_argument("--alpha", type=float, default=0.01, help="Mann-Whitney significance (default 0.01)")
    q.add_argument("--noise-cv", type=float, default=0.10)
    q.set_defaults(func=bench.cmd_bench_compare)
    q = bsub.add_parser("list", help="list scenarios")
    q.add_argument("--filter")
    q.set_defaults(func=list_scenarios)

    p = sub.add_parser("compare-codegen", help="baseline-vs-candidate binary comparison (sections, symbols, functions, ABI)")
    p.add_argument("--baseline", default="HEAD", help="git ref built as the baseline (default HEAD)")
    p.add_argument("--candidate", default="WORKTREE", help="git ref, or WORKTREE for the current checkout (default)")
    p.add_argument("--baseline-bin")
    p.add_argument("--candidate-bin")
    p.add_argument("--functions", default=r"hyprimate::", help="regex selecting demangled functions to disassemble")
    p.add_argument("--run-bench", action="store_true", help="also run interleaved benchmarks on both builds")
    p.add_argument("--rounds", type=int, default=4)
    p.add_argument("--samples", type=int, default=20)
    p.add_argument("--bench-baseline")
    p.add_argument("--bench-candidate")
    p.set_defaults(func=codegen.cmd_compare_codegen)
    return parser


def prepare_native_bench() -> Path:
    directory = configure("default")
    result = build(directory, ["hyprimate-bench", "hyprimate"])
    if result.returncode:
        raise ToolError(f"native benchmark failed to build:\n{tail(result.stdout, 40)}")
    return bench.bench_binary(directory)


def measure_compositor(binary: Path, scenario: str, args) -> dict:
    started = perf_counter_ns()
    samples, warmups = [], []
    with compositor_bench.CompositorBench(binary, args.windows) as compositor:
        for index in range(args.warmup + args.samples):
            receipt = compositor.sample(scenario)
            (warmups if index < args.warmup else samples).append(receipt)

    checksums = [sample["checksum"] for sample in samples]
    warmup_checksums = [sample["checksum"] for sample in warmups]
    timings = [sample["ns_per_run"] for sample in samples]
    deterministic = len(set(checksums + warmup_checksums)) == 1
    row = {
        "name": scenario,
        "deterministic": deterministic, "checksum": checksums[0],
        "allocations": None, "alloc_bytes": None,
        "ns_per_run": timings,
        "timer": samples[0]["scope_metric"],
        "wall_ns": perf_counter_ns() - started,
        "script_wall_ns_per_run": [sample["runner_wall_ns"] for sample in samples],
        "checksums": checksums, "warmup_checksums": warmup_checksums,
        "semantic_workloads": [sample["semantic"] for sample in samples],
        "warmup_semantic_workloads": [sample["semantic"] for sample in warmups],
        "samples": args.samples, "warmup": args.warmup,
        "profiler_reports": [sample["profile"] for sample in samples],
    }
    if scenario == "windows.many":
        row["windows"] = args.windows
    return row


def run_native_bench(args, selected) -> int:
    start = perf_counter_ns()
    report = Report("bench-run")
    try:
        native = argparse.Namespace(samples=2 if args.samples is None else args.samples,
                                    warmup=0 if args.warmup is None else args.warmup, windows=args.windows)
        if native.samples < 2 or native.warmup < 0:
            raise ToolError("native benchmarks need at least two batches and a nonnegative warmup")
        binary = prepare_native_bench()
        build_wall_ns = perf_counter_ns() - start
        core_filter = ["--filter", args.filter] if args.filter else []
        listed = bench.run([binary, "--list", *core_filter], quiet=True)
        if listed.returncode:
            raise ToolError(f"could not list core benchmark scenarios: {tail(listed.stdout)}")
        environment = bench.environment()
        scenarios = []
        failed = False
        if listed.stdout.strip():
            with tempfile.TemporaryDirectory(prefix="hyprimate-core-bench-") as scratch:
                path = Path(scratch) / "core.json"
                core = bench.run([binary, "--samples", 30 if args.samples is None else args.samples,
                                  "--warmup", 5 if args.warmup is None else args.warmup,
                                  "--json", path, *core_filter], log=BUILD_ROOT / "logs" / "bench-run.log")
                print(core.stdout, end="")
                if core.returncode not in (0, 1):
                    raise ToolError(f"core benchmark exited with status {core.returncode}")
                scenarios = json.loads(path.read_text())["scenarios"]
                failed = core.returncode == 1

        for scenario in selected:
            row = measure_compositor(binary, scenario, native)
            scenarios.append(row)
            failed |= not row["deterministic"]
            tag = "" if row["deterministic"] else " CHECKSUM MISMATCH"
            smoke = " (determinism smoke)" if native.samples < 8 else ""
            print(f"{scenario}: {row['timer']} median {statistics.median(row['ns_per_run']) / 1e6:.3f} ms; "
                  f"private-session wall {row['wall_ns'] / 1e9:.3f} s; {native.samples} batches{smoke}{tag}",
                  flush=True)
        output = bench.BENCH_DIR / f"{args.name}.json"
        data = {"schema": 1, "scenarios": scenarios, "build_wall_ns": build_wall_ns,
                "runner_wall_ns": perf_counter_ns() - start}
        if listed.stdout.strip():
            data["warmup"] = 5 if args.warmup is None else args.warmup
        return bench.publish_run(report, output, data, environment, "fail" if failed else "pass")
    except (ToolError, RuntimeError, OSError, ValueError, SubprocessError) as error:
        report.add("bench", "tool_failed", summary=str(error))
    return report.write()


def run_bench(args) -> int:
    selected = [scenario for scenario in compositor_bench.SCENARIOS
                if not args.filter or args.filter in scenario]
    if selected:
        return run_native_bench(args, selected)
    args.samples = 30 if args.samples is None else args.samples
    args.warmup = 5 if args.warmup is None else args.warmup
    try:
        return bench.cmd_bench_run(args)
    except (RuntimeError, ValueError, SubprocessError) as error:
        raise ToolError(str(error)) from error


def list_scenarios(args) -> int:
    binary = bench.prepare_bench()
    core_filter = ["--filter", args.filter] if args.filter else []
    listed = bench.run([binary, "--list", *core_filter], quiet=True)
    if listed.returncode:
        raise ToolError(f"could not list core benchmark scenarios: {tail(listed.stdout)}")
    if listed.stdout:
        print(listed.stdout, end="")
    for scenario, description in compositor_bench.SCENARIOS.items():
        if not args.filter or args.filter in scenario:
            print(f"{scenario:<28}    native    {description}")
    return 0


def main() -> int:
    args = build_parser().parse_args()
    try:
        return args.func(args)
    except ToolError as error:
        print(f"tool error: {error}", file=sys.stderr)
        return EXIT_TOOL
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
