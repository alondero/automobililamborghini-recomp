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
    "a165edf11842509a": "A", "ac27ab4ec0732a83": "D",
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


def fit_tile(tile, canvas_size, margin):
    """Scale a rendered tile down when needed so RT64 wrap cannot repeat edge ink."""
    max_width = canvas_size[0] - margin * 2
    max_height = canvas_size[1] - margin * 2
    ratio = min(1.0, max_width / tile.width, max_height / tile.height)
    if ratio < 1.0:
        tile = tile.resize((max(1, round(tile.width * ratio)),
                            max(1, round(tile.height * ratio))), Image.Resampling.LANCZOS)
    return tile


def render_single(character, ttf, source_size, shear, style):
    """Render one glyph into an 8x integer-scaled replacement canvas."""
    width, height = source_size
    canvas = Image.new("RGBA", (width * render_font.SCALE, height * render_font.SCALE),
                       (0, 0, 0, 0))
    cap = round(height * render_font.SCALE * 0.70)
    font = render_font.make_font(ttf, cap=cap)
    tile = render_font.glyph_tile(character, font, shear, style)
    if tile is None:
        raise ValueError(f"font did not render {character!r}")
    margin = render_font.SCALE
    tile = fit_tile(tile, canvas.size, margin)
    x = round((canvas.width - tile.width) / 2)
    y = render_font.vertical_position(
        character, canvas.height, tile.height,
        round(height * 0.12 * render_font.SCALE),
    )
    canvas.alpha_composite(tile, (max(0, x), max(0, y)))
    return canvas


def build(decoded_dir, pack_dir, ttf, ttf_italic=None):
    decoded_dir = Path(decoded_dir)
    pack_dir = Path(pack_dir)
    ttf = Path(ttf)
    ttf_italic = Path(ttf_italic) if ttf_italic else None

    missing = sorted(h for h in expected_hashes() if not (decoded_dir / f"{h}.png").is_file())
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
    for texture_hash, character in CHROME_GLYPHS.items():
        image = render_single(character, chrome_ttf, (16, 16), chrome_shear,
                              CHROME_STYLE)
        image.save(pack_dir / f"{texture_hash}.png")

    for texture_hash, character in GOLD_GLYPHS.items():
        gold_ttf = ttf_italic or ttf
        gold_shear = 0.08 if ttf_italic else 0.28
        image = render_single(character, gold_ttf, (24, 24), gold_shear,
                              GOLD_STYLE)
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
