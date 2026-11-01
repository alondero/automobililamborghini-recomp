"""Check the package's USA bridge against verified cartridge instructions.

These are address/lifecycle witnesses, not runtime hook or AI behavior tests.
The native JAL return address is deliberately not a guest-context contract:
N64Recomp's base C call does not assign r31 for this call site.
"""
from pathlib import Path
import re
import struct
import sys
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import audit_jp_weather as audit
from gen_mod_patch_guards import protected_addresses


def load_toml(name):
    return tomllib.loads((ROOT / name).read_text(encoding="utf-8"))


def functions():
    return {f["name"]: f for section in load_toml("lamborghini.syms.toml")["section"]
            for f in section.get("functions", [])}


def word(rom, pc):
    return struct.unpack_from(">I", rom, pc - 0x7FFFF400)[0]


class WeatherPackageContractTests(unittest.TestCase):
    def test_hook_is_the_unprotected_stock_function(self):
        source = (ROOT / "mods/jp_weather/weather.c").read_text(encoding="utf-8")
        hooks = re.findall(r'\.recomp_hook\.([A-Za-z_0-9]+)', source)
        self.assertEqual(hooks, ["func_80066F84"])
        function = functions()[hooks[0]]
        self.assertEqual((function["vram"], function["size"]), (0x80066384, 0x138))
        config = load_toml("lamborghini.us.toml")
        self.assertNotIn(function["vram"], protected_addresses(config, load_toml("lamborghini.syms.toml")))
        self.assertNotIn(hooks[0], config.get("patches", {}).get("ignored", []))

    def test_hook_does_not_assume_base_jal_updates_guest_ra(self):
        source = (ROOT / "mods/jp_weather/weather.c").read_text(encoding="utf-8")
        self.assertNotIn("__builtin_return_address", source,
                         "The original recompiled caller does not set guest r31")


@unittest.skipUnless(all((ROOT / value[0]).exists() for value in audit.ROMS.values()),
                     "both verified cartridges required")
class WeatherPackageCartridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.us = (ROOT / audit.ROMS["usa"][0]).read_bytes()
        cls.jp = (ROOT / audit.ROMS["japan"][0]).read_bytes()
        audit.verify_rom(cls.us, "usa")
        audit.verify_rom(cls.jp, "japan")

    def test_loader_final_audio_call_arguments_and_delay_slot(self):
        # These four instruction-defined arguments survive into the hook. The
        # ROM's computed RA is 0x800081E8, but base C omits the register write.
        for args in [(0x800081D0, 9, 0, 4, 1),
                     (0x800081D4, 15, 0, 5, 0x3F00),
                     (0x800081D8, 9, 0, 6, 0x4002),
                     (0x800081DC, 9, 0, 7, 0x40),
                     (0x800081E4, 43, 29, 0, 0x10)]:
            audit.expect_i(self.us, *args)
        call = word(self.us, 0x800081E0)
        self.assertEqual(call >> 26, 3)
        self.assertEqual(0x80000000 | ((call & 0x3FFFFFF) << 2), 0x80066384)
        loader = functions()["boot_pad_apply_calibration"]
        calls = [pc for pc in range(loader["vram"], loader["vram"] + loader["size"], 4)
                 if word(self.us, pc) == call]
        self.assertEqual(calls, [0x800081E0])
        # All five stock fog stores precede this call within the same loader.
        self.assertLess(loader["vram"], 0x80006930)
        self.assertLess(0x800069E0, calls[0])

    def test_loader_fog_addresses_widths_and_source_fields(self):
        for load_pc, load_op, base, value, source_offset, store_pc, store_op, dest in [
            (0x80006928, 33, 25, 15, 0x1E6, 0x80006930, 41, 0x39C0),
            (0x80006954, 33, 8, 12, 0x1E8, 0x8000695C, 41, 0x39C2),
            (0x80006980, 36, 25, 15, 0x1EA, 0x80006988, 40, 0x39C4),
            (0x800069AC, 36, 8, 12, 0x1EB, 0x800069B4, 40, 0x39C5),
            (0x800069D8, 36, 25, 15, 0x1EC, 0x800069E0, 40, 0x39C6),
        ]:
            with self.subTest(store_pc=hex(store_pc)):
                audit.expect_i(self.us, load_pc, load_op, base, value, source_offset)
                audit.expect_i(self.us, store_pc - 4, 15, 0, 1, 0x800A)
                audit.expect_i(self.us, store_pc, store_op, 1, value, dest)

    def test_ai_table_baselines_and_jp_weather_rows(self):
        self.assertEqual(struct.unpack_from(">2f", self.us, 0x8A044), (290.0, 240.0))
        # USA's first countdown is 40, not JP easy's 50. Restoration must save
        # the actual USA values; any difficulty correspondence is a mod choice.
        self.assertEqual(struct.unpack_from(">2h", self.us, 0x8A008), (40, 25))
        rows = struct.unpack_from(">12f", self.jp, 0x7E6F0)
        self.assertEqual(rows, (290, 290, 240, 240, 220, 220, 180, 180, 290, 290, 240, 240))
        self.assertEqual(struct.unpack_from(">4h", self.jp, 0x7E678), (50, 35, 25, 15))


if __name__ == "__main__":
    unittest.main()
