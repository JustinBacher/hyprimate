"""check, analyze and doctor."""

from __future__ import annotations

import json
import os
import re
import shutil
import sys
from pathlib import Path

from . import catalog, diagnostics
from .common import (BUILD_ROOT, GENERATORS, ROOT, Report, ToolError, build, configure,
                     run, tail, test_targets, tool_path, tool_version)

FORMAT_GLOBS = ("src/**/*.cpp", "src/**/*.hpp", "tests/**/*.cpp", "tests/**/*.hpp")  # mirrors the `format` target


def format_files() -> list[Path]:
    files: list[Path] = []
    for pattern in FORMAT_GLOBS:
        files.extend(sorted(ROOT.glob(pattern)))
    return files


def jobs() -> int:
    return max(1, min(32, (os.cpu_count() or 2) // 2))


# --------------------------------------------------------------------------------------- doctor
def cmd_doctor(args) -> int:
    report = Report("doctor")
    # Without --for, only what everyday development needs is required; the heavier tools are per-command.
    wanted = [args.command_for] if args.command_for else ["check", "analyze"]
    required = {t for c in wanted for t in catalog.REQUIRED.get(c, [])}
    optional = {t for c in wanted for t in catalog.OPTIONAL.get(c, [])}
    rows, missing_required = [], []
    for name in catalog.TOOLS:
        info = catalog.probe(name)
        role = "required" if name in required else "optional" if name in optional else "other"
        info["role"] = role
        rows.append(info)
        if role == "required" and not info["found"]:
            missing_required.append(name)
    for info in rows:
        mark = "ok     " if info["found"] else "MISSING"
        extra = (info["version"] or "")[:60] if info["found"] else f"-> {info['install']}"
        print(f"{mark} {info['role']:<9} {info['tool']:<22} {extra}", file=sys.stderr)
    system = catalog.system_checks()
    for item in system:
        print(f"info    {item['check']:<22} {item.get('value')}  {item.get('note', '')}", file=sys.stderr)
    broken = [i for i in system if i["check"] == "hyprland-dev-headers" and not i["found"]]
    status = "tool_failed" if missing_required or broken else "pass"
    report.add("environment", status, tools=rows, system=system, missing_required=missing_required,
               summary=f"{len(missing_required)} required tools missing" if missing_required else "required tools present")
    return report.write()


# ---------------------------------------------------------------------------------------- check
def cmd_check(args) -> int:
    report = Report("check")
    clang_format = tool_path("clang-format")
    if clang_format is None:
        report.add("format-check", "tool_failed", summary="clang-format not found")
    else:
        files = format_files()
        log = BUILD_ROOT / "logs" / "clang-format.log"
        result = run([clang_format, "--dry-run", "--Werror", *files], quiet=True, log=log)
        rx = re.compile(r"^(/[^:]+):(\d+):(\d+): error: (code should be clang-formatted) .*$")
        found = []
        for text in result.stdout.splitlines():
            m = rx.match(text)
            rel = diagnostics.relativize(m.group(1)) if m else None
            if m and rel:
                found.append(diagnostics.Finding(rel, int(m.group(2)), int(m.group(3)), "error", "clang-format", m.group(4)))
        if result.returncode and not found:
            report.add("format-check", "tool_failed", summary="clang-format failed", output=tail(result.stdout))
        else:
            # Non-conforming files predate this tooling, so formatting is a per-file ratchet, not a blanket failure.
            classify_into(report, "format-check", found, args, blocking_new=True, log=log,
                          version=tool_version("clang-format") or "", baseline_name="format")
    try:
        directory = configure("default")
        tests = test_targets(directory)
        targets = [*tests, "hyprimate-bench"] + ([] if args.no_plugin else ["hyprimate"])
        built = build(directory, targets)
        if built.returncode:
            report.add("build", "fail", output=tail(built.stdout, 40), summary="project does not build")
        else:
            report.add("build", "pass", summary=f"{len(targets)} targets")
            ctest = run(["ctest", "--test-dir", directory, "--output-on-failure", "-j", str(jobs())],
                        log=BUILD_ROOT / "logs" / "ctest-default.log")
            summary = tail(ctest.stdout, 4).replace("\n", " | ")
            report.add("ctest", "fail" if ctest.returncode else "pass", summary=summary,
                       output=tail(ctest.stdout, 60) if ctest.returncode else "")
    except ToolError as error:
        report.add("build", "tool_failed", summary=str(error))
    if args.tidy:
        merge_sections(report, run_tidy(args))
    return report.write()


def merge_sections(report: Report, other: Report) -> None:
    report.sections.extend(other.sections)


# -------------------------------------------------------------------------------------- analyze
def prepare_clang_db() -> Path:
    directory = configure("clang")
    built = build(directory, GENERATORS)
    if built.returncode:
        raise ToolError("generated headers failed to build:\n" + tail(built.stdout))
    return directory


def run_tidy(args) -> Report:
    report = Report("tidy")
    try:
        directory = prepare_clang_db()
        log = BUILD_ROOT / "logs" / "clang-tidy.log"
        result = run([sys.executable, ROOT / "tools" / "check_clang_tidy.py", "--build-dir", directory,
                      "--jobs", str(jobs())], log=log)
    except ToolError as error:
        report.add("clang-tidy", "tool_failed", summary=str(error))
        return report
    findings = diagnostics.parse_tidy(result.stdout)
    if not findings and result.returncode:
        report.add("clang-tidy", "tool_failed", summary="clang-tidy failed without diagnostics", output=tail(result.stdout))
        return report
    return classify_into(report, "clang-tidy", findings, args, blocking_new=True, log=log,
                         version=tool_version("clang-tidy") or "")


def classify_into(report: Report, name: str, findings, args, *, blocking_new: bool, log: Path,
                  version: str, baseline_name: str | None = None) -> Report:
    baseline_name = baseline_name or name
    wanted = {baseline_name, name, "all"} | ({"tidy"} if name == "clang-tidy" else set())
    if args.update_baseline in wanted:
        path = diagnostics.write_baseline(baseline_name, version, findings)
        report.add(f"{name} baseline", "pass", summary=f"wrote {path.relative_to(ROOT)} ({len(findings)} findings)")
    result = diagnostics.classify(findings, diagnostics.load_baseline(baseline_name), diagnostics.load_policy())
    payload = diagnostics.as_json(result)
    payload.update(log=str(log.relative_to(ROOT)), total=len(findings))
    if not result["has_baseline"]:
        status = "advisory"
        note = " (no baseline recorded: run with --update-baseline to record one)"
    elif result["new"]:
        status = "fail" if blocking_new else "advisory"
        note = ""
    else:
        status = "pass" if not payload["existing_errors"] else "advisory"
        note = ""
    errors = [f for f in result["new"] if f.severity == "error"]
    if errors:
        status = "fail"
    report.add(name, status, summary=diagnostics.summarize(result, len(findings)) + note, **payload)
    for finding in result["new"][:20]:
        print(f"  new: {finding.where()}: {finding.severity}: {finding.message} [{finding.check}]", file=sys.stderr)
    return report


def run_cppcheck(args) -> Report:
    report = Report("cppcheck")
    try:
        directory = prepare_clang_db()
        log = BUILD_ROOT / "logs" / "cppcheck.log"
        result = run([ROOT / "tools" / "check_unused.sh", directory], log=log)
    except ToolError as error:
        report.add("cppcheck", "tool_failed", summary=str(error))
        return report
    if result.returncode:
        report.add("cppcheck", "tool_failed", summary=f"check_unused.sh exited {result.returncode}", output=tail(result.stdout))
        return report
    findings = diagnostics.parse_cppcheck(result.stdout)
    return classify_into(report, "cppcheck", findings, args, blocking_new=False, log=log,
                         version=tool_version("cppcheck") or "")


def run_scan_build(args) -> Report:
    report = Report("scan-build")
    if tool_path("scan-build") is None:
        report.add("scan-build", "unavailable", summary="scan-build not installed")
        return report
    directory = BUILD_ROOT / "scan-build"
    out = BUILD_ROOT / "reports" / "scan-build-html"
    wrapper_cmake = ROOT / "tools" / "dev"
    exclude = ["--exclude", str(ROOT / "build" / "_deps")]
    try:
        # Reconfigure under scan-build so CMake records its compiler wrappers; the analyzer then sees the real flags.
        if not (directory / "CMakeCache.txt").exists():
            shutil.rmtree(directory, ignore_errors=True)
            cfg = ["scan-build", "--use-analyzer=" + (tool_path("clang") or "clang"), "cmake", "-S", wrapper_cmake,
                   "-B", directory, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                   f"-DHYPRIMATE_SOURCE_DIR={ROOT}"]
            result = run(cfg, log=BUILD_ROOT / "logs" / "scan-build-configure.log")
            if result.returncode:
                raise ToolError("scan-build configure failed:\n" + tail(result.stdout))
        shutil.rmtree(out, ignore_errors=True)
        log = BUILD_ROOT / "logs" / "scan-build.log"
        # A clean build every time: ninja would skip up-to-date objects and analyze nothing.
        run(["cmake", "--build", directory, "--target", "clean"], quiet=True)
        result = run(["scan-build", "--use-analyzer=" + (tool_path("clang") or "clang"), *exclude,
                      "-o", out, "-enable-checker", "alpha.core.CastSize", "cmake", "--build", directory,
                      "--target", "hyprimate-core", "hyprimate"], log=log)
    except ToolError as error:
        report.add("scan-build", "tool_failed", summary=str(error))
        return report
    if "error:" in result.stdout and "scan-build: 0 bugs found" not in result.stdout and result.returncode:
        compile_errors = [l for l in result.stdout.splitlines() if ": error:" in l and "[" not in l.split(": error:")[-1][-40:]]
        if compile_errors:
            report.add("scan-build", "tool_failed", summary="build failed under scan-build", output=tail(result.stdout))
            return report
    findings = diagnostics.parse_tidy(result.stdout)
    bugs = re.search(r"scan-build: (\d+) bugs? found", result.stdout)
    report = classify_into(report, "scan-build", findings, args, blocking_new=False, log=log,
                           version=tool_version("clang", ("--version",)) or "")
    report.sections[-1].data["html_report"] = str(out.relative_to(ROOT)) if out.exists() else None
    report.sections[-1].data["scan_build_reported_bugs"] = int(bugs.group(1)) if bugs else None
    return report


def run_iwyu(args) -> Report:
    report = Report("iwyu")
    tool = tool_path("iwyu_tool.py")
    if tool is None or tool_path("include-what-you-use") is None:
        report.add("include-what-you-use", "unavailable",
                   summary="include-what-you-use / iwyu_tool.py not installed (AUR: include-what-you-use)")
        return report
    try:
        directory = prepare_clang_db()
    except ToolError as error:
        report.add("include-what-you-use", "tool_failed", summary=str(error))
        return report
    sources = [str(p) for base in ("src", "tests") for p in sorted((ROOT / base).rglob("*.cpp"))]
    log = BUILD_ROOT / "reports" / "iwyu.txt"
    # Recommendations only: nothing is applied. Hyprland's headers are an external dependency whose include layout
    # is not ours to rewrite, so keep every include that resolves under hyprland/.
    result = run([sys.executable, tool, "-p", directory, "-j", str(jobs()), *sources, "--",
                  "-Xiwyu", "--keep=*hyprland/*", "-Xiwyu", "--no_comments", "-Xiwyu", "--cxx17ns"], log=log)
    adds = len(re.findall(r"^(.*) should add these lines:", result.stdout, re.M))
    removes = len(re.findall(r"^(.*) should remove these lines:", result.stdout, re.M))
    report.add("include-what-you-use", "advisory" if adds or removes else "pass",
               summary=f"{adds} files missing includes, {removes} files with removable includes; review {log.relative_to(ROOT)}",
               files_to_add=adds, files_to_remove=removes, log=str(log.relative_to(ROOT)))
    return report


def run_codeql(args) -> Report:
    report = Report("codeql")
    if tool_path("codeql") is None:
        report.add("codeql", "unavailable", summary="codeql CLI not installed (optional; see tools/dev/README.md)")
        return report
    database = BUILD_ROOT / "codeql-db"
    sarif = BUILD_ROOT / "reports" / "codeql.sarif"
    suite = args.codeql_suite
    try:
        directory = configure("default", label="codeql")
        command = f"cmake --build {directory} --clean-first --target hyprimate-core hyprimate"
        created = run(["codeql", "database", "create", database, "--language=cpp", f"--source-root={ROOT}",
                       f"--command={command}", "--overwrite"], log=BUILD_ROOT / "logs" / "codeql-create.log")
        if created.returncode:
            raise ToolError("codeql database create failed:\n" + tail(created.stdout))
        sarif.parent.mkdir(parents=True, exist_ok=True)
        analyzed = run(["codeql", "database", "analyze", database, suite, "--format=sarif-latest",
                        f"--output={sarif}", "--sarif-category=hyprimate"], log=BUILD_ROOT / "logs" / "codeql-analyze.log")
        if analyzed.returncode:
            raise ToolError("codeql database analyze failed:\n" + tail(analyzed.stdout))
    except ToolError as error:
        report.add("codeql", "tool_failed", summary=str(error))
        return report
    data = json.loads(sarif.read_text())
    results = [r for run_ in data.get("runs", []) for r in run_.get("results", [])]
    findings = []
    for r in results:
        loc = r["locations"][0]["physicalLocation"]
        rel = diagnostics.relativize(str(ROOT / loc["artifactLocation"]["uri"]))
        if rel is None or rel.startswith("build/"):
            continue
        region = loc.get("region", {})
        findings.append(diagnostics.Finding(rel, region.get("startLine", 0), region.get("startColumn", 0),
                                            r.get("level", "warning"), r["ruleId"], r["message"]["text"]))
    return classify_into(report, "codeql", findings, args, blocking_new=False, log=sarif,
                         version=tool_version("codeql", ("version",)) or "")


ANALYZERS = {"tidy": run_tidy, "cppcheck": run_cppcheck, "scan-build": run_scan_build, "iwyu": run_iwyu,
             "codeql": run_codeql}
DEFAULT_ANALYZERS = ("tidy", "cppcheck")


def cmd_analyze(args) -> int:
    chosen = list(ANALYZERS) if args.all else (args.only.split(",") if args.only else list(DEFAULT_ANALYZERS))
    unknown = [c for c in chosen if c not in ANALYZERS]
    if unknown:
        print(f"unknown analyzer(s): {', '.join(unknown)}; choose from {', '.join(ANALYZERS)}", file=sys.stderr)
        return 2
    report = Report("analyze")
    for name in chosen:
        merge_sections(report, ANALYZERS[name](args))
    inventory = diagnostics.nolint_inventory()
    report.add("approved-exceptions", "pass" if not inventory["without_explanation"] else "advisory",
               summary=f"{inventory['total']} NOLINT suppressions, {len(inventory['without_explanation'])} without explanation",
               **inventory)
    policy = diagnostics.load_policy()
    report.add("policy", "pass", summary=f"{len(policy.get('deferred', []))} deferred rules, "
               f"{len(policy.get('frozen_files', []))} frozen files untouched by tooling",
               frozen_files=policy.get("frozen_files", []))
    return report.write(require_tools=args.require_all)
