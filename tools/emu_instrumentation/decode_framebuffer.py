"""Decode the N64 framebuffer from an ares RDRAM dump and produce a gold-mask oracle.

Phase 1 of the dump-based intro sprite validation plan (2026-04-30).

Usage:
    python tools/emu_instrumentation/decode_framebuffer.py [dump_path]

If no dump_path is given, uses: Automobili Lamborghini (USA)-rdram-copyright.bin
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DUMP = REPO_ROOT / "Automobili Lamborghini (USA)-rdram-copyright.bin"

# N64 RDRAM framebuffer addresses (confirmed per session 2026-04-27)
FB_BASE = 0x8036C000  # front buffer (visible)
FB_W, FB_H = 320, 240


def rgba5551_to_rgba8888(word: int) -> tuple[int, int, int, int]:
    """Decode N64 RGBA5551 big-endian to RGBA8888."""
    r5 = (word >> 11) & 0x1F
    g5 = (word >> 6) & 0x1F
    b5 = (word >> 1) & 0x1F
    a1 = word & 1
    r = (r5 << 3) | (r5 >> 2)
    g = (g5 << 3) | (g5 >> 2)
    b = (b5 << 3) | (b5 >> 2)
    a = 255 if a1 else 0
    return r, g, b, a


def decode_fb(data: bytes) -> Image.Image:
    """Decode RGBA5551 big-endian framebuffer to RGBA8888 PIL Image."""
    img = Image.new("RGBA", (FB_W, FB_H))
    for y in range(FB_H):
        for x in range(FB_W):
            offset = (y * FB_W + x) * 2
            word = struct.unpack_from(">H", data, offset)[0]
            r, g, b, a = rgba5551_to_rgba8888(word)
            img.putpixel((x, y), (r, g, b, a))
    return img


def gold_mask(img: Image.Image) -> tuple[Image.Image, int, tuple, list, list]:
    """Return a binary mask of gold-ish pixels and metadata.

    Returns (mask, gold_count, bbox, col_hist, row_hist) where:
      - gold_count  : number of pixels passing the gold threshold
      - bbox        : (x_min, y_min, x_max, y_max) of all gold pixels
      - col_hist    : list of gold pixel counts per column (320 entries)
      - row_hist    : list of gold pixel counts per row (240 entries)
    """
    mask = Image.new("L", (FB_W, FB_H), 0)
    col_hist = [0] * FB_W
    row_hist = [0] * FB_H
    gold_count = 0
    x_min = FB_W
    y_min = FB_H
    x_max = -1
    y_max = -1

    for y in range(FB_H):
        for x in range(FB_W):
            r, g, b, _ = img.getpixel((x, y))
            if r > 180 and g > 140 and b < 60:
                mask.putpixel((x, y), 255)
                col_hist[x] += 1
                row_hist[y] += 1
                gold_count += 1
                if x < x_min: x_min = x
                if y < y_min: y_min = y
                if x > x_max: x_max = x
                if y > y_max: y_max = y

    if gold_count == 0:
        return mask, 0, (-1, -1, -1, -1), col_hist, row_hist

    return mask, gold_count, (x_min, y_min, x_max, y_max), col_hist, row_hist


def main() -> None:
    dump_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_DUMP
    if not dump_path.exists():
        print(f"ERROR: dump not found at {dump_path}")
        sys.exit(1)

    data = dump_path.read_bytes()
    if len(data) < 8 * 1024 * 1024:
        print(f"ERROR: dump too small ({len(data)} bytes)")
        sys.exit(1)

    print(f"=== Framebuffer Oracle ===")
    print(f"  Dump: {dump_path.name} ({len(data):,} bytes)")

    # Decode FB1 (front buffer)
    fb_offset = FB_BASE - 0x80000000  # convert RDRAM address to dump offset
    fb_data = data[fb_offset : fb_offset + FB_W * FB_H * 2]
    img = decode_fb(fb_data)

    out_png = REPO_ROOT / "screenshots" / "decode" / "fb_copyright_real.png"
    out_png.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_png)
    print(f"  FB1 @ 0x{FB_BASE:X}: saved to {out_png}")

    # Gold mask
    mask, gold_count, bbox, col_hist, row_hist = gold_mask(img)

    out_mask = REPO_ROOT / "screenshots" / "decode" / "fb_copyright_gold_mask.png"
    mask.save(out_mask)
    print(f"  Gold mask @ {out_mask}: {gold_count} pixels")

    print(f"\n  === Gold Pixel Analysis ===")
    print(f"  gold_pixel_count : {gold_count}")
    x_min, y_min, x_max, y_max = bbox
    print(f"  bounding_box     : ({x_min}, {y_min}, {x_max}, {y_max})")
    print(f"  width            : {x_max - x_min + 1}")
    print(f"  height           : {y_max - y_min + 1}")

    # Column histogram peaks (top 5)
    col_peaks = sorted(enumerate(col_hist), key=lambda x: -x[1])[:5]
    print(f"  col_hist peaks   : {[(x, c) for x, c in col_peaks]}")

    # Row histogram peaks (top 5)
    row_peaks = sorted(enumerate(row_hist), key=lambda x: -x[1])[:5]
    print(f"  row_hist peaks   : {[(y, c) for y, c in row_peaks]}")

    # Also try back buffer FB2 at 0x803BC000
    fb2_offset = 0x803BC000 - 0x80000000
    if fb2_offset + FB_W * FB_H * 2 <= len(data):
        fb2_data = data[fb2_offset : fb2_offset + FB_W * FB_H * 2]
        img2 = decode_fb(fb2_data)
        _, gc2, _, _, _ = gold_mask(img2)
        print(f"\n  FB2 @ 0x803BC000 gold_count={gc2} (for comparison)")

    print(f"\n  NOTE: Validation gate — PNG must resemble lamborghini_03.png")
    print(f"        If blank, the game may have advanced past copyright state.")
    print(f"        The 'rumblepakdetected.bin' or 'pressstart.bin' dumps may match.")


if __name__ == "__main__":
    main()
