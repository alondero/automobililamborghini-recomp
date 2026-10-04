#!/usr/bin/env python3
"""Convert captured RT64 swapchain images (task-N-swap.bgra) to PNG.

Captures stay under ignored artifacts/; this writes PNGs beside them, or a
crop/zoom for inspection. Uses only the standard library.
"""
from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path


def read_swap(render_dir: Path, sequence: int) -> tuple[int, int, bytes]:
    present = json.loads((render_dir / f"task-{sequence}-present.json").read_text(encoding="utf-8"))
    width, height, row_bytes = present["width"], present["height"], present["row_bytes"]
    raw = (render_dir / f"task-{sequence}-swap.bgra").read_bytes()
    if present.get("format") != "BGRA8" or len(raw) < row_bytes * height or row_bytes < width * 4:
        raise ValueError(f"task {sequence}: unsupported or short swap capture")
    rows = bytearray()
    for y in range(height):
        row = raw[y * row_bytes:y * row_bytes + width * 4]
        for x in range(width):
            b, g, r = row[x * 4:x * 4 + 3]
            rows += bytes((r, g, b))
    return width, height, bytes(rows)


def crop(width: int, rgb: bytes, box: tuple[int, int, int, int], zoom: int) -> tuple[int, int, bytes]:
    left, top, right, bottom = box
    out = bytearray()
    for y in range(top, bottom):
        line = bytearray()
        for x in range(left, right):
            line += rgb[(y * width + x) * 3:(y * width + x) * 3 + 3] * zoom
        out += bytes(line) * zoom
    return (right - left) * zoom, (bottom - top) * zoom, bytes(out)


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    scanlines = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(scanlines, 6)) + chunk(b"IEND", b""))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("render_dir", type=Path)
    parser.add_argument("sequence", type=int)
    parser.add_argument("output", type=Path)
    parser.add_argument("--crop", type=int, nargs=4, metavar=("LEFT", "TOP", "RIGHT", "BOTTOM"))
    parser.add_argument("--zoom", type=int, default=1)
    args = parser.parse_args()
    width, height, rgb = read_swap(args.render_dir, args.sequence)
    if args.crop:
        width, height, rgb = crop(width, rgb, tuple(args.crop), max(1, args.zoom))
    write_png(args.output, width, height, rgb)


if __name__ == "__main__":
    main()
