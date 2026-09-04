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

- The **white** HUD/message font (`aec01187`, `2cc2b764`) uses 10-pixel cells, omits the
  `:;<=>?@` cells, and is **italic** in the original.
- The **gold** menu font (`7c1ef5cc`) uses 8-pixel cells, includes every character from
  `!` through `Z`, has a leading blank cell, and is **upright**.

So there is no single cell pitch or single style. Naively mapping both atlases to a fixed
8px grid, or rendering upright into the italic atlas, JUMBLES the text (verified: the
copyright line came out as garbage). The per-atlas layouts below were read off decoded
atlases against an x-ruler and validated in-game.

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
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SCALE = 8
OUTLINE = 4
WHITE_CHARACTERS = "!\"#$%&'()*+,-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
GOLD_CHARACTERS = "".join(chr(codepoint) for codepoint in range(ord("!"), ord("Z") + 1))


def boxes_from_cells(img, characters, pitch, first_cell=0):
    """Return each character's opaque bounds within its authored atlas cell."""
    px = img.load()
    w, h = img.size
    boxes = {}
    for index, character in enumerate(characters):
        cell_x0 = (first_cell + index) * pitch
        cell_x1 = min(w, cell_x0 + pitch) - 1
        ink = [x for x in range(cell_x0, cell_x1 + 1)
               if any(px[x, y][3] > 0 for y in range(h))]
        if not ink:
            raise ValueError(f"atlas cell for {character!r} at x={cell_x0} has no ink")
        boxes[character] = (min(ink), max(ink))
    return boxes


def position_in_cell(ideal_x, tile_width, cell_x, cell_width, gutter):
    """Clamp a tile's x position so its ink cannot leak into a neighbouring cell."""
    left = cell_x + gutter
    right = cell_x + cell_width - gutter - tile_width
    if right < left:
        raise ValueError("tile is wider than the atlas cell's usable area")
    return min(max(round(ideal_x), left), right)


@dataclass(frozen=True)
class AtlasProfile:
    italic: bool
    characters: str
    pitch: int
    first_cell: int = 0

    def boxes(self, image):
        return boxes_from_cells(image, self.characters, self.pitch, self.first_cell)


WHITE_PROFILE = AtlasProfile(True, WHITE_CHARACTERS, 10)
GOLD_PROFILE = AtlasProfile(False, GOLD_CHARACTERS, 8, first_cell=1)
PROFILES = {
    "aec01187": WHITE_PROFILE,
    "2cc2b764": WHITE_PROFILE,
    "7c1ef5cc": GOLD_PROFILE,
}


def profile_for(name):
    for key, prof in PROFILES.items():
        if key in name:
            return prof
    raise SystemExit(f"no atlas profile matches '{name}' (known: {list(PROFILES)})")


def make_font(ttf, cap=None):
    """Return a font sized to the requested cap height in output pixels."""
    cap = round(6.8 * SCALE) if cap is None else cap
    probe = ImageFont.truetype(ttf, 200)
    pb = probe.getbbox("H")
    caph = max(1, pb[3] - pb[1])
    return ImageFont.truetype(ttf, max(8, round(200 * cap / caph)))


def glyph_tile(ch, font, colour, shear, outline=OUTLINE):
    bb = font.getbbox(ch, stroke_width=outline)
    gw, gh = bb[2] - bb[0], bb[3] - bb[1]
    if gw <= 0 or gh <= 0:
        return None
    tile = Image.new("RGBA", (gw + 2, gh + 2), (0, 0, 0, 0))
    ImageDraw.Draw(tile).text((1 - bb[0], 1 - bb[1]), ch, font=font, fill=colour + (255,),
                              stroke_width=outline, stroke_fill=(0, 0, 0, 255))
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
    profile = profile_for(Path(ref_path).name)
    boxes = profile.boxes(ref)
    italic = profile.italic
    # italic atlas: prefer a real italic face (clean); else shear the upright font (fallback).
    if italic and ttf_italic:
        font, shear = make_font(ttf_italic), 0.0
    else:
        font, shear = make_font(ttf), (shear_amt if italic else 0.0)

    rendered = []
    for ch, (x0, x1) in boxes.items():
        tile = glyph_tile(ch, font, ink_colour(ref, x0, x1), shear)
        if tile is None:
            continue
        rendered.append((ch, x0, x1, tile))

    # A replacement glyph must stay inside its source atlas cell. Otherwise the game's
    # cell UV samples the edge of the next rendered glyph (e.g. `ARCADE|`). Scale the
    # whole typeface uniformly when its widest glyph needs more than the available cell.
    gutter = SCALE // 2
    usable_width = profile.pitch * SCALE - gutter * 2
    widest = max(tile.width for _ch, _x0, _x1, tile in rendered)
    ratio = min(1.0, usable_width / widest)
    if ratio < 1.0:
        rendered = [(ch, x0, x1,
                     tile.resize((max(1, round(tile.width * ratio)),
                                  max(1, round(tile.height * ratio))), Image.Resampling.LANCZOS))
                    for ch, x0, x1, tile in rendered]

    out = Image.new("RGBA", (w * SCALE, h * SCALE), (0, 0, 0, 0))
    for ch, x0, x1, tile in rendered:
        cx = (x0 + x1 + 1) / 2 * SCALE
        cell_index = profile.first_cell + profile.characters.index(ch)
        px = position_in_cell(cx - tile.width / 2, tile.width,
                              cell_index * profile.pitch * SCALE,
                              profile.pitch * SCALE, gutter)
        if ch in ".,":
            py = h * SCALE - tile.height - round(0.4 * SCALE)
        elif ch in "\"'":
            py = round(0.4 * SCALE)
        else:
            py = round((h * SCALE - tile.height) / 2)
        out.alpha_composite(tile, (px, max(0, py)))
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
