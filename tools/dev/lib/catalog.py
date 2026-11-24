"""Tool catalogue: what each command needs, and how to get it (Arch Linux package names; nothing is auto-installed)."""

from __future__ import annotations

import os
from pathlib import Path

from .common import tool_path, tool_version

# name -> (executable, version args, install hint)
TOOLS: dict[str, tuple[str, tuple[str, ...], str]] = {
    "cmake": ("cmake", ("--version",), "pacman -S cmake"),
    "ninja": ("ninja", ("--version",), "pacman -S ninja"),
    "git": ("git", ("--version",), "pacman -S git"),
    "clang++": ("clang++", ("--version",), "pacman -S clang"),
    "clang-format": ("clang-format", ("--version",), "pacman -S clang"),
    "clang-tidy": ("clang-tidy", ("--version",), "pacman -S clang"),
    "cppcheck": ("cppcheck", ("--version",), "pacman -S cppcheck"),
    "scan-build": ("scan-build", ("--help",), "pacman -S clang (scan-build ships with clang)"),
    "include-what-you-use": ("include-what-you-use", ("--version",), "AUR: include-what-you-use"),
    "iwyu_tool.py": ("iwyu_tool.py", ("--help",), "ships with include-what-you-use"),
    "codeql": ("codeql", ("version",), "AUR: codeql, or the CodeQL CLI bundle from github.com/github/codeql-action/releases"),
    "valgrind": ("valgrind", ("--version",), "pacman -S valgrind"),
    "perf": ("perf", ("--version",), "pacman -S perf"),
    "heaptrack": ("heaptrack", ("--version",), "pacman -S heaptrack"),
    "heaptrack_print": ("heaptrack_print", ("--version",), "pacman -S heaptrack"),
    "ClangBuildAnalyzer": ("ClangBuildAnalyzer", ("--help",), "AUR: clangbuildanalyzer"),
    "llvm-cov": ("llvm-cov", ("--version",), "pacman -S llvm"),
    "llvm-profdata": ("llvm-profdata", ("--version",), "pacman -S llvm"),
    "llvm-objdump": ("llvm-objdump", ("--version",), "pacman -S llvm"),
    "llvm-readelf": ("llvm-readelf", ("--version",), "pacman -S llvm"),
    "llvm-nm": ("llvm-nm", ("--version",), "pacman -S llvm"),
    "bloaty": ("bloaty", ("--version",), "pacman -S bloaty"),
    "abidiff": ("abidiff", ("--version",), "pacman -S libabigail"),
}

# Hard requirements per command; everything else a command uses is optional and reported as unavailable.
REQUIRED: dict[str, list[str]] = {
    "check": ["cmake", "ninja", "clang-format"],
    "analyze": ["cmake", "ninja", "clang++", "clang-tidy", "cppcheck"],
    "sanitize": ["cmake", "ninja", "clang++"],
    "memcheck": ["cmake", "ninja", "valgrind"],
    "coverage": ["cmake", "ninja", "clang++", "llvm-cov", "llvm-profdata"],
    "profile": ["cmake", "ninja", "perf"],
    "callgrind": ["cmake", "ninja", "valgrind"],
    "memory-profile": ["cmake", "ninja"],
    "build-profile": ["cmake", "ninja", "clang++"],
    "compare-codegen": ["cmake", "ninja", "git", "llvm-objdump", "llvm-readelf", "llvm-nm"],
    "bench": ["cmake", "ninja"],
}

# Optional extras that deepen a command when present.
OPTIONAL: dict[str, list[str]] = {
    "analyze": ["scan-build", "include-what-you-use", "iwyu_tool.py", "codeql"],
    "memory-profile": ["heaptrack", "heaptrack_print"],
    "build-profile": ["ClangBuildAnalyzer"],
    "compare-codegen": ["bloaty", "abidiff"],
}


def probe(name: str) -> dict:
    executable, args, hint = TOOLS[name]
    path = tool_path(executable)
    return {"tool": name, "found": path is not None, "path": path,
            "version": tool_version(executable, args) if path else None, "install": hint}


def system_checks() -> list[dict]:
    checks = []
    paranoid = Path("/proc/sys/kernel/perf_event_paranoid")
    if paranoid.exists():
        level = int(paranoid.read_text().strip())
        note = ("ok: user-space profiling and hardware counters for own processes" if level <= 2
                else "perf_event_paranoid > 2 blocks unprivileged perf")
        if level == 2:
            note = "level 2: user-space sampling only; kernel samples need `sysctl kernel.perf_event_paranoid=1`"
        checks.append({"check": "perf_event_paranoid", "value": level, "note": note})
    import subprocess
    pkg = subprocess.run(["pkg-config", "--modversion", "hyprland"], capture_output=True, text=True, check=False)
    checks.append({"check": "hyprland-dev-headers", "value": pkg.stdout.strip() or None,
                   "note": "pkg-config hyprland (needed to configure the project at all)",
                   "found": pkg.returncode == 0})
    checks.append({"check": "cpus", "value": os.cpu_count(), "note": ""})
    return checks
