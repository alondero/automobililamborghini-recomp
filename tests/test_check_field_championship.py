"""Input validation for the ROM-backed championship progression check."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import check_field_championship as championship


class ChampionshipSnapshotTests(unittest.TestCase):
    @staticmethod
    def snapshot(mode):
        blob = bytearray(32 + 0x800000 + 0x24000)
        struct.pack_into('<h', blob, championship.offset(championship.RACE_MODE_ADDR), mode)
        return blob

    def test_rejects_single_race_snapshot_with_actionable_command(self):
        with self.assertRaisesRegex(ValueError, r'race mode 2.*--mode 3'):
            championship.require_championship_mode(self.snapshot(2))

    def test_accepts_championship_snapshot(self):
        championship.require_championship_mode(self.snapshot(3))


if __name__ == '__main__':
    unittest.main()
