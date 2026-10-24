"""Pin the retail USA title/audio transitions used by the reset bridge."""
from pathlib import Path
import struct
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "Automobili Lamborghini (USA).z64"


class ResetHookTests(unittest.TestCase):
    def test_reset_runs_before_the_dispatcher_prologue(self):
        config = tomllib.loads((ROOT / "lamborghini.us.toml").read_text(encoding="utf-8"))
        hook = next(hook for hook in config["patches"]["hook"]
                    if hook["func"] == "func_800028D0"
                    and hook["before_vram"] == 0x80001CD0)
        self.assertIn("if (lambo_reset_tick(rdram, ctx)) return;", hook["text"])
        self.assertLess(hook["text"].index("lambo_reset_tick(rdram, ctx)"),
                        hook["text"].index("lambo_warp_tick(rdram, ctx)"))


@unittest.skipUnless(ROM.is_file(), "requires the supported USA ROM")
class ResetRomTests(unittest.TestCase):
    def test_retail_title_initializer_and_audio_transition(self):
        rom = ROM.read_bytes()
        def jal(target):
            return 0x0C000000 | ((target >> 2) & 0x03FFFFFF)

        # Dispatcher case 3 stops audio, initializes title assets, clears the
        # attract cursor (0x800CE794), advances state to 4, then updates the title.
        expected = {
            0x80001D50: jal(0x80066AB4),
            0x80001D58: jal(0x80037B94),
            0x80001D64: 0xA420E794,
            0x80001D68: 0x24080004,
            0x80001D70: 0xA428E6AC,
            0x80001D74: jal(0x80037C18),
            # Retail race-to-menu helper uses the same stop/drain pair.
            0x80008298: jal(0x80066AB4),
            0x800082A0: jal(0x800082CC),
        }
        for address, word in expected.items():
            with self.subTest(address=hex(address)):
                offset = address - 0x80000400 + 0x1000
                self.assertEqual(struct.unpack_from(">I", rom, offset)[0], word)


if __name__ == "__main__":
    unittest.main()
