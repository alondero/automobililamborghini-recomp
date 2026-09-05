#!/usr/bin/env python3
"""Synthetic, ROM-free tests for tools/track_lab.py."""

from __future__ import annotations

import copy
import io
import json
import struct
import tempfile
import unittest
from pathlib import Path

from tools import track_lab as tl


RDRAM_BASE = tl.RDRAM_BASE


def put_logical(rdram: bytearray, address: int, data: bytes) -> None:
    """Store logical N64 bytes in N64Recomp's physical word-swapped layout."""
    offset = address - RDRAM_BASE
    for index, value in enumerate(data):
        rdram[(offset + index) ^ 3] = value


def put_u32(rdram: bytearray, address: int, value: int) -> None:
    put_logical(rdram, address, struct.pack(">I", value))


def put_s16(rdram: bytearray, address: int, value: int) -> None:
    put_logical(rdram, address, struct.pack(">h", value))


def independent_fnv(rows: list[list[int]]) -> int:
    value = 0xCBF29CE484222325
    for row in rows:
        for cell in row:
            for byte in struct.pack(">h", cell):
                value ^= byte
                value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def make_rdram() -> bytes:
    memory = bytearray(tl.RDRAM_SIZE)
    context = 0x80010000
    segment_base = 0x80100000
    pvs_base = 0x80110000
    row_count = 3
    pvs_end = pvs_base + row_count * tl.PVS_ROW_SIZE
    unknown_base = 0x80120000
    anchor_base = 0x80130000

    put_u32(memory, tl.ACTIVE_CONTEXT_PTR, context)
    for offset, pointer in enumerate(
        (segment_base, pvs_base, pvs_end, unknown_base, anchor_base)
    ):
        put_u32(memory, context + offset * 4, pointer)
    put_s16(memory, tl.ACTIVE_CIRCUIT, 2)
    put_u32(memory, unknown_base, 0x12345678)

    # Multiple distinct negative values prove that a hole is not a terminator
    # and that guarded edits retain the original raw expected value.
    rows = [
        [0, -1, 2, -2, -32768, 1, -1, -1, -1, -1],
        [1, 0, -1, -1, -1, 2, -1, -1, -1, -1],
        [2, 1, 0, -1, -1, -1, -1, -1, -1, -1],
    ]
    for row_index, row in enumerate(rows):
        for slot, value in enumerate(row):
            put_s16(
                memory,
                pvs_base + row_index * tl.PVS_ROW_SIZE + slot * 2,
                value,
            )

    anchor_indices = (2, 0, 2)
    for index, anchor_index in enumerate(anchor_indices):
        record = bytearray(64)
        struct.pack_into(">III", record, 4, 0x80150000 + index * 0x100, 0,
                         0x80160000 + index * 0x100)
        struct.pack_into(">hh", record, 0x10, 10 + index, -20 - index)
        struct.pack_into(">h", record, 0x20, anchor_index)
        put_logical(memory, segment_base + index * 64, record)

    for index, (x, z) in enumerate(((100, 200), (300, 400), (-500, 600))):
        record = bytearray(16)
        struct.pack_into(">hh", record, 0, x, z)
        record[4:] = bytes([index + 1]) * 12
        put_logical(memory, anchor_base + index * 16, record)

    # The extractor deliberately assumes one 16-byte waypoint per PVS row.
    for index in range(row_count):
        record = struct.pack(
            ">ffhHI", 1.25 + index, -2.5 - index, 30 + index, 0x1200 + index,
            0xABC00000 + index
        )
        put_logical(memory, pvs_end + index * 16, record)

    return bytes(memory)


class TrackLabFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._temporary = tempfile.TemporaryDirectory()
        cls.directory = Path(cls._temporary.name)
        cls.raw_bytes = make_rdram()
        cls.raw_path = cls.directory / "race.bin"
        cls.raw_path.write_bytes(cls.raw_bytes)
        cls.state_path = cls.directory / "race.lstate"
        cls.state_path.write_bytes(
            struct.pack("<8s6I", b"LMBOSTAT", 1, tl.RDRAM_SIZE, 8, 0, 0, 0)
            + cls.raw_bytes
        )
        cls.document = tl.extract_document(cls.raw_path)

    @classmethod
    def tearDownClass(cls) -> None:
        cls._temporary.cleanup()

    def fresh_document(self):
        return copy.deepcopy(self.document)


class TestExtraction(TrackLabFixture):
    def test_extracts_raw_word_swapped_snapshot_and_preserves_holes(self):
        document = self.document
        self.assertEqual(document["format"], "al-track-document")
        self.assertEqual(document["version"], 1)
        self.assertEqual(
            document["target"],
            {
                "game_id": "lamborghini.us",
                "rom_xxh3_64": "525201d7279f34e3",
                "circuit": 2,
            },
        )
        visibility = document["visibility"]
        self.assertEqual(visibility["row_count"], 3)
        self.assertEqual(visibility["slots_per_row"], 10)
        self.assertEqual(
            visibility["base_rows"][0],
            [0, None, 2, None, None, 1, None, None, None, None],
        )
        self.assertEqual(visibility["rows"], visibility["base_rows"])
        self.assertEqual(visibility["raw_base_rows"][0][3:5], [-2, -32768])
        self.assertEqual(
            visibility["base_fnv1a64"],
            f"{independent_fnv(visibility['raw_base_rows']):016x}",
        )

    def test_exports_inspection_only_records_and_assumption_warning(self):
        self.assertEqual([a["index"] for a in self.document["anchors"]], [0, 2])
        segment = self.document["segments"][0]
        self.assertEqual(len(segment["raw_be_hex"]), 128)
        self.assertEqual(segment["decoded"]["road_dl"], "0x80150000")
        self.assertIsNone(segment["decoded"]["wall_dl"])
        self.assertEqual(
            segment["decoded"]["cull_anchor"],
            {
                "anchor_index": 2,
                "segment_offset_x": 10,
                "segment_offset_z": -20,
                "world_x": -490,
                "world_z": 580,
            },
        )
        waypoints = self.document["waypoints"]
        self.assertIn("Assumption:", waypoints["assumption_warning"])
        self.assertEqual(len(waypoints["records"]), 3)
        self.assertEqual(
            waypoints["records"][1]["decoded"]["progress_or_extent"], 31
        )

    def test_accepts_lmbostat_v1_header(self):
        document = tl.extract_document(self.state_path)
        self.assertEqual(document["provenance"]["snapshot_format"], "lmbostat-v1")
        self.assertEqual(document["provenance"]["savestate_state"], 8)
        self.assertEqual(
            document["provenance"]["rdram_sha256"],
            self.document["provenance"]["rdram_sha256"],
        )

    def test_accepts_lmbostat_reserved_words_for_forward_compatibility(self):
        path = self.directory / "reserved.lstate"
        path.write_bytes(
            struct.pack(
                "<8s6I",
                b"LMBOSTAT",
                1,
                tl.RDRAM_SIZE,
                8,
                0x11223344,
                0x55667788,
                0,
            )
            + self.raw_bytes
        )
        document = tl.extract_document(path)
        self.assertEqual(document["provenance"]["snapshot_format"], "lmbostat-v1")

    def test_canonical_json_is_stable(self):
        first = self.directory / "first.json"
        second = self.directory / "second.json"
        tl.write_document(first, self.document)
        tl.write_document(second, self.document)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertTrue(first.read_bytes().endswith(b"\n"))
        self.assertEqual(json.loads(first.read_text(encoding="utf-8")), self.document)


class TestMalformedSnapshot(TrackLabFixture):
    def test_rejects_wrong_snapshot_size(self):
        path = self.directory / "short.bin"
        path.write_bytes(b"not rdram")
        with self.assertRaisesRegex(tl.TrackLabError, "expected raw"):
            tl.extract_document(path)

    def test_rejects_out_of_range_context_pointer(self):
        broken = bytearray(self.raw_bytes)
        put_u32(broken, tl.ACTIVE_CONTEXT_PTR, 0x80800000)
        path = self.directory / "bad-context.bin"
        path.write_bytes(broken)
        with self.assertRaisesRegex(tl.TrackLabError, "active track context.*outside"):
            tl.extract_document(path)

    def test_rejects_non_row_aligned_pvs_extent(self):
        broken = bytearray(self.raw_bytes)
        context = 0x80010000
        put_u32(broken, context + 8, 0x8011003E)
        path = self.directory / "bad-pvs-size.bin"
        path.write_bytes(broken)
        with self.assertRaisesRegex(tl.TrackLabError, "not a whole number"):
            tl.extract_document(path)

    def test_rejects_out_of_range_pvs_index(self):
        broken = bytearray(self.raw_bytes)
        put_s16(broken, 0x80110000 + 2, 3)
        path = self.directory / "bad-index.bin"
        path.write_bytes(broken)
        with self.assertRaisesRegex(tl.TrackLabError, "references segment 3"):
            tl.extract_document(path)


class TestValidationAndDiff(TrackLabFixture):
    def test_valid_document_has_no_edits(self):
        validated = tl.validate_document(self.fresh_document())
        self.assertEqual(validated.edits, [])
        self.assertEqual(validated.base_hash, validated.patched_hash)
        self.assertEqual(tl.diff_document(self.fresh_document())["edit_count"], 0)

    def test_rejects_unknown_editable_capability(self):
        document = self.fresh_document()
        document["capabilities"]["editable"].append("geometry")
        with self.assertRaisesRegex(tl.TrackLabError, "unsupported label.*geometry"):
            tl.validate_document(document)

    def test_rejects_row_shape_and_index_errors(self):
        short = self.fresh_document()
        short["visibility"]["rows"][0].pop()
        with self.assertRaisesRegex(tl.TrackLabError, "has 9 slots"):
            tl.validate_document(short)

        bad_index = self.fresh_document()
        bad_index["visibility"]["rows"][1][2] = 3
        with self.assertRaisesRegex(tl.TrackLabError, "null or a segment index"):
            tl.validate_document(bad_index)

    def test_detects_baseline_hash_mismatch(self):
        document = self.fresh_document()
        document["visibility"]["raw_base_rows"][0][1] = -7
        with self.assertRaisesRegex(tl.TrackLabError, "baseline hash mismatch"):
            tl.validate_document(document)

    def test_diff_is_sparse_and_row_major(self):
        document = self.fresh_document()
        document["visibility"]["rows"][1][5] = None
        document["visibility"]["rows"][0][1] = 2
        result = tl.diff_document(document)
        self.assertEqual(result["edit_count"], 2)
        self.assertEqual(
            result["edits"],
            [
                {
                    "row": 0,
                    "slot": 1,
                    "before": None,
                    "after": 2,
                    "expected_raw": -1,
                    "replacement_raw": 2,
                },
                {
                    "row": 1,
                    "slot": 5,
                    "before": 2,
                    "after": None,
                    "expected_raw": 2,
                    "replacement_raw": -1,
                },
            ],
        )


class TestCompile(TrackLabFixture):
    def test_compile_accepts_document_without_inspection_metadata(self):
        document = self.fresh_document()
        for key in ("provenance", "capabilities", "segments", "anchors", "waypoints"):
            document.pop(key)

        self.assertEqual(
            tl.compile_document(document),
            tl.compile_document(self.fresh_document()),
        )
        self.assertEqual(tl.diff_document(document)["edit_count"], 0)

    def test_noop_is_exact_64_byte_patch_and_preserves_negative_holes(self):
        document = self.fresh_document()
        patch = tl.compile_document(document)
        base = independent_fnv(document["visibility"]["raw_base_rows"])
        expected = struct.pack(
            "<8sHHIQIBBHIIQQQ",
            b"ALTRKPV1",
            1,
            64,
            64,
            0x525201D7279F34E3,
            1,
            2,
            10,
            3,
            0,
            0,
            base,
            base,
            0,
        )
        self.assertEqual(patch, expected)

    def test_compile_is_deterministic_and_binary_exact(self):
        document = self.fresh_document()
        document["visibility"]["rows"][1][5] = None
        document["visibility"]["rows"][0][3] = 1  # expected raw is unusual hole -2
        document["visibility"]["rows"][0][2] = None

        first = tl.compile_document(document)
        second = tl.compile_document(copy.deepcopy(document))
        self.assertEqual(first, second)

        raw_base = document["visibility"]["raw_base_rows"]
        patched = copy.deepcopy(raw_base)
        patched[0][2] = -1
        patched[0][3] = 1
        patched[1][5] = -1
        payload = b"".join(
            (
                struct.pack("<HBBhh", 0, 2, 0, 2, -1),
                struct.pack("<HBBhh", 0, 3, 0, -2, 1),
                struct.pack("<HBBhh", 1, 5, 0, 2, -1),
            )
        )
        expected_header = struct.pack(
            "<8sHHIQIBBHIIQQQ",
            b"ALTRKPV1",
            1,
            64,
            64 + len(payload),
            0x525201D7279F34E3,
            1,
            2,
            10,
            3,
            3,
            len(payload),
            independent_fnv(raw_base),
            independent_fnv(patched),
            0,
        )
        self.assertEqual(first, expected_header + payload)


class TestConsoleOutput(unittest.TestCase):
    def test_unicode_paths_remain_reportable_on_legacy_windows_code_pages(self):
        stdout = io.TextIOWrapper(io.BytesIO(), encoding="cp1252", errors="strict")
        stderr = io.TextIOWrapper(io.BytesIO(), encoding="cp1252", errors="strict")
        previous_stdout = tl.sys.stdout
        previous_stderr = tl.sys.stderr
        try:
            tl.sys.stdout = stdout
            tl.sys.stderr = stderr
            tl._make_console_output_total()
            stdout.write("轨道.altrk")
            stderr.write("轨道.altrk")
            self.assertEqual(stdout.errors, "backslashreplace")
            self.assertEqual(stderr.errors, "backslashreplace")
        finally:
            tl.sys.stdout = previous_stdout
            tl.sys.stderr = previous_stderr
            stdout.close()
            stderr.close()


if __name__ == "__main__":
    unittest.main()
