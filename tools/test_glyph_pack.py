#!/usr/bin/env python3
"""Regression tests for the readable-text pack generators."""

import unittest
import tempfile
from pathlib import Path

from PIL import Image, ImageFont

import render_font as rf
import upscale_font as uf
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


class TestColourBleed(unittest.TestCase):
    def test_colour_propagates_beyond_the_first_transparent_ring(self):
        image = Image.new("RGBA", (5, 1), (0, 0, 255, 0))
        image.putpixel((2, 0), (240, 20, 10, 255))

        result = uf.bleed_colour(image, iterations=2)

        self.assertEqual([result.getpixel((x, 0))[:3] for x in range(5)],
                         [(240, 20, 10)] * 5)
        self.assertEqual([result.getpixel((x, 0))[3] for x in range(5)],
                         [0, 0, 255, 0, 0])


class TestAtlasCoverage(unittest.TestCase):
    def test_white_profile_covers_every_authored_symbol_digit_and_letter(self):
        reference = reference_with_cell_ink(512, 10, WHITE_CHARACTERS, first_cell=0)
        self.assertEqual(set(rf.WHITE_PROFILE.boxes(reference)), set(WHITE_CHARACTERS))

    def test_gold_profile_covers_ascii_punctuation_digits_and_letters(self):
        reference = reference_with_cell_ink(512, 8, GOLD_CHARACTERS, first_cell=1)
        self.assertEqual(set(rf.GOLD_PROFILE.boxes(reference)), set(GOLD_CHARACTERS))

    def test_both_atlases_cover_every_issue_countdown_word(self):
        required = set("ONETWOTHREEFOURGO")
        self.assertTrue(required.issubset(set(rf.WHITE_CHARACTERS)))
        self.assertTrue(required.issubset(set(rf.GOLD_CHARACTERS)))

    def test_both_small_atlas_profiles_enable_italic_rendering(self):
        self.assertTrue(rf.WHITE_PROFILE.italic)
        self.assertTrue(rf.GOLD_PROFILE.italic)

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

    def test_runtime_captured_pause_and_last_lap_hashes_are_mapped(self):
        self.assertEqual(gp.GOLD_GLYPHS["7112047fab42df0d"], "U")
        self.assertEqual(gp.GOLD_GLYPHS["e56e844ba1d87442"], "L")

    def test_runtime_captured_checkpoint_i_hash_is_mapped(self):
        self.assertEqual(gp.GOLD_GLYPHS["a14581e313afdd84"], "I")

    def test_oversize_glyph_is_fitted_inside_a_transparent_margin(self):
        tile = Image.new("RGBA", (200, 100), (255, 255, 255, 255))
        fitted = gp.fit_tile(tile, (128, 128), margin=8)
        self.assertLessEqual(fitted.width, 112)
        self.assertLessEqual(fitted.height, 112)


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


if __name__ == "__main__":
    unittest.main()
