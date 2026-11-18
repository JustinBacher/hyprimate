#!/usr/bin/env python3
"""Hyprimate development toolchain: one entry point over the isolated diagnostic builds.

Nothing here changes the production build. Every instrumented or analysis build lives in build/dev/<profile>, is
configured by tools/dev/CMakeLists.txt (which adds the unmodified root project as a subdirectory) and receives its
flags on the command line. Reports are written to build/dev/reports/<command>.json.

Exit codes: 0 ok (advisories allowed) | 1 blocking finding (defect, regression) | 2 usage | 3 tool/environment failure.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "dev"))

from lib import bench, codegen, perf, runtime, static  # noqa: E402
from lib.common import EXIT_TOOL, ToolError  # noqa: E402


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
    q.add_argument("--samples", type=int, default=30)
    q.add_argument("--warmup", type=int, default=5)
    q.set_defaults(func=bench.cmd_bench_run)
    q = bsub.add_parser("compare", help="compare two result files")
    q.add_argument("baseline")
    q.add_argument("candidate")
    q.add_argument("--threshold", type=float, default=3.0, help="minimum median change, percent (default 3)")
    q.add_argument("--alpha", type=float, default=0.01, help="Mann-Whitney significance (default 0.01)")
    q.add_argument("--noise-cv", type=float, default=0.10)
    q.set_defaults(func=bench.cmd_bench_compare)
    q = bsub.add_parser("list", help="list scenarios")
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


def list_scenarios(_args) -> int:
    binary = bench.prepare_bench()
    print(bench.run([binary, "--list"], quiet=True).stdout, end="")
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
