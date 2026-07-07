#!/usr/bin/env python3
"""Regression tests for tools/decode_dump.py, focused on issue #50: CI4/CI8
textures decoded "sheared and miscoloured".

The root cause is the TLUT byte order. RT64's .rice.palette.rdram dump is a *raw*
copy of RDRAM, and RT64 stores every 32-bit word byte-swapped -- the logical byte
at address A physically lives at A^3 (see loadWord() in rt64_rdp.cpp,
`RDRAM[(textureAddress + i) ^ 3]`). Reading the 16-bit palette entries without that
swap maps each index to a garbled RGBA5551 value; the resulting noise still carries
the image's index structure, so it reads as a diagonal "shear". These tests pin the
correct swapped read, and drive main() end-to-end to confirm the emitted PNG colours.

Run: python tools/test_decode_dump.py   (stdlib unittest, no deps)
"""

import unittest

import decode_dump as dd


def to_physical(logical: bytes) -> bytes:
    """Lay out `logical` bytes the way RT64 stores RDRAM: byte at logical offset L
    is placed at physical offset L ^ 3 (32-bit word byte swap). Assumes a 4-aligned
    base, which is what a DMA source always is."""
    n = (len(logical) + 3) & ~3
    phys = bytearray(n)
    for i, b in enumerate(logical):
        phys[i ^ 3] = b
    return bytes(phys)


def pack_palette_logical(entries):
    """entries: list of uint16 -> big-endian logical TLUT bytes (as they sit in
    N64 memory before RT64's physical byte swap)."""
    out = bytearray()
    for v in entries:
        out += bytes(((v >> 8) & 0xFF, v & 0xFF))
    return bytes(out)


class TestRdramByteAddressing(unittest.TestCase):
    def test_physical_read_undoes_word_swap(self):
        logical = bytes(range(16))
        phys = to_physical(logical)
        got = bytes(dd.rdram_byte(phys, i, swap=True) for i in range(16))
        self.assertEqual(got, logical)

    def test_no_swap_is_identity(self):
        phys = bytes(range(16))
        got = bytes(dd.rdram_byte(phys, i, swap=False) for i in range(16))
        self.assertEqual(got, phys)

    def test_out_of_range_is_zero(self):
        self.assertEqual(dd.rdram_byte(b"\x01\x02\x03\x04", 100, swap=True), 0)


class TestPaletteByteOrder(unittest.TestCase):
    def test_swapped_read_recovers_entries(self):
        entries = [0xF801, 0x07C1, 0x003F, 0x0001, 0xFFFF, 0x8421]
        phys = to_physical(pack_palette_logical(entries))
        self.assertEqual(dd.read_palette_rdram(phys, len(entries), swap=True), entries)

    def test_unswapped_read_is_wrong_for_this_port(self):
        # The old default (straight big-endian) is exactly what miscoloured the
        # textures -- prove it disagrees with the correct swapped read.
        entries = [0xF801, 0x07C1, 0x003F, 0x8421]
        phys = to_physical(pack_palette_logical(entries))
        self.assertNotEqual(dd.read_palette_rdram(phys, len(entries), swap=False),
                            entries)


class TestEndToEnd(unittest.TestCase):
    """Drive main() over a synthetic dump laid out as RT64 writes it (TMEM in
    logical order with the odd-row xor, palette in physical RDRAM order) and
    confirm the emitted PNG pixels carry the intended palette colours.

    Note: the palette fixture is byte-swapped with the same ^3 the decoder inverts,
    so these tests pin the addressing math's *self-consistency*, not the byte-order
    hypothesis itself -- that rests on RT64's loadWord() (`RDRAM[addr ^ 3]`, the
    authoritative source) and was confirmed by eyeballing the real aec01187 dump."""

    @staticmethod
    def _decode_png(png):
        import struct
        import zlib
        idat = b""
        i = 8
        w = h = 0
        while i < len(png):
            ln = struct.unpack(">I", png[i:i + 4])[0]
            tag = png[i + 4:i + 8]
            data = png[i + 8:i + 8 + ln]
            if tag == b"IHDR":
                w, h = struct.unpack(">II", data[:8])
            elif tag == b"IDAT":
                idat += data
            i += 12 + ln
        raw = zlib.decompress(idat)
        stride = 1 + w * 4
        px = [[tuple(raw[t * stride + 1 + s * 4:t * stride + 1 + s * 4 + 4])
               for s in range(w)] for t in range(h)]
        return w, h, px

    def _run_main(self, dump, out):
        import contextlib
        import io
        import sys
        argv = sys.argv
        sys.argv = ["decode_dump.py", str(dump), "--out", str(out)]
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(dd.main(), 0)
        finally:
            sys.argv = argv

    def _write_dump(self, dump, base, tile, width, height, entries, tmem):
        import json
        (dump / (base + ".tile.json")).write_text(json.dumps(
            {"tile": tile, "width": width, "height": height, "tlut": "RGBA16"}))
        (dump / (base + ".tmem")).write_bytes(bytes(tmem))
        (dump / (base + ".rice.palette.rdram")).write_bytes(
            to_physical(pack_palette_logical(entries)))

    def test_ci8_dump_decodes_to_correct_colours(self):
        import tempfile
        from pathlib import Path

        width, height, line = 8, 4, 1        # 8 bytes/row, tmem word stride 8
        entries = [(((i & 0x1F) << 11) | ((i & 0x3F) << 5) | 0x0001)
                   for i in range(256)]

        # Build TMEM the way loadTile would: logical bytes, odd rows xor 0x4.
        tmem = bytearray(4096)
        for t in range(height):
            xor = 0x4 if (t & 1) else 0x0
            for s in range(width):
                tmem[(t * line * 8 + s) ^ xor] = (t * width + s) & 0xFF

        with tempfile.TemporaryDirectory() as d:
            dump = Path(d)
            base = "abc0000000000001.v5"
            tile = {"fmt": dd.FMT_CI, "siz": dd.SIZ_8B, "line": line, "palette": 0,
                    "tmem": 0, "uls": 0, "ult": 0}
            self._write_dump(dump, base, tile, width, height, entries, tmem)
            out = dump / "png"
            self._run_main(dump, out)

            w, h, px = self._decode_png((out / "abc0000000000001.png").read_bytes())
            self.assertEqual((w, h), (width, height))
            for (t, s) in [(0, 0), (0, 7), (1, 3), (3, 5)]:
                idx = (t * width + s) & 0xFF
                self.assertEqual(px[t][s], dd.rgba16_to_rgba8(entries[idx]),
                                 f"pixel ({t},{s})")

    def test_ci4_dump_decodes_to_correct_colours(self):
        # Issue #50's motivating case: a CI4 font-shaped strip. Exercises the
        # nibble split (high nibble = even s), a non-zero sub-palette bank, and
        # the odd-row TMEM swizzle -- end to end through main().
        import tempfile
        from pathlib import Path

        width, height, line, bank = 16, 4, 1, 2   # 8 bytes/row; palette bank 2
        pal_base = bank << 4
        entries = [(((i & 0x1F) << 11) | 0x0001) for i in range(256)]

        # Logical CI4 index for texel (t,s), then pack 2 nibbles/byte and lay the
        # bytes into TMEM in logical order with the odd-row xor 0x4.
        def idx_of(t, s):
            return (t * width + s) & 0xF

        tmem = bytearray(4096)
        for t in range(height):
            xor = 0x4 if (t & 1) else 0x0
            for sb in range(width // 2):
                byte = (idx_of(t, sb * 2) << 4) | idx_of(t, sb * 2 + 1)
                tmem[(t * line * 8 + sb) ^ xor] = byte

        with tempfile.TemporaryDirectory() as d:
            dump = Path(d)
            base = "def0000000000002.v5"
            tile = {"fmt": dd.FMT_CI, "siz": dd.SIZ_4B, "line": line,
                    "palette": bank, "tmem": 0, "uls": 0, "ult": 0}
            self._write_dump(dump, base, tile, width, height, entries, tmem)
            out = dump / "png"
            self._run_main(dump, out)

            w, h, px = self._decode_png((out / "def0000000000002.png").read_bytes())
            self.assertEqual((w, h), (width, height))
            for (t, s) in [(0, 0), (0, 1), (0, 15), (1, 4), (3, 9)]:
                exp = dd.rgba16_to_rgba8(entries[pal_base | idx_of(t, s)])
                self.assertEqual(px[t][s], exp, f"pixel ({t},{s})")


if __name__ == "__main__":
    unittest.main()
