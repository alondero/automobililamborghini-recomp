#!/usr/bin/env python3
"""Upscale a decoded font/HUD atlas into a higher-resolution replacement (issue #52).

The source glyph atlases are tiny CI4/CI8 strips (e.g. the HUD font is 512x8). Blown
up at native output resolution by nearest-neighbour they read as blocky and hard to
read -- the whole point of #52 is to make them legible. This tool takes a decoded
<hash>.png (from tools/decode_dump.py) and produces an N-times-larger PNG whose
letterforms are preserved but whose staircase edges are smoothed, i.e. an
"algorithmic upscale" that keeps the original 1997 shapes rather than re-drawing
them from a modern font.

Why not just Image.resize(LANCZOS) on the RGBA? A palettized font decodes to RGBA
where every *transparent* texel has an undefined RGB value. Resampling the whole
image pulls that junk RGB into the glyph edges and fringes them. So we:

  1. Split colour from the coverage (alpha) channel.
  2. Bleed the nearest opaque colour outward into the transparent region, so the
     colour plane has no hard seam for the resampler to smear.
  3. Upscale the bled colour plane and the alpha plane independently (high-quality
     Lanczos), then recombine. The smooth alpha ramp *is* the anti-aliased edge.

For a 1-bit-alpha font (CI4 with an RGBA16 TLUT, the common HUD case) this yields
clean, readable, letterform-preserving glyphs with no colour fringe.

    python tools/upscale_font.py <in.png> <out.png> [--scale N] [--edge soft|crisp]
                                                     [--sharpen]

  --scale N     integer upscale factor (default 8).
  --edge soft   plain Lanczos alpha ramp (default; softest, most faithful).
  --edge crisp  supersample + reharden the alpha so edges stay tight at high N
                (better for very low glyphs where soft looks mushy).
  --sharpen     apply a mild unsharp mask to the colour plane afterwards.

The output is an integer-scaled copy of the N64 texel grid (no sub-texel origin
offset), so pack it with `make_pack.py --shift none`, NOT the default `half`. A
half-texel shift over-shifts a grid-aligned upscale by half a source texel, which
in-game reads as each glyph doubling into its neighbour's cell (verified on the
512x8 HUD font: `shift: half` -> "ONE PLAYER" renders as "ONE PLAYER:").

Requires Pillow (already used by tools/drive_input.py). No numpy.
"""

import argparse
from pathlib import Path

from PIL import Image, ImageFilter


def bleed_colour(rgba, iterations):
    """Push the nearest opaque RGB outward into transparent texels so the colour
    plane has no hard transparent/opaque seam for the resampler to smear. Alpha is
    left untouched; only RGB of transparent pixels is filled."""
    px = rgba.load()
    w, h = rgba.size
    for _ in range(iterations):
        changed = False
        # snapshot alpha so a pixel filled this pass isn't treated as a source yet
        opaque = [[px[x, y][3] > 0 for y in range(h)] for x in range(w)]
        for y in range(h):
            for x in range(w):
                if opaque[x][y]:
                    continue
                acc = [0, 0, 0]
                n = 0
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1),
                               (-1, -1), (1, -1), (-1, 1), (1, 1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and opaque[nx][ny]:
                        r, g, b, _ = px[nx, ny]
                        acc[0] += r
                        acc[1] += g
                        acc[2] += b
                        n += 1
                if n:
                    r, g, b, a = px[x, y]
                    px[x, y] = (acc[0] // n, acc[1] // n, acc[2] // n, a)
                    changed = True
        if not changed:
            break
    return rgba


def upscale(inp, scale, edge, sharpen):
    src = Image.open(inp).convert("RGBA")
    w, h = src.size
    tw, th = w * scale, h * scale

    # 1. colour plane: bleed then upscale (no alpha, so no transparent smear)
    bled = bleed_colour(src.copy(), iterations=max(w, h))
    rgb = bled.convert("RGB").resize((tw, th), Image.LANCZOS)
    if sharpen:
        rgb = rgb.filter(ImageFilter.UnsharpMask(radius=scale / 2, percent=80, threshold=0))

    # 2. alpha (coverage) plane
    alpha = src.getchannel("A")
    if edge == "crisp":
        # supersample high, blur lightly, then reharden the ramp so edges stay
        # tight instead of dissolving -- better for very short (<=8px) glyphs.
        big = alpha.resize((tw * 2, th * 2), Image.LANCZOS)
        big = big.filter(ImageFilter.GaussianBlur(radius=scale / 4))
        big = big.point(lambda v: 0 if v < 96 else (255 if v > 160 else int((v - 96) * 255 / 64)))
        up_a = big.resize((tw, th), Image.LANCZOS)
    else:  # soft
        up_a = alpha.resize((tw, th), Image.LANCZOS)

    out = rgb.convert("RGBA")
    out.putalpha(up_a)
    return out


def main():
    ap = argparse.ArgumentParser(description="Upscale a decoded font atlas (issue #52).")
    ap.add_argument("inp", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--scale", type=int, default=8)
    ap.add_argument("--edge", choices=["soft", "crisp"], default="soft")
    ap.add_argument("--sharpen", action="store_true")
    args = ap.parse_args()

    img = upscale(args.inp, args.scale, args.edge, args.sharpen)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    img.save(args.out)
    print(f"upscaled {args.inp.name} {img.width // args.scale}x{img.height // args.scale}"
          f" -> {img.width}x{img.height} ({args.edge}) -> {args.out}")
    print("  pack with: make_pack.py <dir> --shift none  (grid-aligned upscale)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
