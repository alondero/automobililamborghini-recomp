#!/usr/bin/env python3
"""Re-render the game's small font atlases from a vector (TTF) font (issue #52).

Bitmap interpolation only smooths the original 8px glyphs -- it cannot add detail an 8px
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
  `!` through `Z` and has a leading blank cell. This renderer gives it the same
  forward-leaning treatment as the larger headings for a more coherent modern pack.

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
- **Italic atlases**: pass a real italic/oblique face via `--ttf-italic` for the cleanest
  result. A small additional shear keeps a modern face close to the original game's stronger
  motorsport slant. Without an italic face, the upright `--ttf` is sheared as a fallback.

Pack the result with `make_pack.py <dir> --shift none` (the output is grid-aligned; `half`
over-shifts it -- see docs/TEXTURES.md). Ship with an OPEN font (e.g. Liberation/DejaVu Bold)
to avoid embedding a proprietary face.

    python tools/render_font.py <decoded_atlas.png> <out.png> --ttf <bold.ttf> \
        [--ttf-italic <bold-oblique.ttf>] [--shear 0.28] [--extra-shear 0.08]

The atlas profile (positions + italic flag) is auto-selected from the hash in the filename.

Requires Pillow. No numpy.
"""

import argparse
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SCALE = 8
OUTLINE = 4
WHITE_CHARACTERS = "!\"#$%&'()*+,-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
GOLD_CHARACTERS = "".join(chr(codepoint) for codepoint in range(ord("!"), ord("Z") + 1))
# Stock pause repro: LAMBO_WARP=1, press Start. These two source slots draw the
# opposing CONTINUE cursor arrows; treating them as ASCII produces literal `&` / `'`.
WHITE_CUSTOM_GLYPHS = {"&": "right-arrow", "'": "left-arrow"}


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


class Alignment(Enum):
    CENTER = "center"
    BASELINE = "baseline"
    TOP = "top"


def vertical_position(canvas_height, tile_height, alignment, punctuation_inset, top_inset=None):
    """Place a glyph according to an explicit alignment role."""
    if alignment is Alignment.BASELINE:
        return canvas_height - tile_height - punctuation_inset
    if alignment is Alignment.TOP:
        return punctuation_inset if top_inset is None else top_inset
    return round((canvas_height - tile_height) / 2)


def tonal_gradient(colour, strength):
    """Return a restrained highlight-to-shadow ramp derived from a sampled ink colour."""
    top = tuple(round(channel + (255 - channel) * strength) for channel in colour)
    bottom = tuple(round(channel * (1.0 - strength)) for channel in colour)
    return top, bottom


def apply_gradient(tile, mask, gradient):
    """Paint a vertical colour ramp through a supplied coverage mask."""
    if gradient is None:
        return tile
    ramp = Image.new("RGBA", (1, tile.height), (0, 0, 0, 0))
    top, bottom = gradient
    denominator = max(1, tile.height - 1)
    for y in range(tile.height):
        amount = y / denominator
        row = tuple(round(top[i] * (1.0 - amount) + bottom[i] * amount)
                    for i in range(3))
        ramp.putpixel((0, y), row + (255,))
    ramp = ramp.resize(tile.size, Image.Resampling.BILINEAR)
    tile.paste(ramp, (0, 0), mask)
    return tile


@dataclass(frozen=True)
class GlyphStyle:
    colour: tuple
    gradient: tuple | None = None
    outline: int = OUTLINE
    shadow_colour: tuple | None = None
    shadow_offset: tuple = (0, 0)


def arrow_tile(direction, style):
    """Draw a modern vector replacement for a custom pause cursor slot."""
    # Nearly fill the 10x8 source cell so the cursor retains a deliberate, chunky weight.
    width, height = 9 * SCALE, 7 * SCALE
    mid = height // 2
    inset = style.outline + 1
    head_x = round(width * 0.54)
    upper = round(height * 0.31)
    lower = height - upper
    points = [
        (inset, upper), (head_x, upper), (head_x, inset),
        (width - inset, mid), (head_x, height - inset),
        (head_x, lower), (inset, lower),
    ]
    if direction == "left-arrow":
        points = [(width - x, y) for x, y in points]
    elif direction != "right-arrow":
        raise ValueError(f"unknown arrow direction: {direction}")

    tile = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(tile)
    draw.polygon(points, fill=style.colour + (255,), outline=(0, 0, 0, 255),
                 width=style.outline)
    mask = Image.new("L", tile.size, 0)
    ImageDraw.Draw(mask).polygon(points, fill=255)
    apply_gradient(tile, mask, style.gradient)
    # Keep the keyline after applying the ramp to the polygon interior.
    ImageDraw.Draw(tile).line(points + [points[0]], fill=(0, 0, 0, 255),
                              width=style.outline, joint="curve")
    return tile


@dataclass(frozen=True)
class AtlasProfile:
    italic: bool
    characters: str
    pitch: int
    first_cell: int = 0
    gradient_strength: float = 0.0

    def boxes(self, image):
        return boxes_from_cells(image, self.characters, self.pitch, self.first_cell)


WHITE_PROFILE = AtlasProfile(True, WHITE_CHARACTERS, 10, gradient_strength=0.12)
GOLD_PROFILE = AtlasProfile(True, GOLD_CHARACTERS, 8, first_cell=1,
                            gradient_strength=0.24)
PROFILES = {
    "aec01187": WHITE_PROFILE,
    "2cc2b764": WHITE_PROFILE,
    "7c1ef5cc": GOLD_PROFILE,
}


def profile_for(name):
    for key, prof in PROFILES.items():
        if key in name:
            return prof
    raise ValueError(f"no atlas profile matches '{name}' (known: {list(PROFILES)})")


def make_font(ttf, cap=None):
    """Return a font sized to the requested cap height in output pixels."""
    cap = round(6.8 * SCALE) if cap is None else cap
    probe = ImageFont.truetype(ttf, 200)
    pb = probe.getbbox("H")
    caph = max(1, pb[3] - pb[1])
    return ImageFont.truetype(ttf, max(8, round(200 * cap / caph)))


def glyph_tile(ch, font, shear, style):
    bb = font.getbbox(ch, stroke_width=style.outline)
    gw, gh = bb[2] - bb[0], bb[3] - bb[1]
    if gw <= 0 or gh <= 0:
        return None
    shadow_x, shadow_y = style.shadow_offset
    tile = Image.new("RGBA", (gw + 2 + abs(shadow_x), gh + 2 + abs(shadow_y)),
                     (0, 0, 0, 0))
    origin = (1 - bb[0] + max(0, -shadow_x), 1 - bb[1] + max(0, -shadow_y))
    draw = ImageDraw.Draw(tile)
    if style.shadow_colour is not None:
        draw.text((origin[0] + shadow_x, origin[1] + shadow_y), ch, font=font,
                  fill=style.shadow_colour + (255,), stroke_width=style.outline,
                  stroke_fill=(0, 0, 0, 255))
    draw.text(origin, ch, font=font, fill=style.colour + (255,),
              stroke_width=style.outline,
              stroke_fill=(0, 0, 0, 255))
    if style.gradient is not None:
        mask = Image.new("L", tile.size, 0)
        ImageDraw.Draw(mask).text(origin, ch, font=font, fill=255)
        apply_gradient(tile, mask, style.gradient)
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


def render(ref_path, ttf, ttf_italic, shear_amt, extra_shear=0.08):
    with Image.open(ref_path) as source:
        ref = source.convert("RGBA")
    w, h = ref.size
    profile = profile_for(Path(ref_path).name)
    boxes = profile.boxes(ref)
    italic = profile.italic
    # Prefer a real italic face, then retain a little of the original's stronger slant.
    if italic and ttf_italic:
        font, shear = make_font(ttf_italic), extra_shear
    else:
        font, shear = make_font(ttf), (shear_amt if italic else 0.0)

    rendered = []
    for ch, (x0, x1) in boxes.items():
        colour = ink_colour(ref, x0, x1)
        gradient = tonal_gradient(colour, profile.gradient_strength)
        style = GlyphStyle(colour, gradient)
        custom_glyph = WHITE_CUSTOM_GLYPHS.get(ch) if profile is WHITE_PROFILE else None
        if custom_glyph:
            tile = arrow_tile(custom_glyph, style)
        else:
            tile = glyph_tile(ch, font, shear, style)
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
        if profile is WHITE_PROFILE and ch in WHITE_CUSTOM_GLYPHS:
            alignment = Alignment.CENTER
        elif ch in ".,":
            alignment = Alignment.BASELINE
        elif ch in "\"'":
            alignment = Alignment.TOP
        else:
            alignment = Alignment.CENTER
        py = vertical_position(h * SCALE, tile.height, alignment,
                               round(0.4 * SCALE))
        out.alpha_composite(tile, (px, max(0, py)))
    return out, italic, len(boxes)


def main():
    ap = argparse.ArgumentParser(description="Re-render a small font atlas from a TTF (#52).")
    ap.add_argument("ref", type=Path, help="decoded atlas PNG (filename must contain the hash)")
    ap.add_argument("out", type=Path)
    ap.add_argument("--ttf", required=True, help="a bold upright TTF (used for the gold atlas)")
    ap.add_argument("--ttf-italic", dest="ttf_italic", default=None,
                    help="a bold italic/oblique TTF for the white atlases (cleanest); else --ttf is sheared")
    ap.add_argument("--shear", type=float, default=0.28,
                    help="italic shear when only the upright face is available (default 0.28)")
    ap.add_argument("--extra-shear", type=float, default=0.08,
                    help="additional slant applied to an italic face (default 0.08)")
    args = ap.parse_args()

    img, italic, n = render(args.ref, args.ttf, args.ttf_italic, args.shear,
                            args.extra_shear)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    img.save(args.out)
    print(f"re-rendered {args.ref.name} ({'italic' if italic else 'upright'}, {n} glyphs) "
          f"via {Path(args.ttf).name} -> {args.out}")
    print("  pack with: make_pack.py <dir> --shift none")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
