"""Pin the ordinary 2-player speed and place text to the right edge.

func_80050860's 2P section draws each half two ways. When the per-car halfword
at 0x800A5FBC (top) or 0x800A6040 (bottom) is nonzero, it calls the alternate
dial and that call is already bracketed. When the halfword is zero, it draws
the speed (func_80058614, x=0xEB) and the place (func_80058770, x=0x118) as
texrects, then the pinned RANK/LAP blocks. Those two calls were outside every
rect-align bracket, so on an expanded aspect the speed and place stay in the
4:3 column while RANK travels to the widescreen edge.
"""
from pathlib import Path
import struct
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "Automobili Lamborghini (USA).z64"
DISPATCHER = "func_80050860"
SPEED = 0x80057A14
PLACE = 0x80057B70

# pin address, reset address, and the ROM words from the speed-x load through
# the place call's delay slot. The speed-x addiu is the instruction before the
# pin. The reset is the next instruction after that delay slot, so the hook is
# not itself in a delay slot. Each entry is (address, opcode, rs, rt, extra).
HALVES = (
    (0x80050CC0, 0x80050CD8, (
        (0x80050CBC, 0x09, 0, 5, 0xEB),    # addiu a1, zero, speed x
        (0x80050CC0, 0x03, None, None, SPEED),
        (0x80050CC4, 0x09, 0, 6, 0x68),    # addiu a2, zero, speed y (delay)
        (0x80050CCC, 0x09, 0, 5, 0x118),   # addiu a1, zero, place x
        (0x80050CD0, 0x03, None, None, PLACE),
        (0x80050CD4, 0x09, 0, 6, 0x5E),    # addiu a2, zero, place y (delay)
    )),
    (0x80050F04, 0x80050F1C, (
        (0x80050F00, 0x09, 0, 5, 0xEB),
        (0x80050F04, 0x03, None, None, SPEED),
        (0x80050F08, 0x09, 0, 6, 0xDC),
        (0x80050F10, 0x09, 0, 5, 0x118),
        (0x80050F14, 0x03, None, None, PLACE),
        (0x80050F18, 0x09, 0, 6, 0xD2),
    )),
)


def _decode(word, address):
    op = word >> 26
    if op == 0x03:
        target = (address & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
        return op, None, None, target
    return op, (word >> 21) & 31, (word >> 16) & 31, word & 0xFFFF


def _control_targets(word, address):
    """Branch and jump destinations. Delay-slot jr/jalr have no embedded target."""
    op = word >> 26
    if op in (0x02, 0x03):
        return ((address & 0xF0000000) | ((word & 0x03FFFFFF) << 2),)
    if op in (0x01, 0x04, 0x05, 0x06, 0x07):
        imm = word & 0xFFFF
        if imm & 0x8000:
            imm -= 0x10000
        return (address + 4 + imm * 4,)
    # COP1 BC1F/BC1T. rs == 8 selects the branch form.
    if op == 0x11 and ((word >> 21) & 31) == 8:
        imm = word & 0xFFFF
        if imm & 0x8000:
            imm -= 0x10000
        return (address + 4 + imm * 4,)
    return ()


class Hud2pSpeedPinTests(unittest.TestCase):
    def test_text_readouts_are_right_pinned(self):
        config = tomllib.loads((ROOT / "lamborghini.us.toml").read_text(encoding="utf-8"))
        hooks = {(hook["func"], hook["before_vram"]): hook["text"]
                 for hook in config["patches"]["hook"]}
        for pin_at, reset_at, _words in HALVES:
            with self.subTest(pin=f"0x{pin_at:08X}"):
                self.assertEqual(hooks.get((DISPATCHER, pin_at)), "lambo_ws_pin_right(rdram);")
                self.assertEqual(hooks.get((DISPATCHER, reset_at)), "lambo_ws_pin_reset(rdram);")

    @unittest.skipUnless(ROM.is_file(), "USA ROM required for HUD instruction pins")
    def test_rom_call_sites(self):
        symbols = tomllib.loads((ROOT / "lamborghini.syms.toml").read_text(encoding="utf-8"))
        matches = [(section, function) for section in symbols["section"]
                   for function in section["functions"] if function["name"] == DISPATCHER]
        self.assertEqual(len(matches), 1)
        section, function = matches[0]
        rom = ROM.read_bytes()
        for pin_at, reset_at, words in HALVES:
            self.assertGreaterEqual(pin_at, function["vram"])
            self.assertLess(reset_at, function["vram"] + function["size"])
            # The reset must be the instruction after the place call's delay slot,
            # so the hook itself is not in a delay slot.
            self.assertEqual(words[-1][0] + 4, reset_at)
            for address, op, rs, rt, extra in words:
                with self.subTest(vram=f"0x{address:08X}"):
                    word, = struct.unpack_from(
                        ">I", rom, section["rom"] + address - section["vram"])
                    self.assertEqual(_decode(word, address), (op, rs, rt, extra))
        # Nonzero gate: top halfword 0x800A5FBC, bottom 0x800A6040. The branch
        # goes to the dial jal, which is after this bracket, so the text pin
        # and its reset stay paired.
        for gate_at, dial_at, pin_at, reset_at in (
            (0x80050C7C, 0x80050E0C, 0x80050CC0, 0x80050CD8),
            (0x80050EC0, 0x80051050, 0x80050F04, 0x80050F1C),
        ):
            word, = struct.unpack_from(">I", rom, section["rom"] + gate_at - section["vram"])
            self.assertEqual(_control_targets(word, gate_at), (dial_at,))
            self.assertLess(gate_at, pin_at)
            self.assertLess(reset_at, dial_at)
        # A branch onto the pin or the reset would run that hook on a path that
        # did not take the matching half of the pair. N64Recomp runs the hook
        # text on every incoming path.
        pinned = {address for pin_at, reset_at, _words in HALVES
                  for address in (pin_at, reset_at)}
        start = function["vram"]
        for address in range(start, start + function["size"], 4):
            word, = struct.unpack_from(">I", rom, section["rom"] + address - section["vram"])
            for target in _control_targets(word, address):
                self.assertNotIn(target, pinned, f"0x{address:08X} targets 0x{target:08X}")
