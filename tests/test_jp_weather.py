"""Static weather evidence must reject stale reports and malformed witnesses."""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import audit_jp_weather as audit


class WeatherAuditTests(unittest.TestCase):
    def test_read_rejects_negative_and_truncated_ranges(self):
        for offset, size in ((-1, 2), (0, -1), (3, 2), (5, 0)):
            with self.subTest(offset=offset, size=size), self.assertRaises(ValueError):
                audit.read(b"1234", offset, size)
        self.assertEqual(audit.read(b"1234", 2, 2), b"34")

    def test_identity_rejects_wrong_region_and_truncated_cartridges(self):
        for region in audit.ROMS:
            with self.subTest(region=region), self.assertRaisesRegex(ValueError, "identity"):
                audit.verify_rom(b"not a cartridge", region)

    def test_instruction_check_validates_register_width_and_signed_offset(self):
        # Synthetic LH t0,-2(t1), deliberately not copied cartridge bytes.
        word = (33 << 26) | (9 << 21) | (8 << 16) | 0xFFFE
        data = bytes(0x1000) + struct.pack(">I", word)
        audit.expect_i(data, 0x80000400, 33, 9, 8, -2)
        for op, base, target, immediate in ((35, 9, 8, -2), (33, 8, 8, -2),
                                            (33, 9, 9, -2), (33, 9, 8, 2)):
            with self.subTest(op=op, base=base, target=target, immediate=immediate):
                with self.assertRaisesRegex(ValueError, "semantics changed"):
                    audit.expect_i(data, 0x80000400, op, base, target, immediate)

    def test_shift_check_distinguishes_arithmetic_from_logical(self):
        data = bytes(0x1000) + struct.pack(">I", (8 << 16) | (9 << 11) | (1 << 6) | 3)
        audit.expect_r(data, 0x80000400, 0, 8, 9, 1, 3)
        with self.assertRaises(ValueError):
            audit.expect_r(data, 0x80000400, 0, 8, 9, 1, 2)

    def test_unknown_code_mapping_is_rejected(self):
        with self.assertRaises(ValueError):
            audit.offset(0x800C503C)
        self.assertEqual(audit.offset(0x801F9800), 0xFA1F0)

    def test_label_pointer_and_text_are_checked(self):
        data = bytearray(0x151840)
        for i, label in enumerate((b"WEATHER", b"DRY", b"WET", b"NIGHT")):
            pointer = 0x80240000 + i * 16
            struct.pack_into(">I", data, 0x138D74 + i * 4, pointer)
            start = pointer - 0x800FF610
            data[start:start + len(label)] = label
        self.assertEqual([row["text"] for row in audit.labels(data)],
                         ["WEATHER", "DRY", "WET", "NIGHT"])
        bad_text = bytearray(data)
        bad_text[0x80240000 - 0x800FF610] = ord("X")
        with self.assertRaisesRegex(ValueError, "Unexpected weather label"):
            audit.labels(bad_text)
        struct.pack_into(">I", data, 0x138D74, 0)
        with self.assertRaisesRegex(ValueError, "pointer outside"):
            audit.labels(data)

    def test_menu_record_cannot_silently_change_field(self):
        data = bytearray(0x151840)
        for address, callback in ((0x80238948, 0x80201220),
                                  (0x80238A48, 0x80201220),
                                  (0x80239448, 0x802011A4)):
            pos = address - 0x800FF610
            struct.pack_into(">II", data, pos + 4, callback, 0x800C4F30)
            struct.pack_into(">H", data, pos + 24, 0x5A)
            struct.pack_into(">I", data, pos + 40, 0x800C4F30)
            struct.pack_into(">3H", data, pos + 58, 0x5B, 0x5C, 0x5D)
        audit.menu_records(data)
        struct.pack_into(">I", data, 0x80238948 - 0x800FF610 + 40, 0x800C4F10)
        with self.assertRaisesRegex(ValueError, "menu record changed"):
            audit.menu_records(data)

    def test_checked_in_report_is_current_and_explicitly_static(self):
        report = json.loads(audit.OUTPUT.read_text(encoding="utf-8"))
        self.assertEqual(report["input_sha256"], audit.input_digest())
        self.assertIn("static", report["evidence_type"])
        self.assertNotIn("instruction_words", report)
        self.assertEqual(report["menu"]["element_type"], "big-endian signed halfword")

    def test_check_inputs_fails_for_stale_or_missing_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            with mock.patch.object(audit, "OUTPUT", path), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(audit.main(["--check-inputs"]), 1)
                path.write_text('{"input_sha256":"stale"}', encoding="utf-8")
                self.assertEqual(audit.main(["--check-inputs"]), 1)


@unittest.skipUnless(all((ROOT / value[0]).exists() for value in audit.ROMS.values()),
                     "both verified cartridges required")
class WeatherCartridgeTests(unittest.TestCase):
    def test_complete_report_matches_cartridges(self):
        self.assertEqual(audit.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
