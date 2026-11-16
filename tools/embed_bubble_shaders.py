#!/usr/bin/env python3
"""Embed the bubble GLSL files into a header for the compositor plugin."""

import argparse
from pathlib import Path


SHADERS = {
    "VERTEX": "membrane.vert",
    "FRAGMENT": "membrane.frag",
    "CURL_VERTEX": "curl.vert",
    "CURL_FRAGMENT": "curl.frag",
    "PARTICLE_VERTEX": "particle.vert",
    "PARTICLE_FRAGMENT": "particle.frag",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    parts = ["#pragma once\n", "namespace bubble_shader {\n"]
    for name, filename in SHADERS.items():
        source = (args.source_dir / filename).read_text()
        if ')SHADER"' in source:
            raise ValueError(f"{filename} contains the raw string terminator")
        parts.append(f'inline constexpr char {name}[] = R"SHADER({source})SHADER";\n')
    parts.append("} // namespace bubble_shader\n")

    output = "".join(parts)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text() != output:
        args.output.write_text(output)


if __name__ == "__main__":
    main()
