"""force_stub.txt is a checked-in record of why each function is still stubbed.

The file is a generation input (scripts/gen_syms_toml.py, FORCE_STUB_FILE). Each line is
`name disposition reason`. Structure and reconciliation checks always run. With the USA
ROM present, every recorded reason is recomputed from the declared symbol ranges: a
`pending` entry must still branch outside its own range into the symbol it names, and an
`overlap` entry must have no such target while its whole range sits inside the symbol it
names.
"""
from pathlib import Path
import re
import struct
import sys
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUB_FILE = ROOT / "force_stub.txt"
ROM = ROOT / "Automobili Lamborghini (USA).z64"
SYMS = tomllib.loads((ROOT / "lamborghini.syms.toml").read_text(encoding="utf-8"))
CONFIG = tomllib.loads((ROOT / "lamborghini.us.toml").read_text(encoding="utf-8"))
SECTION = SYMS["section"][0]
FUNCS = {f["name"]: (f["vram"], f["size"]) for f in SECTION["functions"]}
DISPOSITIONS = {"pending", "overlap"}
TARGET = re.compile(r"target (0x[0-9a-fA-F]+) is (?:inside|the start of) (\w+)")
INSIDE = re.compile(r"range is inside (\w+)")


def entries():
    """(name, disposition, reason) for every non-comment line, padded to three fields."""
    out = []
    for line in STUB_FILE.read_text(encoding="utf-8").splitlines():
        text = line.split("#", 1)[0].strip()
        if not text:
            continue
        fields = text.split(None, 2)
        out.append(tuple(fields) + ("",) * (3 - len(fields)))
    return out


def word(rom, vram):
    offset = vram - SECTION["vram"] + SECTION["rom"]
    return struct.unpack(">I", rom[offset:offset + 4])[0]


def outside_targets(rom, vram, size):
    """Branch and jump targets that leave [vram, vram + size); jal is a call."""
    out = set()
    for offset in range(0, size, 4):
        at = vram + offset
        instruction = word(rom, at)
        opcode, immediate = instruction >> 26, instruction & 0xFFFF
        if immediate & 0x8000:
            immediate -= 0x10000
        if opcode in (1, 4, 5, 6, 7):
            target = at + 4 + (immediate << 2)
        elif opcode == 2:
            target = ((instruction & 0x03FFFFFF) << 2) | ((at + 4) & 0xF0000000)
        else:
            continue
        if not vram <= target < vram + size:
            out.add(target)
    return out


class ForceStubFileTests(unittest.TestCase):
    def test_every_entry_has_a_disposition_and_reason(self):
        for name, disposition, reason in entries():
            with self.subTest(entry=name):
                self.assertIn(disposition, DISPOSITIONS, name)
                self.assertTrue(reason.strip(), f"{name} has no reason")

    def test_entries_are_unique_and_named_in_the_symbol_table(self):
        names = [name for name, _, _ in entries()]
        self.assertEqual(len(names), len(set(names)), "duplicate entry")
        for name in names:
            with self.subTest(entry=name):
                self.assertIn(name, FUNCS, "no such symbol; the generator drops it silently")

    def test_generated_config_applies_every_entry(self):
        names = {name for name, _, _ in entries()}
        stubs = set(CONFIG["patches"]["stubs"])
        ignored = set(CONFIG["patches"]["ignored"])
        self.assertEqual(names - stubs, set(), "entry is not stubbed by the config")
        self.assertEqual(names & ignored, set(), "entry is ignored instead of stubbed")

    def test_generator_reads_exactly_these_names(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        import gen_syms_toml  # noqa: E402

        self.assertEqual(gen_syms_toml.parse_force_stub_file(STUB_FILE),
                         [name for name, _, _ in entries()])


@unittest.skipUnless(ROM.exists(), "USA ROM not present")
class ForceStubMeasurementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = ROM.read_bytes()

    def test_pending_reasons_match_the_rom(self):
        for name, disposition, reason in entries():
            if disposition != "pending":
                continue
            with self.subTest(entry=name):
                vram, size = FUNCS[name]
                match = TARGET.search(reason)
                self.assertIsNotNone(match, f"{name} does not name a target: {reason}")
                target, owner = int(match.group(1), 16), match.group(2)
                self.assertIn(target, outside_targets(self.rom, vram, size))
                if owner == name:
                    self.fail(f"{name} cannot branch into itself")
                owner_vram, owner_size = FUNCS[owner]
                self.assertTrue(owner_vram <= target < owner_vram + owner_size,
                                f"{target:#x} is not inside {owner}")

    def test_overlap_entries_are_covered_by_the_symbol_they_name(self):
        for name, disposition, reason in entries():
            if disposition != "overlap":
                continue
            with self.subTest(entry=name):
                vram, size = FUNCS[name]
                self.assertEqual(outside_targets(self.rom, vram, size), set())
                match = INSIDE.search(reason)
                self.assertIsNotNone(match, f"{name} does not name its owner: {reason}")
                owner_vram, owner_size = FUNCS[match.group(1)]
                self.assertTrue(owner_vram <= vram and vram + size <= owner_vram + owner_size,
                                f"{name} is not wholly inside {match.group(1)}")


if __name__ == "__main__":
    unittest.main()
