"""sanitize, memcheck and coverage: isolated instrumented runs of the standalone test executables.

These cover the Hyprland-independent core (animation models, tracker, material/EGL tests). The plugin itself is never
instrumented or loaded into a compositor here: a sanitizer runtime must be the first library in a process, which the
host Hyprland binary is not, and an unstable plugin must not run in the user's session.
"""

from __future__ import annotations

import json
import re
import shutil
import sys
from pathlib import Path

from .common import (BUILD_ROOT, REPORT_ROOT, Report, ToolError, build, configure, run, tail,
                     test_targets, tool_path)

SANITIZER_MARKERS = ("ERROR: AddressSanitizer", "ERROR: LeakSanitizer", "runtime error:", "ERROR: UndefinedBehaviorSanitizer")


def ctest_tests(directory: Path) -> list[dict]:
    result = run(["ctest", "--test-dir", directory, "--show-only=json-v1"], quiet=True)
    data = json.loads(result.stdout)
    tests = []
    for test in data["tests"]:
        env = {}
        for prop in test.get("properties", []):
            if prop["name"] == "ENVIRONMENT":
                for item in prop["value"]:
                    key, _, value = item.partition("=")
                    env[key] = value
        tests.append({"name": test["name"], "command": test["command"], "env": env})
    return tests


def native_tests(directory: Path, targets: list[str]) -> list[dict]:
    return [t for t in ctest_tests(directory) if t["name"] in targets]


def prepare(profile: str, label: str | None = None) -> tuple[Path, list[str]]:
    directory = configure(profile, label=label)
    targets = test_targets(directory)
    built = build(directory, targets)
    if built.returncode:
        raise ToolError(f"{profile} build failed:\n" + tail(built.stdout, 40))
    return directory, targets


# --------------------------------------------------------------------------------------- sanitize
SANITIZER_ENV = {
    "asan": {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:abort_on_error=0:symbolize=1:strict_string_checks=1:"
                              "detect_stack_use_after_return=1:check_initialization_order=1:print_summary=1:fast_unwind_on_malloc=0"},
    "asan-lsan": {"LSAN_OPTIONS": f"suppressions={Path(__file__).resolve().parents[1] / 'lsan.supp'}:print_suppressions=0"},
    "ubsan": {"UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1:abort_on_error=0:symbolize=1"},
}


def run_sanitizer(kind: str, args) -> Report:
    report = Report(f"sanitize-{kind}")
    try:
        directory, targets = prepare(kind)
    except ToolError as error:
        report.add(kind, "tool_failed", summary=str(error))
        return report
    symbolizer = tool_path("llvm-symbolizer")
    env = dict(SANITIZER_ENV[kind])
    if kind == "asan":
        env.update(SANITIZER_ENV["asan-lsan"])
    if symbolizer:
        env["ASAN_SYMBOLIZER_PATH"] = env["UBSAN_SYMBOLIZER_PATH"] = symbolizer
    exclude = re.compile(args.exclude) if args.exclude else None
    failures, notes, ran = [], [], 0
    for test in native_tests(directory, targets):
        if exclude and exclude.search(test["name"]):
            notes.append(f"{test['name']}: excluded")
            continue
        ran += 1
        log = BUILD_ROOT / "logs" / f"{kind}-{test['name']}.log"
        result = run(test["command"], env={**env, **test["env"]}, log=log, quiet=True, timeout=args.timeout)
        flagged = [m for m in SANITIZER_MARKERS if m in result.stdout]
        if flagged:
            failures.append({"test": test["name"], "kind": "sanitizer-report", "markers": flagged,
                             "excerpt": first_report(result.stdout), "log": str(log.relative_to(BUILD_ROOT.parent.parent))})
        elif result.returncode:
            failures.append({"test": test["name"], "kind": "test-failure", "exit": result.returncode,
                             "excerpt": tail(result.stdout, 15), "log": str(log.relative_to(BUILD_ROOT.parent.parent))})
        print(f"  {test['name']}: {'FAIL' if flagged or result.returncode else 'ok'}", file=sys.stderr)
    scope = "standalone core test executables only; not the plugin, not a live compositor"
    report.add(kind, "fail" if failures else "pass", tests_run=ran, failures=failures, notes=notes, scope=scope,
               summary=f"{ran} test executables, {len(failures)} failing ({scope})")
    return report


def first_report(text: str) -> str:
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if any(m in line for m in SANITIZER_MARKERS):
            return "\n".join(lines[index:index + 20])
    return tail(text, 15)


def cmd_sanitize(args) -> int:
    kinds = ["asan", "ubsan"] if args.kind == "all" else [args.kind]
    report = Report("sanitize")
    for kind in kinds:
        report.sections.extend(run_sanitizer(kind, args).sections)
    return report.write()


# --------------------------------------------------------------------------------------- memcheck
def cmd_memcheck(args) -> int:
    report = Report("memcheck")
    if tool_path("valgrind") is None:
        report.add("valgrind", "unavailable", summary="valgrind not installed")
        return report.write(require_tools=True)
    try:
        directory, targets = prepare("default")
    except ToolError as error:
        report.add("valgrind", "tool_failed", summary=str(error))
        return report.write()
    exclude = re.compile(args.exclude) if args.exclude else None
    failures, ran = [], 0
    out_dir = REPORT_ROOT / "memcheck"
    out_dir.mkdir(parents=True, exist_ok=True)
    for test in native_tests(directory, targets):
        if args.only and not re.search(args.only, test["name"]):
            continue
        if exclude and exclude.search(test["name"]):
            continue
        ran += 1
        xml = out_dir / f"{test['name']}.xml"
        cmd = ["valgrind", "--tool=memcheck", "--error-exitcode=99", "--leak-check=full",
               "--show-leak-kinds=definite,possible", "--track-origins=yes", "--num-callers=30",
               "--xml=yes", f"--xml-file={xml}", *test["command"]]
        log = BUILD_ROOT / "logs" / f"memcheck-{test['name']}.log"
        result = run(cmd, env=test["env"], log=log, quiet=True, timeout=args.timeout)
        errors = len(re.findall(r"<error>", xml.read_text(errors="replace"))) if xml.exists() else None
        if result.returncode == 99 or errors:
            failures.append({"test": test["name"], "valgrind_errors": errors, "xml": str(xml.relative_to(BUILD_ROOT.parent.parent))})
        elif result.returncode:
            failures.append({"test": test["name"], "kind": "test-failure", "exit": result.returncode,
                             "excerpt": tail(result.stdout, 15)})
        print(f"  {test['name']}: {'FAIL' if result.returncode else 'ok'}", file=sys.stderr)
    report.add("valgrind-memcheck", "fail" if failures else "pass", tests_run=ran, failures=failures,
               scope="standalone core test executables on the RelWithDebInfo default build",
               summary=f"{ran} test executables under memcheck, {len(failures)} with errors")
    return report.write()


# --------------------------------------------------------------------------------------- coverage
COVERAGE_IGNORE = r"(/_deps/|/usr/|/generated/|/tests/|/build/|/src/window/[a-z]+/tests/|Tests?\.cpp$)"


def cmd_coverage(args) -> int:
    report = Report("coverage")
    if tool_path("llvm-cov") is None or tool_path("llvm-profdata") is None:
        report.add("llvm-cov", "unavailable", summary="llvm-cov/llvm-profdata not installed (pacman -S llvm)")
        return report.write(require_tools=True)
    try:
        directory, targets = prepare("coverage")
    except ToolError as error:
        report.add("coverage", "tool_failed", summary=str(error))
        return report.write()
    out = REPORT_ROOT / "coverage"
    shutil.rmtree(out, ignore_errors=True)
    raw = out / "raw"
    raw.mkdir(parents=True)
    env = {"LLVM_PROFILE_FILE": str(raw / "%p-%m.profraw")}
    failures = []
    for test in native_tests(directory, targets):
        result = run(test["command"], env={**env, **test["env"]}, quiet=True, timeout=args.timeout,
                     log=BUILD_ROOT / "logs" / f"coverage-{test['name']}.log")
        if result.returncode:
            failures.append(test["name"])
    if failures:
        report.add("tests", "fail", failures=failures, summary=f"{len(failures)} test executables failed under coverage")
    raws = sorted(raw.glob("*.profraw"))
    if not raws:
        report.add("llvm-cov", "tool_failed", summary="no profile data produced")
        return report.write()
    profdata = out / "coverage.profdata"
    merged = run(["llvm-profdata", "merge", "-sparse", *raws, "-o", profdata], quiet=True)
    if merged.returncode:
        report.add("llvm-profdata", "tool_failed", output=tail(merged.stdout), summary="merge failed")
        return report.write()
    binaries = [directory / "hyprimate" / t for t in targets if (directory / "hyprimate" / t).exists()]
    objects = [arg for b in binaries[1:] for arg in ("-object", str(b))]
    common = [f"-instr-profile={profdata}", f"-ignore-filename-regex={COVERAGE_IGNORE}"]
    sources = [str(Path(__file__).resolve().parents[3] / "src")]
    run(["llvm-cov", "report", str(binaries[0]), *objects, *common, "-show-branch-summary", *sources],
               log=out / "summary.txt", quiet=True)
    export = run(["llvm-cov", "export", str(binaries[0]), *objects, *common, "-summary-only", *sources],
                 quiet=True, split_stderr=True)
    lcov = run(["llvm-cov", "export", str(binaries[0]), *objects, *common, "-format=lcov", *sources],
               quiet=True, split_stderr=True)
    (out / "coverage.lcov").write_text(lcov.stdout)
    run(["llvm-cov", "show", str(binaries[0]), *objects, *common, "-format=html", "-show-branches=count",
         "-show-line-counts-or-regions", f"-output-dir={out / 'html'}", *sources], quiet=True)
    totals = {}
    try:
        totals = json.loads(export.stdout)["data"][0]["totals"]
        (out / "summary.json").write_text(json.dumps(totals, indent=1))
    except (ValueError, KeyError, IndexError):
        report.add("llvm-cov", "tool_failed", summary="could not parse llvm-cov export", output=tail(export.stdout))
        return report.write()
    short = {k: f"{totals[k]['percent']:.1f}% ({totals[k]['covered']}/{totals[k]['count']})"
             for k in ("lines", "regions", "branches", "functions") if k in totals}
    baseline_path = Path(__file__).resolve().parents[1] / "baselines" / "coverage.json"
    if args.update_baseline:
        baseline_path.write_text(json.dumps({k: {"percent": totals[k]["percent"], "covered": totals[k]["covered"],
                                                 "count": totals[k]["count"]} for k in short}, indent=1) + "\n")
    delta = {}
    if baseline_path.exists():
        base = json.loads(baseline_path.read_text())
        delta = {k: round(totals[k]["percent"] - base[k]["percent"], 2) for k in short if k in base}
    report.add("llvm-cov", "pass", totals=short, delta_vs_baseline_percent_points=delta,
               text_report=str((out / "summary.txt").relative_to(BUILD_ROOT.parent.parent)),
               html=str((out / "html" / "index.html").relative_to(BUILD_ROOT.parent.parent)),
               lcov=str((out / "coverage.lcov").relative_to(BUILD_ROOT.parent.parent)),
               scope="src/ only, covered by the standalone test executables; plugin glue (main, renderers, "
                     "live styles) is not linked into any test and does not appear",
               summary=", ".join(f"{k} {v}" for k, v in short.items()))
    return report.write()
