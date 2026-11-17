#!/usr/bin/env python3
"""Embed GLSL files into a header for the compositor plugin.

A line `#include "name.glsl"` is replaced by that file from the same directory.
The default shader list and namespace target the bubble material.
"""

import argparse
import re
from pathlib import Path


SHADERS = {
    "VERTEX": "membrane.vert",
    "FRAGMENT": "membrane.frag",
    "CURL_VERTEX": "curl.vert",
    "CURL_FRAGMENT": "curl.frag",
    "PARTICLE_VERTEX": "particle.vert",
    "PARTICLE_FRAGMENT": "particle.frag",
    "FILM_VERTEX": "film.vert",
    "FILM_FRAGMENT": "film.frag",
    "MICROBUBBLE_VERTEX": "microbubble.vert",
    "MICROBUBBLE_FRAGMENT": "microbubble.frag",
}

INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"\s*$')


def expand(directory: Path, filename: str) -> str:
    lines = []
    for line in (directory / filename).read_text().splitlines(keepends=True):
        match = INCLUDE.match(line)
        if match:
            included = (directory / match.group(1)).read_text()
            if any(INCLUDE.match(l) for l in included.splitlines()):
                raise ValueError(f"{match.group(1)}: nested includes are not supported")
            lines.append(included if included.endswith("\n") else included + "\n")
        else:
            lines.append(line)
    return "".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--namespace", default="bubble_shader")
    parser.add_argument("--shader", action="append", metavar="NAME=FILE")
    args = parser.parse_args()
    shaders = SHADERS
    if args.shader:
        shaders = {}
        for entry in args.shader:
            name, separator, filename = entry.partition("=")
            if not separator or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) or not filename:
                parser.error("--shader requires NAME=FILE with a C++ identifier")
            shaders[name] = filename
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args.namespace):
        parser.error("--namespace requires a C++ identifier")

    parts = ["#pragma once\n", f"namespace {args.namespace} {{\n"]
    for name, filename in shaders.items():
        source = expand(args.source_dir, filename)
        if ')SHADER"' in source:
            raise ValueError(f"{filename} contains the raw string terminator")
        parts.append(f'inline constexpr auto {name} = ::std::to_array(R"SHADER({source})SHADER");\n')
    parts.append(f"}} // namespace {args.namespace}\n")

    output = "".join(parts)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != output:
        args.output.write_text(output)


if __name__ == "__main__":
    main()
