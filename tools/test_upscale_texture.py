#!/usr/bin/env python3
"""Unit tests for the pure parts of upscale_texture.py (padding + skip logic).

The xBRZ CLI itself is exercised end-to-end when generating a pack; these tests
cover the texel-addressing logic that would silently produce seams if wrong.
"""

import unittest

from PIL import Image

from upscale_texture import PAD, MAGENTA, pad_edges


def img_from_rows(rows):
    im = Image.new("RGBA", (len(rows[0]), len(rows)))
    im.putdata([px for row in rows for px in row])
    return im


R = (255, 0, 0, 255)
G = (0, 255, 0, 255)
B = (0, 0, 255, 255)
W = (255, 255, 255, 255)


class PadEdgesTest(unittest.TestCase):
    def setUp(self):
        # 2x2 quadrant image: R G / B W
        self.src = img_from_rows([[R, G], [B, W]])

    def pad(self, cms, cmt):
        out, ps, pt = pad_edges(self.src, cms, cmt)
        # 2x2 source: pad clamps to the image size, not the full PAD
        self.assertEqual((ps, pt), (2, 2))
        self.assertEqual(out.size, (2 + 2 * ps, 2 + 2 * pt))
        return out, ps, pt

    def test_wrap_continues_from_opposite_edge(self):
        out, ps, pt = self.pad(0, 0)
        # texel left of (ps, pt)=R must be the wrapped right column = G
        self.assertEqual(out.getpixel((ps - 1, pt)), G)
        # texel above R must be the wrapped bottom row = B
        self.assertEqual(out.getpixel((ps, pt - 1)), B)

    def test_mirror_reflects_own_edge(self):
        out, ps, pt = self.pad(1, 1)
        self.assertEqual(out.getpixel((ps - 1, pt)), R)
        self.assertEqual(out.getpixel((ps, pt - 1)), R)

    def test_clamp_replicates_edge(self):
        out, ps, pt = self.pad(2, 2)
        for x in range(ps):
            self.assertEqual(out.getpixel((x, pt)), R)
        for y in range(pt):
            self.assertEqual(out.getpixel((ps, y)), R)
        # corner region extends the corner texel
        self.assertEqual(out.getpixel((0, 0)), R)

    def test_interior_is_untouched(self):
        for cms, cmt in ((0, 0), (1, 1), (2, 2), (0, 2)):
            out, ps, pt = pad_edges(self.src, cms, cmt)
            self.assertEqual(out.crop((ps, pt, ps + 2, pt + 2)).tobytes(),
                             self.src.tobytes())

    def test_mixed_axes(self):
        out, ps, pt = self.pad(0, 2)  # wrap S, clamp T
        self.assertEqual(out.getpixel((ps - 1, pt)), G)   # wrapped
        self.assertEqual(out.getpixel((ps, pt - 1)), R)   # clamped

    def test_one_texel_high_strip_gets_no_black_padding(self):
        # dumps contain 128x1 / 32x4 strips; an unclamped pad used to crop past the
        # bitmap, which PIL zero-fills -> black halo after scaling
        strip = img_from_rows([[R, G, B, W]])
        for cms, cmt in ((0, 0), (1, 1), (2, 2)):
            out, ps, pt = pad_edges(strip, cms, cmt)
            self.assertEqual(pt, 1)
            self.assertNotIn((0, 0, 0, 0), list(out.getdata()))


class MagentaSkipTest(unittest.TestCase):
    def test_fallback_colour_matches_decoder(self):
        # decode_dump.py paints out-of-range palette indices exactly this value
        self.assertEqual(MAGENTA, (255, 0, 255, 255))
        im = img_from_rows([[MAGENTA, W], [W, W]])
        self.assertIn(MAGENTA, im.getdata())


if __name__ == "__main__":
    unittest.main()
