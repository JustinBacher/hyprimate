#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"

usage() {
    cat <<'EOF'
Usage: tools/check_unused.sh [BUILD_DIR]
       tools/check_unused.sh --help

Scan project translation units from CMake's compilation database with Cppcheck.
BUILD_DIR defaults to the repository's build directory; a relative path is
resolved from the current directory. CMake, Cppcheck, and the project's normal
configure dependencies must be installed. If the compilation database is
missing, CMake configures the build with compilation database export enabled.
Only generated-header targets are built; the plugin and tests are not built or
run. The Cppcheck cache is stored in BUILD_DIR/cppcheck-unused.

Findings are advisory: functions called externally by the host, including plugin
entrypoints, can be reported as unused. The exit status is Cppcheck's status;
reported findings alone do not make the scan fail.
EOF
}

if [[ $# -eq 1 && $1 == --help ]]; then
    usage
    exit 0
fi
if [[ $# -gt 1 || ( $# -eq 1 && $1 == -* ) ]]; then
    usage >&2
    exit 2
fi

build_dir="${1:-$repo_root/build}"
mkdir -p -- "$build_dir"
build_dir="$(cd -- "$build_dir" && pwd -P)"

if ! command -v cppcheck >/dev/null 2>&1; then
    echo 'error: cppcheck is required; install it and retry' >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo 'error: cmake is required to configure and generate headers' >&2
    exit 1
fi

if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    cmake -S "$repo_root" -B "$build_dir" -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi
if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    echo "error: no compile_commands.json in $build_dir; use a CMake Makefiles or Ninja build with CMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
    exit 1
fi
if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    echo "error: $build_dir is not a configured CMake build; configure it for this repository first" >&2
    exit 1
fi

cmake --build "$build_dir" --target generate-config generate-bubble-shaders generate-swarm-shaders
mkdir -p -- "$build_dir/cppcheck-unused"

# Do not override the C++ standard recorded in the compilation database.
# Externally called plugin entrypoints may be reported as unused candidates.
exec cppcheck --project="$build_dir/compile_commands.json" \
    --cppcheck-build-dir="$build_dir/cppcheck-unused" \
    --enable=unusedFunction,missingInclude,information \
    --file-filter="$repo_root/src/*" \
    --file-filter="$repo_root/tests/*"
