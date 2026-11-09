"""USA instruction witnesses for rain producers; no renderer/parity claim."""
from pathlib import Path
import hashlib
import struct
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "Automobili Lamborghini (USA).z64"


@unittest.skipUnless(ROM.exists(), "verified USA cartridge required")
class RainHookTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = ROM.read_bytes()
        if hashlib.sha256(cls.rom).hexdigest() != "cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c":
            raise AssertionError("USA cartridge identity mismatch")

    def test_road_insertions_follow_pointer_stores_and_keep_record_register(self):
        hooks = tomllib.loads((ROOT / "lamborghini.us.toml").read_text())["patches"]["hook"]
        for load, store, target, record, command in [
            (0x8000CF24, 0x8000CF28, 11, 10, 14),
            (0x8000D7B8, 0x8000D7BC, 10, 24, 12),
        ]:
            with self.subTest(store=hex(store)):
                word = lambda pc: struct.unpack_from(">I", self.rom, pc - 0x7FFFF400)[0]
                self.assertEqual(word(load), (35 << 26) | (record << 21) | (target << 16) | 4)
                self.assertEqual(word(store), (43 << 26) | (command << 21) | (target << 16) | 4)
                hook = next(h for h in hooks if h.get("before_vram") == store + 4)
                self.assertEqual(hook["func"], "func_8000A6C0")
                self.assertIn(f"ctx->r{record}", hook["text"])
                self.assertIn("lambo_rain_road", hook["text"])


class RainCacheHookTests(unittest.TestCase):
    def test_track_load_invalidates_cached_geometry(self):
        hooks = tomllib.loads((ROOT / "lamborghini.us.toml").read_text())["patches"]["hook"]
        hook = next(h for h in hooks if h.get("before_vram") == 0x80006094)
        text = hook["text"]
        self.assertIn("lambo_rain_invalidate();", text)
        self.assertLess(text.index("lambo_track_patch_on_track_loaded(rdram)"),
                        text.index("lambo_rain_invalidate();"))


if __name__ == "__main__":
    unittest.main()
