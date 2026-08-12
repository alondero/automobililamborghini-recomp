#!/usr/bin/env python3
"""Re-render the game's small font atlases from a vector (TTF) font (issue #52).

`upscale_font.py` only smooths the original 8px glyphs -- it cannot add detail an 8px
source never had, so the text stays soft. HD texture packs instead replace the atlas at
HIGHER RESOLUTION (RT64 remaps the game's UVs onto any-size replacement, see
`lib/rt64/src/render/rt64_texture_cache.cpp`), so the real readability win is to *re-draw*
each glyph from a real outline font at high resolution. This produces the crisp, legible
text the issue asks for.

## Why this is per-atlas (the hard part)

Each atlas hash is a distinct TMEM texture and they are packed DIFFERENTLY:

- The **white** HUD/message font (`aec01187`, `2cc2b764`) is PROPORTIONALLY packed -- glyphs
  sit at irregular atlas U-positions and some touch -- and it is **italic** in the original.
- The **gold** menu font (`7c1ef5cc`) is cleanly separated on a regular pitch and **upright**.

So there is no single position table and no single style: white needs a proportional table +
an italic slant, gold needs an index-derived table + upright. Naively mapping glyphs to a
fixed 8px grid, or rendering upright into the italic atlas, JUMBLES the text (verified: the
copyright line came out as garbage). The per-atlas positions below were read off each decoded
atlas against an x-ruler and validated in-game.

## Rendering rules that matter

- **Uniform cap-height + uniform stroke, then POSITION (do not stretch to fill the box).**
  Stretching each glyph to its atlas box distorts stroke weight -- a narrow box fattens 'I',
  a wide box thins 'M'. Render every glyph at one size and just place it.
- Caps/digits are vertical-centred; punctuation ('.') is bottom-aligned to the baseline.
- **Transparent base** -- paint ONLY the mapped glyphs. Any upscaled base (nearest or lanczos)
  leaves original-glyph slivers/ghosts between the crisp glyphs.
- **Italic atlases** (the white font): pass a real italic/oblique face via `--ttf-italic` for
  the cleanest result. Without one, the upright `--ttf` is sheared as a fallback (portable, but
  the shear leaves faint edge ticks). `--shear 0` disables the fallback.

Pack the result with `make_pack.py <dir> --shift none` (the output is grid-aligned; `half`
over-shifts it -- see docs/TEXTURES.md). Ship with an OPEN font (e.g. Liberation/DejaVu Bold)
to avoid embedding a proprietary face.

    python tools/render_font.py <decoded_atlas.png> <out.png> --ttf <bold.ttf> \
        [--ttf-italic <bold-oblique.ttf>] [--shear 0.22]

The atlas profile (positions + italic flag) is auto-selected from the hash in the filename.

Requires Pillow. No numpy.
"""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SCALE = 8        # 512x8 -> 4096x64
OUTLINE = 4      # dark outline width in output px (contrast on bright HUD backgrounds)


def ink_runs(img):
    """Contiguous columns that contain any opaque texel -> [(x0, x1), ...]."""
    px = img.load()
    w, h = img.size
    ink = [any(px[x, y][3] > 0 for y in range(h)) for x in range(w)]
    runs = []
    x = 0
    while x < w:
        if ink[x]:
            s = x
            while x < w and ink[x]:
                x += 1
            runs.append((s, x - 1))
        else:
            x += 1
    return runs


def _split(a, b, n):
    """Split a merged run [a, b] evenly into n glyph boxes."""
    w = (b - a + 1) / n
    return [(round(a + i * w), round(a + (i + 1) * w) - 1) for i in range(n)]


def white_boxes(_img):
    """Proportional white atlas (aec01187 / 2cc2b764): read vs an x-ruler; merged runs
    (touching glyphs) split evenly across their glyph count."""
    b = {'0': (150, 158), '1': (161, 167), '2': (170, 178), '3': (180, 188)}
    for ch, box in zip("456", _split(190, 218, 3)):
        b[ch] = box
    for ch, box in zip("789", _split(220, 248, 3)):
        b[ch] = box
    b.update({'A': (250, 258), 'B': (260, 268), 'C': (270, 277), 'D': (280, 288),
              'E': (290, 298), 'F': (300, 308), 'G': (310, 317), 'J': (340, 346),
              'K': (350, 358), 'L': (360, 366), 'P': (400, 407), 'Q': (410, 418),
              'R': (420, 428), 'S': (430, 437), 'T': (440, 447), 'U': (450, 458),
              'V': (460, 467), 'Z': (500, 509)})
    for ch, box in zip("HI", _split(320, 335, 2)):
        b[ch] = box
    for ch, box in zip("MNO", _split(370, 398, 3)):
        b[ch] = box
    for ch, box in zip("WXY", _split(470, 496, 3)):
        b[ch] = box
    b['.'] = (130, 135)   # period (bottom dot); also the ".....' deco
    return b


def gold_boxes(img):
    """Gold menu atlas (7c1ef5cc): cleanly separated (no merges), so derive from run
    indices -- digits 0-9 = runs 16..25, letters A-Z = runs 33..58, period = run 14."""
    r = ink_runs(img)
    b = {}
    for i, ch in enumerate("0123456789"):
        b[ch] = r[16 + i]
    for i in range(26):
        b[chr(ord('A') + i)] = r[33 + i]
    b['.'] = r[14]
    return b


# hash substring -> (box-table builder, italic?)
PROFILES = {
    "aec01187": (white_boxes, True),
    "2cc2b764": (white_boxes, True),
    "7c1ef5cc": (gold_boxes, False),
}


def profile_for(name):
    for key, prof in PROFILES.items():
        if key in name:
            return prof
    raise SystemExit(f"no atlas profile matches '{name}' (known: {list(PROFILES)})")


def make_font(ttf):
    """Font sized so cap height ~= 6.8 texels of the 8-texel-tall atlas."""
    cap = round(6.8 * SCALE)
    probe = ImageFont.truetype(ttf, 200)
    pb = probe.getbbox("H")
    caph = max(1, pb[3] - pb[1])
    return ImageFont.truetype(ttf, max(8, round(200 * cap / caph)))


def glyph_tile(ch, font, colour, shear):
    bb = font.getbbox(ch, stroke_width=OUTLINE)
    gw, gh = bb[2] - bb[0], bb[3] - bb[1]
    if gw <= 0 or gh <= 0:
        return None
    tile = Image.new("RGBA", (gw + 2, gh + 2), (0, 0, 0, 0))
    ImageDraw.Draw(tile).text((1 - bb[0], 1 - bb[1]), ch, font=font, fill=colour + (255,),
                              stroke_width=OUTLINE, stroke_fill=(0, 0, 0, 255))
    if shear:
        # slant right (italic): top edge shifts by +shear*height relative to the bottom
        pad = int(abs(shear) * tile.height) + 1
        wide = Image.new("RGBA", (tile.width + pad, tile.height), (0, 0, 0, 0))
        wide.paste(tile, (pad if shear > 0 else 0, 0))
        tile = wide.transform(wide.size, Image.AFFINE, (1, shear, -shear * tile.height, 0, 1, 0),
                              resample=Image.BICUBIC)
    return tile


def ink_colour(ref, x0, x1):
    """Highlight colour of the original glyph (brightest quartile of opaque texels, so a
    white atlas stays white and a gold one stays gold -- the mean would read as grey)."""
    px = ref.load()
    h = ref.size[1]
    ink = []
    for x in range(x0, x1 + 1):
        for y in range(h):
            r, g, b, a = px[x, y]
            if a > 0:
                ink.append((r + g + b, r, g, b))
    if not ink:
        return (255, 255, 255)
    ink.sort(reverse=True)
    top = ink[: max(1, len(ink) // 4)]
    return tuple(sum(p[i] for p in top) // len(top) for i in (1, 2, 3))


def render(ref_path, ttf, ttf_italic, shear_amt):
    ref = Image.open(ref_path).convert("RGBA")
    w, h = ref.size
    boxes_fn, italic = profile_for(Path(ref_path).name)
    boxes = boxes_fn(ref)
    # italic atlas: prefer a real italic face (clean); else shear the upright font (fallback).
    if italic and ttf_italic:
        font, shear = make_font(ttf_italic), 0.0
    else:
        font, shear = make_font(ttf), (shear_amt if italic else 0.0)

    out = Image.new("RGBA", (w * SCALE, h * SCALE), (0, 0, 0, 0))     # transparent base
    for ch, (x0, x1) in boxes.items():
        tile = glyph_tile(ch, font, ink_colour(ref, x0, x1), shear)
        if tile is None:
            continue
        cx = (x0 + x1 + 1) / 2 * SCALE
        px = round(cx - tile.width / 2)
        if ch in ".,":
            py = h * SCALE - tile.height - round(0.4 * SCALE)    # punctuation on the baseline
        else:
            py = round((h * SCALE - tile.height) / 2)            # caps/digits centred
        out.alpha_composite(tile, (max(0, px), max(0, py)))
    return out, italic, len(boxes)


def main():
    ap = argparse.ArgumentParser(description="Re-render a small font atlas from a TTF (#52).")
    ap.add_argument("ref", type=Path, help="decoded atlas PNG (filename must contain the hash)")
    ap.add_argument("out", type=Path)
    ap.add_argument("--ttf", required=True, help="a bold upright TTF (used for the gold atlas)")
    ap.add_argument("--ttf-italic", dest="ttf_italic", default=None,
                    help="a bold italic/oblique TTF for the white atlases (cleanest); else --ttf is sheared")
    ap.add_argument("--shear", type=float, default=0.22, help="italic shear fallback for white atlases (0=off)")
    args = ap.parse_args()

    img, italic, n = render(args.ref, args.ttf, args.ttf_italic, args.shear)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    img.save(args.out)
    print(f"re-rendered {args.ref.name} ({'italic' if italic else 'upright'}, {n} glyphs) "
          f"via {Path(args.ttf).name} -> {args.out}")
    print("  pack with: make_pack.py <dir> --shift none")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
