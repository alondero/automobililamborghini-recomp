#!/usr/bin/env python3
"""Batch-upscale a decoded RT64 texture dump with xBRZ (issue #52 follow-up).

`upscale_font.py` smooths glyph atlases with a Lanczos resample -- right for text,
too soft for everything else. This tool runs the real xBRZ pixel-art scaler over
decoded dump PNGs to produce an "HD" replacement pack whose HUD elements, wordmarks,
trackside signs and world art keep their hard 1997 edges but lose the staircase
blockiness against the natively-rendered polygons.

    python tools/upscale_texture.py <dump_dir> <pack_dir> [--scale N] [--only PREFIX ...]
                                                          [--exclude PREFIX ...]

  <dump_dir>   RT64 dump directory (the one holding <hash>.v5.tile.json), with the
               decoded PNGs in <dump_dir>/png (run tools/decode_dump.py first).
  <pack_dir>   output directory; written as <hash>.png ready for make_pack.py.
  --scale N    xBRZ factor 2-6 (default 6 -- HUD art is tiny, take all it gives).
  --only       process only hashes starting with one of these prefixes.
  --exclude    skip hashes starting with one of these prefixes (e.g. the three font
               atlases when you pair this pack with render_font.py output).

Correctness details, each learned from how RT64/the N64 actually sample:

- **Edge padding follows the tile's clamp bits.** A wrapped (tiling) texture scaled
  in isolation gets seams at every tile border because xBRZ treats the image edge as
  a hard boundary. Each dump has cms/cmt in its tile.json (0=wrap 1=mirror 2/3=clamp);
  we pad 3 texels per side in that mode, scale, then crop the padding away.
- **Transparent texels get their RGB bled from opaque neighbours first** (same trick
  as upscale_font.py): a palettized decode leaves junk RGB under alpha=0, and xBRZ
  would smear that junk into the edge gradients.
- **CI decodes with palette-fallback pixels are skipped, loudly.** decode_dump.py
  paints out-of-range palette indices magenta; replacing a real texture with that
  would ship the corruption. Nothing is dropped silently.

The output is grid-aligned with the N64 texel grid, so pack with
`make_pack.py <pack_dir> --shift none` (NOT the default `half`).

Needs the xBRZ CLI from tools/xbrz/ (auto-built with g++ if missing; see
tools/xbrz/README.md) and Pillow.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

from upscale_font import bleed_colour

XBRZ_DIR = Path(__file__).resolve().parent / "xbrz"
PAD = 3  # xBRZ analyses 2 texels past the edge; 3 keeps the cropped result pristine

MAGENTA = (255, 0, 255, 255)  # decode_dump.py's palette-fallback colour


def find_or_build_cli():
    env = os.environ.get("XBRZ_CLI")
    if env:
        return Path(env)
    for name in ("xbrz_cli.exe", "xbrz_cli"):
        p = XBRZ_DIR / name
        if p.exists():
            return p
    gxx = shutil.which("g++") or shutil.which("c++")
    if not gxx:
        sys.exit("xbrz_cli not built and no g++ on PATH -- see tools/xbrz/README.md")
    out = XBRZ_DIR / ("xbrz_cli.exe" if os.name == "nt" else "xbrz_cli")
    print(f"building {out.name} with {gxx} ...")
    subprocess.run([gxx, "-O3", "-std=gnu++17", "-static",
                    str(XBRZ_DIR / "xbrz.cpp"), str(XBRZ_DIR / "xbrz_cli.cpp"),
                    "-o", str(out)], check=True)
    return out


def pad_edges(img, cms, cmt):
    """Surround the image with PAD texels continuing it the way the RDP samples past
    the edge (wrap / mirror / clamp per axis), so xBRZ sees the true neighbourhood."""
    w, h = img.size

    def strip(source, box, flip=None, stretch=None):
        s = source.crop(box)
        if flip:
            s = s.transpose(flip)
        if stretch:
            s = s.resize(stretch, Image.NEAREST)
        return s

    mid = Image.new("RGBA", (w + 2 * PAD, h), (0, 0, 0, 0))
    mid.paste(img, (PAD, 0))
    if cms == 0:  # wrap
        mid.paste(strip(img, (w - PAD, 0, w, h)), (0, 0))
        mid.paste(strip(img, (0, 0, PAD, h)), (w + PAD, 0))
    elif cms == 1:  # mirror
        mid.paste(strip(img, (0, 0, PAD, h), flip=Image.FLIP_LEFT_RIGHT), (0, 0))
        mid.paste(strip(img, (w - PAD, 0, w, h), flip=Image.FLIP_LEFT_RIGHT), (w + PAD, 0))
    else:  # clamp: replicate the edge column
        mid.paste(strip(img, (0, 0, 1, h), stretch=(PAD, h)), (0, 0))
        mid.paste(strip(img, (w - 1, 0, w, h), stretch=(PAD, h)), (w + PAD, 0))

    pw = mid.width
    out = Image.new("RGBA", (pw, h + 2 * PAD), (0, 0, 0, 0))
    out.paste(mid, (0, PAD))
    if cmt == 0:
        out.paste(strip(mid, (0, h - PAD, pw, h)), (0, 0))
        out.paste(strip(mid, (0, 0, pw, PAD)), (0, h + PAD))
    elif cmt == 1:
        out.paste(strip(mid, (0, 0, pw, PAD), flip=Image.FLIP_TOP_BOTTOM), (0, 0))
        out.paste(strip(mid, (0, h - PAD, pw, h), flip=Image.FLIP_TOP_BOTTOM), (0, h + PAD))
    else:
        out.paste(strip(mid, (0, 0, pw, 1), stretch=(pw, PAD)), (0, 0))
        out.paste(strip(mid, (0, h - 1, pw, h), stretch=(pw, PAD)), (0, h + PAD))
    return out


def xbrz_scale(cli, img, factor):
    r = subprocess.run([str(cli), str(factor), str(img.width), str(img.height)],
                       input=img.tobytes(), stdout=subprocess.PIPE, check=True)
    return Image.frombytes("RGBA", (img.width * factor, img.height * factor), r.stdout)


def process(cli, png, meta, factor):
    img = Image.open(png).convert("RGBA")
    alphas = img.getchannel("A").getextrema()
    if alphas[0] == 0 and alphas[1] > 0:
        # xBRZ only analyses ~2 texels around a pixel, so a shallow bleed suffices
        # (unlike upscale_font's Lanczos, whose kernel reaches with the scale factor)
        img = bleed_colour(img, iterations=PAD + 3)
    tile = meta.get("tile", {})
    padded = pad_edges(img, tile.get("cms", 2), tile.get("cmt", 2))
    up = xbrz_scale(cli, padded, factor)
    return up.crop((PAD * factor, PAD * factor,
                    up.width - PAD * factor, up.height - PAD * factor))


def main():
    ap = argparse.ArgumentParser(description="xBRZ-upscale a decoded RT64 texture dump.")
    ap.add_argument("dump_dir", type=Path)
    ap.add_argument("pack_dir", type=Path)
    ap.add_argument("--scale", type=int, default=6, choices=range(2, 7))
    ap.add_argument("--only", nargs="*", default=[])
    ap.add_argument("--exclude", nargs="*", default=[])
    args = ap.parse_args()

    cli = find_or_build_cli()
    pngs = sorted((args.dump_dir / "png").glob("*.png"))
    pngs = [p for p in pngs if len(p.stem) == 16]  # hash-named only, not contact sheets
    if args.only:
        pngs = [p for p in pngs if any(p.stem.startswith(o) for o in args.only)]
    args.pack_dir.mkdir(parents=True, exist_ok=True)

    done = skipped_fallback = skipped_excluded = 0
    for png in pngs:
        if any(png.stem.startswith(e) for e in args.exclude):
            skipped_excluded += 1
            continue
        tj = args.dump_dir / f"{png.stem}.v5.tile.json"
        meta = json.loads(tj.read_text()) if tj.exists() else {}
        img = Image.open(png).convert("RGBA")
        if meta.get("tile", {}).get("fmt") == 2 and MAGENTA in img.getdata():
            print(f"SKIP {png.stem}: CI decode has palette-fallback (magenta) pixels")
            skipped_fallback += 1
            continue
        process(cli, png, meta, args.scale).save(args.pack_dir / png.name)
        done += 1

    print(f"upscaled {done} texture(s) x{args.scale} -> {args.pack_dir}"
          f" ({skipped_fallback} skipped on palette fallback,"
          f" {skipped_excluded} excluded)")
    print(f"  next: python tools/make_pack.py {args.pack_dir} --shift none")
    return 0 if done else 1


if __name__ == "__main__":
    raise SystemExit(main())
