"""Pin the USA ROM emission sequences consumed by the blue HUD strip hook."""
from pathlib import Path
import struct
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "Automobili Lamborghini (USA).z64"
DISPATCHER = "func_80050860"
HOOK_NAME = "lambo_hud_remove_blue_bar"
# Decoded I-format instructions: mnemonic, rt, rs, unsigned immediate.
# These pin allocation/cursor updates, the complete G_DL write, and t3 (r11)
# remaining the command pointer at each hook; neither hook is a delay slot.
EMITTERS = {
    0x8004FE78: (0x8004FE44, (
        ("lui", 24, 0, 0x800A),
        ("lw", 24, 24, 0x39CC),
        ("lui", 1, 0, 0x800A),
        ("addiu", 25, 24, 8),
        ("sw", 25, 1, 0x39CC),
        ("sw", 24, 29, 0x12C),
        ("lw", 9, 29, 0x12C),
        ("lui", 8, 0, 0x0600),
        ("sw", 8, 9, 0),
        ("lw", 11, 29, 0x12C),
        ("lui", 10, 0, 0x8016),
        ("addiu", 10, 10, 0x7170),
        ("sw", 10, 11, 4),
        ("lui", 12, 0, 0x800A),
    )),
    0x80051144: (0x80051110, (
        ("lui", 8, 0, 0x800A),
        ("lw", 8, 8, 0x39CC),
        ("lui", 1, 0, 0x800A),
        ("addiu", 15, 8, 8),
        ("sw", 15, 1, 0x39CC),
        ("sw", 8, 29, 0x10C),
        ("lw", 10, 29, 0x10C),
        ("lui", 9, 0, 0x0600),
        ("sw", 9, 10, 0),
        ("lw", 11, 29, 0x10C),
        ("lui", 14, 0, 0x8016),
        ("addiu", 14, 14, 0x7170),
        ("sw", 14, 11, 4),
        ("lui", 25, 0, 0x800D),
    )),
}


class HudBlueBarRomTests(unittest.TestCase):
    def test_hook_locations(self):
        config = tomllib.loads((ROOT / "lamborghini.us.toml").read_text(encoding="utf-8"))
        hooks = [hook for hook in config["patches"]["hook"] if HOOK_NAME in hook["text"]]
        self.assertCountEqual(
            [(hook["func"], hook["before_vram"]) for hook in hooks],
            [(DISPATCHER, address) for address in EMITTERS],
        )

    @unittest.skipUnless(ROM.is_file(), "USA ROM required for HUD instruction pins")
    def test_rom_emission_sequences(self):
        symbols = tomllib.loads((ROOT / "lamborghini.syms.toml").read_text(encoding="utf-8"))
        matches = [(section, function) for section in symbols["section"]
                   for function in section["functions"] if function["name"] == DISPATCHER]
        self.assertEqual(len(matches), 1, "HUD dispatcher symbol must be unique")
        section, function = matches[0]
        self.assertEqual(function["vram"], 0x8004FC60)
        rom = ROM.read_bytes()
        opcodes = {0x0F: "lui", 0x23: "lw", 0x2B: "sw", 0x09: "addiu"}
        for hook, (start, instructions) in EMITTERS.items():
            self.assertEqual(start + 4 * (len(instructions) - 1), hook)
            self.assertGreaterEqual(start, function["vram"])
            self.assertLess(hook, function["vram"] + function["size"])
            for index, expected in enumerate(instructions):
                address = start + 4 * index
                with self.subTest(vram=f"0x{address:08X}"):
                    word, = struct.unpack_from(">I", rom, section["rom"] + address - section["vram"])
                    actual = (opcodes.get(word >> 26, word >> 26),
                              (word >> 16) & 31, (word >> 21) & 31, word & 0xFFFF)
                    self.assertEqual(actual, expected, "HUD hook instruction assumption changed")


if __name__ == "__main__":
    unittest.main()
