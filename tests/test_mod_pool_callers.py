"""USA direct-call evidence for the racer-extension pool-reset boundary.

This bounds the stock direct-call paths, not arbitrary mod/native/indirect
calls. The measured menu-to-race trace is recorded in docs/mod-api.md.
Named function ranges can overlap, so a call may belong to multiple functions.
This over-approximates reachability, which is conservative for the no-path claim.
"""
from functools import lru_cache
import hashlib
from pathlib import Path
import struct
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "Automobili Lamborghini (USA).z64"
CALLERS = {0x800062D8: "boot_pad_apply_calibration",
           0x80037E1C: "func_800389A4", 0x8003C1CC: "func_8003CD84"}
DISPATCH = {5: "func_80038BD8", 6: "func_80038D6C", 7: "boot_pad_apply_calibration",
            8: "func_800030F8", 9: "func_80038C88", 10: "func_80038D6C",
            11: "func_80038CD4", 12: "func_80038D6C", 13: "func_8004DCC0",
            14: "func_80038D6C", 15: "func_80038D20", 16: "func_80038D6C"}


@lru_cache
def evidence():
    rom = ROM.read_bytes()
    if hashlib.sha256(rom).hexdigest() != "cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c":
        raise AssertionError("Pool-reset evidence requires the verified USA ROM")
    with (ROOT / "lamborghini.syms.toml").open("rb") as source:
        functions = {f["name"]: f for s in tomllib.load(source)["section"] for f in s["functions"]}
    with (ROOT / "lamborghini.us.toml").open("rb") as source:
        patches = tomllib.load(source)["patches"]
    return rom, functions, set(patches["stubs"]) | set(patches["ignored"])


def word(rom, address):
    return struct.unpack_from(">I", rom, address - 0x80000000 + 0xC00)[0]


def calls(rom, start, end):
    for pc in range(start, end, 4):
        instruction = word(rom, pc)
        if instruction >> 26 == 3:
            yield pc, 0x80000000 | ((instruction & 0x3FFFFFF) << 2)


@unittest.skipUnless(ROM.is_file(), "USA ROM needed for stock pool-reset evidence")
class PoolResetCallersTests(unittest.TestCase):
    def test_stock_reset_calls_match_the_audited_inventory(self):
        rom, functions, _ = evidence()
        sites = {pc for pc, target in calls(rom, 0x80000400, 0x80000400 + 0x86690)
                 if target == functions["func_80011710"]["vram"]}
        self.assertEqual(sites, set(CALLERS))
        for pc, name in CALLERS.items():
            function = functions[name]
            self.assertTrue(function["vram"] <= pc < function["vram"] + function["size"])

    def test_dispatch_states_and_direct_paths_exclude_the_race_updater(self):
        rom, functions, disabled = evidence()
        names = {function["vram"]: name for name, function in functions.items()}
        edges = {name: {names[target] for _, target in calls(
            rom, function["vram"], function["vram"] + function["size"]) if target in names}
                 for name, function in functions.items() if name not in disabled}
        entries = [word(rom, 0x8008D610 + index * 4) for index in range(16)]
        for state, name in DISPATCH.items():
            entry = entries[state - 1]
            end = min((address for address in entries if address > entry), default=0x80002150)
            self.assertIn(functions[name]["vram"], {target for _, target in calls(rom, entry, end)})
        reached = {name: set() for name in CALLERS.values()}
        for state, name in DISPATCH.items():
            pending, seen = [name], set()
            while pending:
                current = pending.pop()
                if current not in seen:
                    seen.add(current)
                    pending.extend(edges.get(current, ()))
            for caller in reached:
                if caller in seen:
                    reached[caller].add(state)
        self.assertEqual(reached, {
            "boot_pad_apply_calibration": {7},
            "func_800389A4": {5, 9, 11, 15},
            "func_8003CD84": {5, 6, 9, 10, 11, 12, 13, 14, 15, 16},
        })


if __name__ == "__main__":
    unittest.main()
