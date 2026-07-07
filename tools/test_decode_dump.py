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
        got = bytes(dd.rdram_byte(phys, 0, i, swap=True) for i in range(16))
        self.assertEqual(got, logical)

    def test_no_swap_is_identity(self):
        phys = bytes(range(16))
        got = bytes(dd.rdram_byte(phys, 0, i, swap=False) for i in range(16))
        self.assertEqual(got, phys)

    def test_aligned_base_matches_plain_xor3(self):
        # DMA sources are 8-aligned, so the anchored swap reduces to a plain L^3.
        buf = bytes(range(16))
        for base in (0, 0x1000, 0x2E800):
            for L in range(16):
                self.assertEqual(dd.rdram_byte(buf, base, L, swap=True),
                                 dd.rdram_byte(buf, 0, L, swap=True),
                                 f"base={base:#x} L={L}")


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


class TestRiceRdramStart(unittest.TestCase):
    def test_block_uses_full_ult(self):
        import json
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.json"
            p.write_text(json.dumps({
                "type": "Block", "tile": {"uls": 0, "ult": 2},
                "texture": {"address": 0x1000, "siz": 0, "width": 8}}))
            # common_bpr = 8<<0>>1 = 4; +4*2 = 8
            self.assertEqual(dd.rice_rdram_start(p), 0x1000 + 8)

    def test_missing_file_is_zero(self):
        from pathlib import Path
        self.assertEqual(dd.rice_rdram_start(Path("does_not_exist.json")), 0)


class TestEndToEnd(unittest.TestCase):
    """Drive main() over a synthetic dump laid out as RT64 writes it (TMEM in
    logical order with the odd-row xor, palette in physical RDRAM order) and
    confirm the emitted PNG pixels carry the intended palette colours."""

    def test_ci8_dump_decodes_to_correct_colours(self):
        import json
        import struct
        import tempfile
        import zlib
        from pathlib import Path

        width, height, line = 8, 4, 1        # 8 bytes/row, tmem word stride 8
        entries = [(((i & 0x1F) << 11) | ((i & 0x3F) << 5) | 0x0001)
                   for i in range(256)]

        # Build TMEM the way loadTile would: logical bytes, odd rows xor 0x4.
        tmem = bytearray(4096)
        for t in range(height):
            xor = 0x4 if (t & 1) else 0x0
            for s in range(width):
                idx = (t * width + s) & 0xFF
                tmem[(t * line * 8 + s) ^ xor] = idx

        with tempfile.TemporaryDirectory() as d:
            dump = Path(d)
            base = "abc0000000000001.v5"
            tile = {"fmt": dd.FMT_CI, "siz": dd.SIZ_8B, "line": line, "palette": 0,
                    "tmem": 0, "uls": 0, "ult": 0}
            (dump / (base + ".tile.json")).write_text(json.dumps(
                {"tile": tile, "width": width, "height": height, "tlut": "RGBA16"}))
            (dump / (base + ".tmem")).write_bytes(bytes(tmem))
            (dump / (base + ".rice.palette.rdram")).write_bytes(
                to_physical(pack_palette_logical(entries)))
            (dump / (base + ".rice.palette.json")).write_text(json.dumps(
                {"tile": {"uls": 0, "ult": 0}, "type": "TLUT",
                 "texture": {"address": 0x2000, "siz": 2, "width": 256}}))

            out = dump / "png"
            import sys
            argv = sys.argv
            sys.argv = ["decode_dump.py", str(dump), "--out", str(out)]
            try:
                self.assertEqual(dd.main(), 0)
            finally:
                sys.argv = argv

            png = (out / (base.split(".")[0] + ".png")).read_bytes()
            self.assertTrue(png.startswith(b"\x89PNG"))
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
            self.assertEqual((w, h), (width, height))
            raw = zlib.decompress(idat)
            stride = 1 + width * 4
            for (t, s) in [(0, 0), (0, 7), (1, 3), (3, 5)]:
                idx = (t * width + s) & 0xFF
                exp = dd.rgba16_to_rgba8(entries[idx])
                off = t * stride + 1 + s * 4
                self.assertEqual(tuple(raw[off:off + 4]), exp, f"pixel ({t},{s})")


if __name__ == "__main__":
    unittest.main()
