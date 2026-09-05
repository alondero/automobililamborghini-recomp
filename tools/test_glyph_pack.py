#!/usr/bin/env python3
"""Regression tests for the readable-text pack generators."""

import unittest
import tempfile
from pathlib import Path

from PIL import Image, ImageFont

import render_font as rf
import build_glyph_pack as gp
import make_pack


WHITE_CHARACTERS = "!\"#$%&'()*+,-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
GOLD_CHARACTERS = "".join(chr(codepoint) for codepoint in range(ord("!"), ord("Z") + 1))


def reference_with_cell_ink(width, pitch, characters, first_cell):
    """Make isolated one-pixel glyph marks in the atlas cells used by a profile."""
    image = Image.new("RGBA", (width, 8), (0, 0, 0, 0))
    for index, _character in enumerate(characters):
        image.putpixel(((first_cell + index) * pitch + 2, 4), (255, 255, 255, 255))
    return image


class TestAtlasCoverage(unittest.TestCase):
    def test_white_profile_covers_every_authored_symbol_digit_and_letter(self):
        reference = reference_with_cell_ink(512, 10, WHITE_CHARACTERS, first_cell=0)
        self.assertEqual(set(rf.WHITE_PROFILE.boxes(reference)), set(WHITE_CHARACTERS))

    def test_gold_profile_covers_ascii_punctuation_digits_and_letters(self):
        reference = reference_with_cell_ink(512, 8, GOLD_CHARACTERS, first_cell=1)
        self.assertEqual(set(rf.GOLD_PROFILE.boxes(reference)), set(GOLD_CHARACTERS))

    def test_white_custom_slots_render_as_menu_arrows(self):
        self.assertEqual(rf.WHITE_CUSTOM_GLYPHS, {"&": "right-arrow", "'": "left-arrow"})
        style = rf.GlyphStyle((255, 255, 255), ((255, 255, 255), (160, 180, 220)))
        right = rf.arrow_tile("right-arrow", style)
        left = rf.arrow_tile("left-arrow", style)
        self.assertEqual(right.size, left.size)
        mid = right.height // 2
        self.assertGreater(right.getpixel((right.width - 5, mid))[3], 0)
        self.assertGreater(left.getpixel((5, mid))[3], 0)

    def test_gradient_endpoints_and_mask_are_exact(self):
        tile = Image.new("RGBA", (2, 3), (0, 0, 0, 0))
        mask = Image.new("L", tile.size, 0)
        for y in range(tile.height):
            mask.putpixel((0, y), 255)
        rf.apply_gradient(tile, mask, ((240, 250, 255), (60, 90, 150)))
        self.assertEqual(tile.getpixel((0, 0)), (240, 250, 255, 255))
        self.assertEqual(tile.getpixel((0, 2)), (60, 90, 150, 255))
        self.assertEqual(tile.getpixel((1, 1)), (0, 0, 0, 0))

    def test_shear_and_shadow_change_the_rendered_extent(self):
        font = ImageFont.load_default(size=32)
        plain = rf.glyph_tile("A", font, 0.0, rf.GlyphStyle((255, 255, 255)))
        styled = rf.glyph_tile(
            "A", font, 0.2,
            rf.GlyphStyle((255, 255, 255), shadow_colour=(20, 30, 50),
                          shadow_offset=(3, 4)),
        )
        self.assertGreater(styled.width, plain.width + 3)
        self.assertGreater(styled.height, plain.height)

    def test_atlas_tile_position_is_clamped_inside_its_cell_gutter(self):
        self.assertEqual(rf.position_in_cell(ideal_x=50, tile_width=70,
                                             cell_x=80, cell_width=80, gutter=4), 84)
        self.assertEqual(rf.position_in_cell(ideal_x=140, tile_width=70,
                                             cell_x=80, cell_width=80, gutter=4), 86)


class TestPackCoverage(unittest.TestCase):
    def test_chrome_hashes_cover_full_uppercase_alphabet_digits_and_slash(self):
        expected = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/")
        self.assertEqual(set(gp.CHROME_GLYPHS.values()), expected)
        self.assertEqual(len(gp.CHROME_GLYPHS), len(expected))

    def test_glyph_pack_has_no_overlapping_hash_assignments(self):
        expected_count = len(gp.ATLAS_HASHES) + len(gp.CHROME_GLYPHS) + len(gp.GOLD_GLYPHS)
        self.assertEqual(len(gp.expected_hashes()), expected_count)

class TestManifest(unittest.TestCase):
    def test_write_manifest_returns_the_database_used_by_the_pack_builder(self):
        with tempfile.TemporaryDirectory() as directory:
            pack_dir = Path(directory)
            (pack_dir / "0123456789abcdef.png").write_bytes(b"fixture")

            database = make_pack.write_manifest(pack_dir, shift="none", operation="preload")

            self.assertEqual(database["configuration"]["defaultShift"], "none")
            self.assertEqual(database["configuration"]["defaultOperation"], "preload")
            self.assertEqual(database["textures"][0]["hashes"]["rt64"],
                             "0123456789abcdef")
            self.assertTrue((pack_dir / "rt64.json").is_file())

    def test_write_manifest_rejects_an_empty_pack(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                make_pack.write_manifest(Path(directory))

    def test_write_manifest_supports_rice_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            pack_dir = Path(directory)
            (pack_dir / "0123456789abcdef.png").write_bytes(b"fixture")
            database = make_pack.write_manifest(pack_dir, auto_path="rice")
            hashes = database["textures"][0]["hashes"]
            self.assertEqual(hashes, {"rt64": "", "rice": "0123456789abcdef"})


class TestPipelines(unittest.TestCase):
    def test_render_font_end_to_end(self):
        with tempfile.TemporaryDirectory() as directory:
            reference = reference_with_cell_ink(512, 10, WHITE_CHARACTERS, 0)
            reference_path = Path(directory) / "aec011878342c59d.png"
            reference.save(reference_path)
            font = Path(__file__).parents[1] / "lib/rt64/src/contrib/mupen64plus-core/data/font.ttf"
            rendered, italic, count = rf.render(reference_path, font, None, 0.28)
            self.assertEqual(rendered.size, (reference.width * rf.SCALE,
                                             reference.height * rf.SCALE))
            self.assertTrue(italic)
            self.assertEqual(count, len(WHITE_CHARACTERS))
            self.assertGreater(rendered.getbbox()[2], 0)

    def test_build_requires_only_atlas_inputs_and_generates_full_pack(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            decoded = root / "decoded"
            pack = root / "pack"
            decoded.mkdir()
            for atlas_hash in gp.ATLAS_HASHES:
                characters, pitch, first = (GOLD_CHARACTERS, 8, 1) if atlas_hash.startswith("7c1") else (WHITE_CHARACTERS, 10, 0)
                reference_with_cell_ink(512, pitch, characters, first).save(decoded / f"{atlas_hash}.png")
            font = Path(__file__).parents[1] / "lib/rt64/src/contrib/mupen64plus-core/data/font.ttf"
            database = gp.build(decoded, pack, font)
            self.assertEqual(len(database["textures"]), len(gp.expected_hashes()))
            self.assertTrue((pack / "a14581e313afdd84.png").is_file())
            with Image.open(pack / "a14581e313afdd84.png") as image:
                self.assertEqual(image.size, (192, 192))


if __name__ == "__main__":
    unittest.main()
