#!/usr/bin/env python3
"""Embed the slice's 512 × 256 PNG atlas as a private C++ byte array."""

import argparse
import struct
from pathlib import Path


ATLAS_SIZE = (512, 256)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    data = args.input.read_bytes()
    if len(data) < 33 or data[:8] != PNG_SIGNATURE or data[8:16] != b"\x00\x00\x00\rIHDR":
        parser.error(f"{args.input}: expected a PNG signature followed by an IHDR chunk")
    width, height = struct.unpack_from(">II", data, 16)
    if (width, height) != ATLAS_SIZE:
        parser.error(f"{args.input}: expected {ATLAS_SIZE[0]} × {ATLAS_SIZE[1]}, got {width} × {height}")

    rows = (
        "    " + ", ".join(f"0x{byte:02x}" for byte in data[i : i + 16]) + ",\n"
        for i in range(0, len(data), 16)
    )
    header = (
        "#pragma once\n\n#include <array>\n\nnamespace hyprimate::construction_atlas {\n"
        f"inline constexpr int WIDTH = {width};\n"
        f"inline constexpr int HEIGHT = {height};\n"
        f"inline constexpr std::array<unsigned char, {len(data)}> PNG{{\n"
        + "".join(rows)
        + "};\n}  // namespace hyprimate::construction_atlas\n"
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != header:
        args.output.write_text(header, encoding="utf-8")


if __name__ == "__main__":
    main()
