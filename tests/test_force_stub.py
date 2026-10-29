"""force_stub.txt is a checked-in record of why each function is still stubbed.

The file is a generation input (scripts/gen_syms_toml.py, FORCE_STUB_FILE). Each line is
`name disposition reason`. Structure and reconciliation checks always run. With the USA
ROM present, every recorded reason is recomputed from the declared symbol ranges: a
`pending` entry must still branch outside its own range, and the reason must name the
lowest target it reaches and the symbol that covers it; an `overlap` entry must have no
static branch leaving the range while its whole range sits inside a symbol emitted real.
"""
import functools
import importlib.util
from pathlib import Path
import re
import struct
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUB_FILE = ROOT / "force_stub.txt"
ROM = ROOT / "Automobili Lamborghini (USA).z64"
DISPOSITIONS = {"pending", "overlap"}
TARGET = re.compile(
    r"target (0x[0-9a-fA-F]+) is (inside|the start of) (\w+)(?: \(\+(\d+) more\))?$"
)
INSIDE = re.compile(r"range is inside (\w+)")
# Opcodes with a PC-relative branch target. jal (3) is a call, and jr/jalr have no
# static target, so neither is a range violation.
BRANCH_OPCODES = (1, 4, 5, 6, 7, 20, 21, 22, 23)  # REGIMM, beq/bne/blez/bgtz, *-likely
COP1 = 17
COP1_BC = 8  # COP1 rs field of bc1f/bc1t/bc1fl/bc1tl
JUMP = 2


@functools.lru_cache(maxsize=None)
def load_table():
    """(section, funcs, config), read on first use so import stays cheap and contained."""
    syms = tomllib.loads((ROOT / "lamborghini.syms.toml").read_text(encoding="utf-8"))
    config = tomllib.loads((ROOT / "lamborghini.us.toml").read_text(encoding="utf-8"))
    section = syms["section"][0]
    funcs = {f["name"]: (f["vram"], f["size"]) for f in section["functions"]}
    return section, funcs, config


@functools.lru_cache(maxsize=None)
def load_generator():
    path = ROOT / "scripts" / "gen_syms_toml.py"
    spec = importlib.util.spec_from_file_location("gen_syms_toml", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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


def branch_target(instruction, at):
    """Static branch or jump target of one instruction, or None."""
    opcode = instruction >> 26
    rs = (instruction >> 21) & 0x1F
    immediate = instruction & 0xFFFF
    if immediate & 0x8000:
        immediate -= 0x10000
    if opcode in BRANCH_OPCODES or (opcode == COP1 and rs == COP1_BC):
        return at + 4 + (immediate << 2)
    if opcode == JUMP:
        return ((instruction & 0x03FFFFFF) << 2) | ((at + 4) & 0xF0000000)
    return None


def outside_targets(read_word, vram, size):
    """Every distinct target of a branch or jump that leaves [vram, vram + size)."""
    out = set()
    for offset in range(0, size, 4):
        at = vram + offset
        target = branch_target(read_word(at), at)
        if target is not None and not vram <= target < vram + size:
            out.add(target)
    return out


def rom_reader(rom, section):
    def read_word(vram):
        offset = vram - section["vram"] + section["rom"]
        return struct.unpack(">I", rom[offset:offset + 4])[0]
    return read_word


def covering_symbols(funcs, address):
    """(name, vram, size) of every declared symbol whose range holds address."""
    return [(name, vram, size) for name, (vram, size) in funcs.items()
            if vram <= address < vram + size]


class DecoderTests(unittest.TestCase):
    """The scanner must see every static branch form; a blind decoder reports no targets."""

    def target(self, instruction, at=0x80010000):
        return branch_target(instruction, at)

    def test_relative_branches(self):
        at = 0x80010000
        for opcode in BRANCH_OPCODES:
            with self.subTest(opcode=opcode):
                self.assertEqual(self.target((opcode << 26) | 0x10, at), at + 4 + 0x10 * 4)

    def test_backward_branch_likely(self):
        at = 0x80010100
        instruction = (21 << 26) | (0xFFF0 & 0xFFFF)  # bnel, -16 words
        self.assertEqual(self.target(instruction, at), at + 4 - 0x40)

    def test_cop1_branches_but_not_cop1_moves(self):
        at = 0x80010200
        bc1t = (COP1 << 26) | (COP1_BC << 21) | 8
        self.assertEqual(self.target(bc1t, at), at + 4 + 8 * 4)
        mfc1 = (COP1 << 26) | (0 << 21) | 8  # rs=0 is mfc1, a move, not a branch
        self.assertIsNone(self.target(mfc1, at))

    def test_jump_and_calls(self):
        at = 0x80010300
        self.assertEqual(self.target((JUMP << 26) | 0x10, at), 0x80000040)
        self.assertIsNone(self.target((3 << 26) | 0x10, at))  # jal is a call
        self.assertIsNone(self.target(0x03E00008, at))  # jr $ra


class ForceStubStructureTests(unittest.TestCase):
    def test_every_entry_has_a_disposition_and_reason(self):
        for name, disposition, reason in entries():
            with self.subTest(entry=name):
                self.assertIn(disposition, DISPOSITIONS, name)
                self.assertTrue(reason.strip(), f"{name} has no reason")

    def test_entries_are_unique_and_named_in_the_symbol_table(self):
        _, funcs, _ = load_table()
        names = [name for name, _, _ in entries()]
        self.assertEqual(len(names), len(set(names)), "duplicate entry")
        for name in names:
            with self.subTest(entry=name):
                self.assertIn(name, funcs, "no such symbol; the generator drops it silently")

    def test_generated_config_applies_every_entry(self):
        _, _, config = load_table()
        names = {name for name, _, _ in entries()}
        stubs = set(config["patches"]["stubs"])
        ignored = set(config["patches"]["ignored"])
        self.assertEqual(names - stubs, set(), "entry is not stubbed by the config")
        self.assertEqual(names & ignored, set(), "entry is ignored instead of stubbed")

    def test_generator_reads_exactly_these_names(self):
        generator = load_generator()
        self.assertEqual(generator.parse_force_stub_file(STUB_FILE),
                         [name for name, _, _ in entries()])


@unittest.skipUnless(ROM.exists(), "USA ROM not present")
class ForceStubMeasurementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.section, cls.funcs, _ = load_table()
        read_word = rom_reader(ROM.read_bytes(), cls.section)
        cls.targets = {name: outside_targets(read_word, *cls.funcs[name])
                       for name, _, _ in entries()}

    def references(self, name, disposition, reason):
        """Assert the reason describes the measured targets. Returns the named symbol."""
        measured = self.targets[name]
        if disposition == "pending":
            match = TARGET.search(reason)
            self.assertIsNotNone(match, f"{name} does not name a target: {reason}")
            target, where, symbol = int(match.group(1), 16), match.group(2), match.group(3)
            self.assertIn(target, measured, f"{target:#x} is not an out-of-range target")
            self.assertEqual(target, min(measured), f"{name} should name its lowest target")
            self.assertIn(symbol, self.funcs, f"{symbol} is not a declared symbol")
            symbol_vram, symbol_size = self.funcs[symbol]
            if where == "the start of":
                self.assertEqual(target, symbol_vram, f"{target:#x} does not start {symbol}")
            else:
                self.assertTrue(symbol_vram <= target < symbol_vram + symbol_size,
                                f"{target:#x} is not inside {symbol}")
            self.assertEqual(1 + int(match.group(4) or 0), len(measured),
                             f"{name} miscounts its targets")
            return symbol
        match = INSIDE.search(reason)
        self.assertIsNotNone(match, f"{name} does not name the symbol containing it")
        return match.group(1)

    def test_pending_reasons_match_the_rom(self):
        for name, disposition, reason in entries():
            if disposition != "pending":
                continue
            with self.subTest(entry=name):
                self.assertTrue(self.targets[name],
                                f"{name} has no out-of-range target but is pending")
                symbol = self.references(name, disposition, reason)
                self.assertNotEqual(symbol, name, f"{name} cannot branch into itself")

    def test_overlap_entries_are_covered_by_the_symbol_they_name(self):
        for name, disposition, reason in entries():
            if disposition != "overlap":
                continue
            with self.subTest(entry=name):
                vram, size = self.funcs[name]
                self.assertEqual(self.targets[name], set(),
                                 "a branch leaves the range; this is a pending split")
                symbol = self.references(name, disposition, reason)
                self.assertNotEqual(symbol, name)
                owner_vram, owner_size = self.funcs[symbol]
                self.assertTrue(owner_vram <= vram and vram + size <= owner_vram + owner_size,
                                f"{name} is not wholly inside {symbol}")

    def test_every_target_lands_in_a_declared_symbol(self):
        for name, measured in self.targets.items():
            for target in measured:
                with self.subTest(entry=name, target=hex(target)):
                    self.assertTrue(covering_symbols(self.funcs, target),
                                    f"{target:#x} from {name} is in no declared symbol")


if __name__ == "__main__":
    unittest.main()
