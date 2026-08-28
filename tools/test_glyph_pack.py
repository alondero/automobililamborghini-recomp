#!/usr/bin/env python3
"""Regression tests for the readable-text pack generators."""

import unittest
import tempfile
from pathlib import Path

from PIL import Image

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
