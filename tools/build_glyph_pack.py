#!/usr/bin/env python3
"""Build the glyph-only v1 readable-text RT64 pack.

The decoded dump is used only as a compatibility/coverage input: it proves that the
current renderer produced every expected version-5 hash and supplies the three small
atlas palettes. The generated replacements contain newly rendered Lato (or another
user-supplied open font), not decoded game pixels.

    python tools/build_glyph_pack.py <decoded-png-dir> <pack-dir> --ttf <bold.ttf> \
        [--ttf-italic <bold-italic.ttf>]

The output is a loose PNG pack with `rt64.json`. Package it with RT64's
`texture_packer <pack-dir> --create-low-mip-cache` followed by `--create-pack`.
"""

import argparse
from pathlib import Path

from PIL import Image

import make_pack
import render_font


ATLAS_HASHES = (
    "2cc2b76457edc973",
    "7c1ef5cc1ab579eb",
    "aec011878342c59d",
)

# 16x16 blue-chrome race HUD glyphs. The name-entry grid loads the complete alphabet;
# race counters load 0-9 and slash. Hashes are RT64 v5 TMEM identities.
CHROME_GLYPHS = {
    "0a315844174cfb29": "4", "1e77b22bb573e7e2": "F",
    "235999eddd95ee7c": "I", "2837e0b9d52356db": "S",
    "3420d09ecce9ef25": "G", "352665c5960cb5d4": "1",
    "36af059e77647056": "8", "3d43f4da2fee0d5f": "K",
    "3e0f930e1c864292": "3", "4b9c0f6df03f1f01": "O",
    "5053d266ff712562": "H", "518a46d0218843b3": "/",
    "5a2357323e4142ca": "L", "5b2c075c3f864584": "X",
    "5d831916dd79a59f": "R", "6033a2def586ad33": "Z",
    "65d5358cd5481e4e": "2", "6bda9baf892ead8e": "T",
    "87a8d80d57edfa4d": "P", "8ce8877db09a2303": "V",
    "9cd784f1c2bad952": "6", "a1e1184fcad30cb4": "A",
    "a493cfa4d247d486": "D", "a705d62d86e0b5b2": "M",
    "c3ef8dd57491f5fb": "U", "c422a81127e92249": "W",
    "cf4754560f6a7b65": "9", "d01dcfb123ae5608": "5",
    "d92c5861bb5fd140": "0", "d94580de1591993b": "B",
    "e4219dedb67aa2ca": "J", "e929071aa32f1b1b": "Y",
    "ea86f6a09076bee9": "E", "ee380fb2c8f4c1d7": "Q",
    "ef6c38bc1ea56478": "C", "fabfbfb7791da252": "N",
    "fb5243ac423b8aac": "7",
}

# 24x24 gold message/countdown glyphs observed in driven menu and race captures. Keep
# this map capture-backed: alternate screens add hashes even when the character already
# exists in another font family.
GOLD_GLYPHS = {
    "1ca0ebcef7bd9d17": "K", "1d1c418210eb8eb9": "G",
    "612ea5fbdcf2041e": "N", "69f496caa4a581b4": "E",
    "6b5caa506c64dc65": "H", "82cd326180c0a37f": "C",
    # I captured from the active gold UI glyph at RDRAM 0x0014F020 on 2026-08-29;
    # decoded as 24x24 RGBA32 and hashed with RT64's v5 TMEM format.
    "a14581e313afdd84": "I", "a165edf11842509a": "A", "ac27ab4ec0732a83": "D",
    "af3e5e7b4903e514": "Y", "b45e8785058d9d71": ".",
    "b724e8b4f81ca6ce": "T", "b8e41873b7efd84a": "O",
    "be528398b54d1001": "R", "c6c7989916f1c72c": "W",
    "d4aaa411f6499884": "S", "dfb262661f6e60f8": "M",
    "fc25c298a9daa547": "P",
    # Decoded runtime captures, 2026-08-29: U from Start-held LAMBO_WARP=1;
    # L from the last-lap state reached by a LAMBO_WARP=1:1 driven run.
    "7112047fab42df0d": "U", "e56e844ba1d87442": "L",
}

CHROME_STYLE = render_font.GlyphStyle(
    colour=(210, 228, 255), gradient=((250, 253, 255), (116, 153, 211)),
    outline=6, shadow_colour=(36, 54, 91), shadow_offset=(3, 4),
)
GOLD_STYLE = render_font.GlyphStyle(
    colour=(245, 211, 82), gradient=((255, 241, 105), (186, 132, 31)),
    outline=7, shadow_colour=(78, 50, 12), shadow_offset=(3, 4),
)


def expected_hashes():
    return set(ATLAS_HASHES) | set(CHROME_GLYPHS) | set(GOLD_GLYPHS)


def render_single(character, font, source_size, shear, style, scale=1.0):
    """Render one glyph into an 8x integer-scaled replacement canvas."""
    width, height = source_size
    canvas = Image.new("RGBA", (width * render_font.SCALE, height * render_font.SCALE),
                       (0, 0, 0, 0))
    tile = render_font.glyph_tile(character, font, shear, style)
    if tile is None:
        raise ValueError(f"font did not render {character!r}")
    margin = render_font.SCALE
    if scale < 1.0:
        tile = tile.resize((max(1, round(tile.width * scale)),
                            max(1, round(tile.height * scale))), Image.Resampling.LANCZOS)
    x = round((canvas.width - tile.width) / 2)
    y = render_font.vertical_position(
        canvas.height, tile.height, render_font.Alignment.CENTER,
        round(height * 0.12 * render_font.SCALE),
    )
    canvas.alpha_composite(tile, (max(0, x), max(0, y)))
    return canvas


def render_family(glyphs, font, source_size, shear, style):
    """Render a family at one shared scale so stroke weight stays consistent."""
    tiles = {}
    for texture_hash, character in glyphs.items():
        tile = render_font.glyph_tile(character, font, shear, style)
        if tile is None:
            raise ValueError(f"font did not render {character!r}")
        tiles[texture_hash] = (character, tile)
    width, height = source_size
    usable_width = width * render_font.SCALE - 2 * render_font.SCALE
    usable_height = height * render_font.SCALE - 2 * render_font.SCALE
    widest = max(tile.width for _character, tile in tiles.values())
    tallest = max(tile.height for _character, tile in tiles.values())
    scale = min(1.0, usable_width / widest, usable_height / tallest)
    return {texture_hash: render_single(character, font, source_size, shear, style, scale)
            for texture_hash, (character, _tile) in tiles.items()}


def build(decoded_dir, pack_dir, ttf, ttf_italic=None):
    decoded_dir = Path(decoded_dir)
    pack_dir = Path(pack_dir)
    ttf = Path(ttf)
    ttf_italic = Path(ttf_italic) if ttf_italic else None

    missing = sorted(h for h in ATLAS_HASHES if not (decoded_dir / f"{h}.png").is_file())
    if missing:
        raise ValueError("decoded dump is missing expected glyph hashes: " + ", ".join(missing))

    if pack_dir.exists() and any(pack_dir.iterdir()):
        raise ValueError(f"output directory must be empty: {pack_dir}")
    pack_dir.mkdir(parents=True, exist_ok=True)

    for atlas_hash in ATLAS_HASHES:
        reference = decoded_dir / f"{atlas_hash}.png"
        image, _italic, _count = render_font.render(reference, ttf, ttf_italic, 0.28)
        image.save(pack_dir / f"{atlas_hash}.png")

    chrome_ttf = ttf_italic or ttf
    chrome_shear = 0.08 if ttf_italic else 0.28
    chrome_font = render_font.make_font(chrome_ttf, cap=round(16 * render_font.SCALE * 0.70))
    for texture_hash, image in render_family(CHROME_GLYPHS, chrome_font, (16, 16),
                                             chrome_shear, CHROME_STYLE).items():
        image.save(pack_dir / f"{texture_hash}.png")

    gold_font = render_font.make_font(chrome_ttf, cap=round(24 * render_font.SCALE * 0.70))
    gold_shear = 0.08 if ttf_italic else 0.28
    for texture_hash, image in render_family(GOLD_GLYPHS, gold_font, (24, 24),
                                             gold_shear, GOLD_STYLE).items():
        image.save(pack_dir / f"{texture_hash}.png")

    database = make_pack.write_manifest(pack_dir, shift="none", operation="stream")
    if len(database["textures"]) != len(expected_hashes()):
        raise RuntimeError("manifest texture count does not match glyph coverage")
    return database


def main():
    parser = argparse.ArgumentParser(description="Build the glyph-only readable-text pack")
    parser.add_argument("decoded_dir", type=Path)
    parser.add_argument("pack_dir", type=Path)
    parser.add_argument("--ttf", required=True, type=Path, help="open-licensed bold TTF")
    parser.add_argument("--ttf-italic", type=Path, help="open-licensed bold italic TTF")
    args = parser.parse_args()

    try:
        database = build(args.decoded_dir, args.pack_dir, args.ttf, args.ttf_italic)
    except (OSError, ValueError) as error:
        print(f"error: {error}")
        return 1
    print(f"glyph pack ready: {args.pack_dir} ({len(database['textures'])} replacements)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
