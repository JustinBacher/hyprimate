#!/usr/bin/env python3
"""Check plugin imports against the host and their shared-library dependencies.

Only use this on trusted binaries: ldd may execute code from its input on some
systems. The plugin itself is never loaded by this script.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys


class CheckError(Exception):
    pass


def run_tool(command):
    env = os.environ.copy()
    env["LC_ALL"] = "C"
    try:
        result = subprocess.run(command, capture_output=True, text=True, env=env)
    except OSError as error:
        raise CheckError(f"cannot run {command[0]}: {error}") from error
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic"
        raise CheckError(f"{' '.join(map(str, command))} failed: {detail}")
    return result.stdout


def symbols(nm, binary, selection):
    output = run_tool([nm, "-D", selection, "--format=posix", str(binary)])
    result = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 2 or len(fields[1]) != 1:
            raise CheckError(f"unexpected nm output for {binary}: {line}")
        result.append((fields[0], fields[1]))
    return result


def dependencies(binary):
    output = run_tool(["ldd", str(binary)])
    paths = set()
    for line in output.splitlines():
        line = line.strip()
        if not line:
            continue
        if " => " in line:
            name, target = line.split(" => ", 1)
            if target == "not found":
                raise CheckError(f"missing dependency {name} required by {binary}")
        else:
            target = line
            if target.startswith("linux-vdso.so") or target.startswith("linux-gate.so"):
                continue
        path, separator, _ = target.rpartition(" (")
        if not separator or not path.startswith("/"):
            raise CheckError(f"unexpected ldd output for {binary}: {line}")
        paths.add(Path(path))
    return paths


def provided_names(name):
    if "@@" in name:
        base, version = name.split("@@", 1)
        return (base, f"{base}@{version}")
    return (name,)


def check(plugin, host, nm):
    imports = {name for name, kind in symbols(nm, plugin, "--undefined-only")
               if kind not in ("w", "v")}
    providers = {host} | dependencies(host) | dependencies(plugin)
    exports = set()
    for provider in sorted(providers):
        for name, _ in symbols(nm, provider, "--defined-only"):
            exports.update(provided_names(name))
    return sorted(imports - exports)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plugin", type=Path, help="plugin shared object")
    parser.add_argument("host", type=Path, help="host executable")
    parser.add_argument("--nm", default="nm", help="nm executable (default: nm)")
    args = parser.parse_args()
    try:
        missing = check(args.plugin.resolve(), args.host.resolve(), args.nm)
    except CheckError as error:
        print(f"plugin symbol check failed: {error}", file=sys.stderr)
        return 1
    if missing:
        print("plugin symbol check failed: unresolved strong imports:\n  "
              + "\n  ".join(missing), file=sys.stderr)
        return 1
    print(f"plugin symbol check passed: {args.plugin}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
