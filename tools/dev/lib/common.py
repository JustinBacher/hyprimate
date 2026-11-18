"""Shared plumbing for tools/dev.py: paths, isolated build profiles, subprocess helpers and reports."""

from __future__ import annotations

import json
import os
import shlex
import shutil
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEV_WRAPPER = ROOT / "tools" / "dev"
BUILD_ROOT = ROOT / "build" / "dev"
REPORT_ROOT = BUILD_ROOT / "reports"
BASELINE_DIR = DEV_WRAPPER / "baselines"
POLICY_FILE = DEV_WRAPPER / "policy.json"

# Exit codes. A finding that should block a merge and a broken tool are different things.
EXIT_OK = 0
EXIT_DEFECT = 1  # blocking finding: failed test, new blocking diagnostic, regression
EXIT_USAGE = 2
EXIT_TOOL = 3  # missing/crashed tool or environment problem; says nothing about the code


class ToolError(Exception):
    """A tool or the environment failed; maps to EXIT_TOOL."""


@dataclass(frozen=True)
class Profile:
    compiler: str | None = None
    cxxflags: tuple[str, ...] = ()
    ldflags: tuple[str, ...] = ()
    env: tuple[tuple[str, str], ...] = ()
    note: str = ""


# Every profile builds in build/dev/<name>; none of them touches build/ or the production CMakeLists.txt.
# "default" uses the project's own compiler and flags, so measurements taken on it describe the production build.
PROFILES: dict[str, Profile] = {
    "default": Profile(note="project compiler and flags, as the production build"),
    "clang": Profile("clang++", note="clang compilation database for tidy/IWYU/CodeQL"),
    "asan": Profile("clang++", ("-fsanitize=address", "-fno-omit-frame-pointer", "-fno-sanitize-recover=address"),
                    ("-fsanitize=address",), note="AddressSanitizer + LeakSanitizer"),
    "ubsan": Profile("clang++", ("-fsanitize=undefined", "-fno-sanitize-recover=undefined", "-fno-omit-frame-pointer"),
                     ("-fsanitize=undefined",), note="UndefinedBehaviorSanitizer, aborts on first report"),
    "coverage": Profile("clang++", ("-fprofile-instr-generate", "-fcoverage-mapping"),
                        ("-fprofile-instr-generate",), note="LLVM source-based coverage"),
    "timetrace": Profile("clang++", ("-ftime-trace",), note="per-TU compile time traces"),
    "scan-build": Profile(note="configured under scan-build's compiler wrappers"),
}


def tool_path(name: str) -> str | None:
    return shutil.which(name)


def require(name: str, hint: str = "") -> str:
    found = shutil.which(name)
    if found is None:
        raise ToolError(f"required tool not found: {name}" + (f" ({hint})" if hint else ""))
    return found


def tool_version(name: str, args: tuple[str, ...] = ("--version",)) -> str | None:
    path = shutil.which(name)
    if path is None:
        return None
    try:
        out = subprocess.run([path, *args], capture_output=True, text=True, timeout=20, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return "unknown"
    text = (out.stdout or out.stderr).strip().splitlines()
    for line in text:
        if "version" in line.lower():
            return line.strip()
    return text[0].strip() if text else "unknown"


def run(cmd, *, cwd: Path | None = None, env: dict | None = None, log: Path | None = None,
        quiet: bool = False, timeout: float | None = None, split_stderr: bool = False) -> subprocess.CompletedProcess:
    """Run a command, teeing combined output to `log`; raise ToolError only if it cannot be started.

    `split_stderr` keeps stderr out of stdout, for tools whose stdout is machine-readable."""
    full_env = dict(os.environ)
    full_env.update(env or {})
    argv = [str(part) for part in cmd]
    if not quiet:
        print("+ " + shlex.join(argv), file=sys.stderr, flush=True)
    try:
        result = subprocess.run(argv, cwd=cwd or ROOT, env=full_env, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE if split_stderr else subprocess.STDOUT, text=True,
                                errors="replace", check=False, timeout=timeout)
    except OSError as error:
        raise ToolError(f"cannot execute {argv[0]}: {error}") from error
    except subprocess.TimeoutExpired as error:
        raise ToolError(f"{argv[0]} timed out after {timeout}s") from error
    if log is not None:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(result.stdout)
    return result


def tail(text: str, lines: int = 25) -> str:
    return "\n".join(text.splitlines()[-lines:])


def git_state() -> dict:
    def git(*args: str) -> str:
        out = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=False)
        return out.stdout.strip()
    return {"commit": git("rev-parse", "HEAD"), "dirty": bool(git("status", "--porcelain", "--untracked-files=no"))}


def build_dir(profile: str, label: str | None = None) -> Path:
    return BUILD_ROOT / (f"{profile}-{label}" if label else profile)


def configure(profile: str, *, label: str | None = None, source: Path | None = None,
              extra: tuple[str, ...] = (), cxxflags_extra: tuple[str, ...] = (),
              wrapper_env: dict | None = None) -> Path:
    """Configure (idempotently) the isolated build tree for `profile`; return its directory."""
    spec = PROFILES[profile]
    directory = build_dir(profile, label)
    cmake = require("cmake")
    require("ninja", "pacman -S ninja")
    source = source or ROOT
    args = [cmake, "-S", DEV_WRAPPER, "-B", directory, "-G", "Ninja", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo", f"-DHYPRIMATE_SOURCE_DIR={source}"]
    if spec.compiler:
        require(spec.compiler)
        args.append(f"-DCMAKE_CXX_COMPILER={spec.compiler}")
    if spec.cxxflags or cxxflags_extra:
        args.append("-DCMAKE_CXX_FLAGS=" + " ".join((*spec.cxxflags, *cxxflags_extra)))
    if spec.ldflags:
        flags = " ".join(spec.ldflags)
        args += [f"-DCMAKE_EXE_LINKER_FLAGS={flags}", f"-DCMAKE_SHARED_LINKER_FLAGS={flags}"]
    # Jolt is pinned by hash in the root CMakeLists.txt; reuse an existing download instead of fetching again.
    cache = BUILD_ROOT / "_jolt-src"
    for candidate in (source / "build" / "_deps" / "joltphysics-src", ROOT / "build" / "_deps" / "joltphysics-src", cache):
        if (candidate / "Build" / "CMakeLists.txt").exists():
            args += [f"-DFETCHCONTENT_SOURCE_DIR_JOLTPHYSICS={candidate}", "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"]
            break
    args += list(extra)
    result = run(args, log=directory.parent / "logs" / f"configure-{directory.name}.log", env=wrapper_env)
    if result.returncode:
        raise ToolError(f"cmake configure failed for profile {profile}:\n{tail(result.stdout)}")
    fetched = directory / "_deps" / "joltphysics-src"
    if not cache.exists() and (fetched / "Build" / "CMakeLists.txt").exists():
        shutil.copytree(fetched, cache)  # one download shared by every profile (and cacheable in CI)
    return directory


def list_targets(directory: Path) -> list[str]:
    result = run(["ninja", "-C", directory, "-t", "targets", "all"], quiet=True)
    return [m.group(1) for m in (re.match(r"^([\w.+-]+): phony$", line) for line in result.stdout.splitlines()) if m]


def test_targets(directory: Path) -> list[str]:
    return sorted(t for t in list_targets(directory) if t.endswith("-tests"))


GENERATORS = ["generate-config", "generate-bubble-shaders", "generate-swarm-shaders", "generate-smoke-shaders"]


def build(directory: Path, targets: list[str], *, env: dict | None = None) -> subprocess.CompletedProcess:
    result = run(["cmake", "--build", directory, "--target", *targets], env=env,
                 log=directory.parent / "logs" / f"build-{directory.name}.log")
    return result


@dataclass
class Section:
    name: str
    status: str  # pass | advisory | fail | tool_failed | unavailable | skipped
    data: dict = field(default_factory=dict)


class Report:
    """Collects per-tool sections, prints a summary and chooses the exit code."""

    def __init__(self, command: str) -> None:
        self.command = command
        self.sections: list[Section] = []
        self.started = time.time()

    def add(self, name: str, status: str, **data) -> Section:
        section = Section(name, status, data)
        self.sections.append(section)
        detail = data.get("summary", "")
        print(f"[{status.upper():>11}] {name}" + (f": {detail}" if detail else ""), file=sys.stderr, flush=True)
        return section

    def exit_code(self, *, require_tools: bool = False) -> int:
        statuses = {s.status for s in self.sections}
        if "fail" in statuses:
            return EXIT_DEFECT
        if "tool_failed" in statuses or (require_tools and "unavailable" in statuses):
            return EXIT_TOOL
        return EXIT_OK

    def write(self, *, require_tools: bool = False) -> int:
        code = self.exit_code(require_tools=require_tools)
        REPORT_ROOT.mkdir(parents=True, exist_ok=True)
        path = REPORT_ROOT / f"{self.command}.json"
        payload = {"schema": 1, "command": self.command, "exit_code": code, "git": git_state(),
                   "seconds": round(time.time() - self.started, 1),
                   "sections": [{"name": s.name, "status": s.status, **s.data} for s in self.sections]}
        path.write_text(json.dumps(payload, indent=2, default=str) + "\n")
        print(f"report: {path.relative_to(ROOT)}  exit={code}", file=sys.stderr)
        return code
