#!/usr/bin/env python3
"""Run the configured clang-tidy policy on project translation units in a build."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from pathlib import Path
import shutil
import subprocess
import sys


def jobs_count(value):
    try:
        jobs = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("jobs must be an integer between 1 and 32")
    if not 1 <= jobs <= 32:
        raise argparse.ArgumentTypeError("jobs must be an integer between 1 and 32")
    return jobs


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog="Use a Clang analysis build configured with "
               "-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON; "
               "GCC-only flags in other compilation databases may be rejected by clang-tidy.",
    )
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="build directory containing compile_commands.json")
    parser.add_argument("--jobs", type=jobs_count, default=3,
                        help="parallel clang-tidy processes (1-32, default: 3)")
    parser.add_argument("--clang-tidy", default="clang-tidy",
                        help="clang-tidy executable (default: clang-tidy)")
    args = parser.parse_args()

    executable = shutil.which(args.clang_tidy)
    if executable is None:
        print(f"clang-tidy executable not found or not executable: {args.clang_tidy}", file=sys.stderr)
        return 1

    build_dir = args.build_dir.resolve()
    database = build_dir / "compile_commands.json"
    try:
        with database.open(encoding="utf-8") as stream:
            commands = json.load(stream)
        if not isinstance(commands, list):
            raise ValueError("expected an array of compilation commands")
        project = Path(__file__).resolve().parent.parent
        roots = (project / "src", project / "tests")
        files = set()
        for command in commands:
            directory = Path(command["directory"])
            source = (directory / command["file"]).resolve()
            if any(source.is_relative_to(root) for root in roots):
                files.add(source)
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"Cannot load compilation database {database}: {error}", file=sys.stderr)
        return 1

    if not files:
        print(f"No project translation units found in {database}", file=sys.stderr)
        return 1

    def check(source):
        try:
            return subprocess.run(
                [executable, "-p", str(build_dir), str(source)],
                cwd=project, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, errors="replace", check=False,
            )
        except OSError as error:
            return error

    failed = False
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(check, source): source for source in sorted(files)}
        for future in as_completed(futures):
            source = futures[future]
            result = future.result()
            print(f"\n=== {source.relative_to(project)} ===", flush=True)
            if isinstance(result, OSError):
                print(f"Failed to run clang-tidy: {result}", file=sys.stderr, flush=True)
                failed = True
            else:
                print(result.stdout, end="" if result.stdout.endswith("\n") else "\n", flush=True)
                if result.returncode:
                    print(f"clang-tidy exited with status {result.returncode}", file=sys.stderr, flush=True)
                    failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
