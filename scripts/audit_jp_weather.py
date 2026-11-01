#!/usr/bin/env python3
"""Extract bounded static weather evidence for issue 340 from both cartridges.

This is not a playable mod, runtime measurement, or proof of AI vision. Output
contains interpreted values and locations, never ROM bytes or instruction words.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/evidence/jp-weather-static.json"
ROMS = {
    "usa": ("Automobili Lamborghini (USA).z64", 0x400000,
            "cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c"),
    "japan": ("Super Speed Race 64 (Japan).z64", 0x800000,
              "06c9c992014da5982e747117c690c1590a011c0c6756b0522df126366bf8b9a1"),
}


def input_digest():
    return hashlib.sha256(Path(__file__).read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def read(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"ROM range out of bounds at 0x{offset:X}, size {size}")
    return data[offset:offset + size]


def verify_rom(data, region):
    _, size, digest = ROMS[region]
    if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
        raise ValueError(f"Wrong {region} cartridge identity; see docs/japanese-rom.md")


def offset(pc):
    if 0x80000400 <= pc < 0x8007AF80:
        return pc - 0x7FFFF400
    if 0x801F9800 <= pc < 0x80227DB0:
        return pc - 0x800FF610
    raise ValueError(f"Unmapped code address 0x{pc:08X}")


def fields(data, pc):
    word, = struct.unpack(">I", read(data, offset(pc), 4))
    return word >> 26, (word >> 21) & 31, (word >> 16) & 31, word & 0xFFFF


def expect_i(data, pc, opcode, rs, rt, immediate):
    if fields(data, pc) != (opcode, rs, rt, immediate & 0xFFFF):
        raise ValueError(f"Instruction semantics changed at 0x{pc:08X}")


def expect_r(data, pc, rs, rt, rd, shift, function):
    expect_i(data, pc, 0, rs, rt, (rd << 11) | (shift << 6) | function)


def labels(data):
    result = []
    for index, expected in enumerate(("WEATHER", "DRY", "WET", "NIGHT")):
        pointer, = struct.unpack(">I", read(data, 0x138D74 + index * 4, 4))
        # These strings are in the loaded overlay's data, not its code range.
        if not 0x80227DB0 <= pointer < 0x80250E50:
            raise ValueError("Weather label pointer outside overlay data")
        text = read(data, pointer - 0x800FF610, 16).split(b"\0", 1)[0]
        if text != expected.encode("ascii"):
            raise ValueError(f"Unexpected weather label at index {index}")
        result.append({"id": 0x5A + index, "text": expected,
                       "pointer_vram": f"0x{pointer:08X}"})
    return result


def menu_records(data):
    """Bind the string IDs to the weather field and its menu callbacks."""
    for address, callback in ((0x80238948, 0x80201220),
                              (0x80238A48, 0x80201220),
                              (0x80239448, 0x802011A4)):
        record = read(data, address - 0x800FF610, 64)
        function, field = struct.unpack_from(">II", record, 4)
        label, = struct.unpack_from(">H", record, 24)
        value_field, = struct.unpack_from(">I", record, 40)
        choices = struct.unpack_from(">3H", record, 58)
        if (function, field, label, value_field, choices) != (
                callback, 0x800C4F30, 0x5A, 0x800C4F30, (0x5B, 0x5C, 0x5D)):
            raise ValueError(f"Weather menu record changed at 0x{address:08X}")


def witnesses(jp, us):
    # The menu increments a signed halfword and wraps at three choices.
    for pc, op, rs, rt, imm in [
        (0x80201220, 15, 0, 14, 0x800C),
        (0x80201224, 9, 14, 14, 0x4F30),
        (0x80201228, 33, 14, 15, 0),
        (0x80201234, 9, 15, 24, 1),
        (0x80201238, 41, 14, 24, 0),
        (0x80201240, 10, 8, 1, 3),
        (0x80201254, 41, 9, 0, 0),
        (0x8000745C, 33, 25, 25, 0x4EAE),
        (0x80007458, 15, 0, 25, 0x800C),
        (0x80007460, 15, 0, 24, 0x800C),
        (0x80007464, 15, 0, 1, 0x800C),
        (0x80007470, 33, 24, 24, 0x4F30),
        (0x80007474, 41, 1, 24, 0x503C),
        (0x8020175C, 33, 8, 8, 0x6CC0),
        (0x80201738, 9, 0, 15, 1),
        (0x8020174C, 15, 0, 8, 0x8012),
        (0x802017AC, 41, 1, 0, 0x4F30),
        (0x802017F8, 10, 13, 1, 7),
        (0x80036584, 15, 0, 14, 0x800C),
        (0x80036588, 15, 0, 24, 0x800C),
        (0x8003658C, 33, 24, 24, 0x4FFE),
        (0x80036590, 33, 14, 14, 0x503C),
        (0x800365B0, 15, 0, 1, 0x8008),
        (0x800365B8, 49, 1, 4, -0x2510),
        (0x80035834, 15, 0, 14, 0x800C),
        (0x80035838, 33, 14, 14, 0x503C),
        (0x8003583C, 9, 0, 1, 1),
        (0x80035840, 4, 14, 1, (0x80035884 - 0x80035844) // 4),
        (0x80035894, 15, 0, 25, 0x8008),
        (0x800358A0, 33, 25, 25, -0x2588),
        (0x800358BC, 41, 1, 10, -0x7600),
    ]:
        expect_i(jp, pc, op, rs, rt, imm)
    for args in [
        (0x80007468, 0, 25, 12, 1, 0),
        (0x8000746C, 24, 12, 24, 0, 33),
        (0x800365A0, 0, 24, 13, 2, 0),
        (0x800365A8, 0, 14, 10, 4, 0),
        (0x800365AC, 10, 13, 11, 0, 33),
        (0x800365B4, 1, 11, 1, 0, 33),
        (0x80035898, 0, 13, 9, 1, 0),
        (0x8003589C, 25, 9, 25, 0, 33),
        (0x800358B8, 0, 25, 10, 1, 3),
    ]:
        expect_r(jp, *args)
    expect_i(us, 0x80030728, 15, 0, 1, 0x8009)
    expect_r(us, 0x80030730, 0, 12, 11, 2, 0)
    expect_r(us, 0x80030734, 1, 11, 1, 0, 33)
    expect_i(us, 0x80030738, 49, 1, 4, -0x6BBC)


def generate(us, jp):
    verify_rom(us, "usa")
    verify_rom(jp, "japan")
    witnesses(jp, us)
    menu_labels = labels(jp)
    menu_records(jp)
    order = list(struct.unpack(">6h", read(jp, 0x869D2, 12)))
    if sorted(order) != list(range(6)):
        raise ValueError("Default championship is not a permutation of six circuits")
    speed = struct.unpack(">12f", read(jp, 0x7E6F0, 48))
    countdown = list(struct.unpack(">4h", read(jp, 0x7E678, 8)))
    return {
        "schema": 1, "input_sha256": input_digest(),
        "evidence_type": "static cartridge extraction with selected instruction checks",
        "roms": {region: {"size": meta[1], "sha256": meta[2]}
                 for region, meta in ROMS.items()},
        "limitations": [
            "No live reference or port behavior is measured by this report.",
            "Selected witnesses are checked, not an exhaustive control-flow proof.",
            "The weather array is indexed by race slot, separately from circuit ID.",
            "No AI line-of-sight or sensing-distance algorithm is identified.",
            "Countdown values are update counts; elapsed time and maneuver semantics remain unverified.",
        ],
        "menu": {"labels": menu_labels, "weather_array": "0x800C4F30",
                 "slot_index": "0x800C4EAE", "track_array": "0x800C4EB0",
                 "active_weather": "0x800C503C", "element_type": "big-endian signed halfword",
                 "cycle_function": "0x80201220", "race_slot_copy": "0x80007470"},
        "default_championship": {
            "track_order_rom": "0x000869D2", "circuit_indices": order,
            "weather_values": [0] * 6, "scope": "reset callback slots 1 through 6",
            "reset_function": "0x80201724", "zero_store": "0x802017AC",
        },
        "ai_speed_control_parameter": {
            "interpretation": "speed-control parameter, not established vision distance; physical units unverified",
            "japan_table_rom": "0x0007E6F0", "japan_read": "0x800365B8",
            "index": "weather * 4 + difficulty",
            "japan_rows": {name: list(speed[i * 4:i * 4 + 4])
                           for i, name in enumerate(("dry", "wet", "night"))},
            "usa_table_rom": "0x0008A044", "usa_read": "0x80030738",
            "usa_values": list(struct.unpack(">2f", read(us, 0x8A044, 8))),
        },
        "ai_countdown": {
            "table_rom": "0x0007E678", "index": "difficulty",
            "dry_and_night": countdown, "wet": [value >> 1 for value in countdown],
            "wet_test": "0x80035840", "wet_shift": "0x800358B8",
            "wet_store": "0x800358BC", "units": "update counts, not proven frames or distance",
        },
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    checks = parser.add_mutually_exclusive_group()
    checks.add_argument("--check", action="store_true", help="regenerate from both ROMs and compare")
    checks.add_argument("--check-inputs", action="store_true", help="check script/report freshness without ROMs")
    args = parser.parse_args(argv)
    try:
        if args.check_inputs:
            if json.loads(OUTPUT.read_text(encoding="utf-8"))["input_sha256"] != input_digest():
                raise ValueError("Weather audit inputs changed; regenerate the report")
        else:
            data = {region: (ROOT / meta[0]).read_bytes() for region, meta in ROMS.items()}
            text = json.dumps(generate(data["usa"], data["japan"]), indent=2) + "\n"
            if args.check:
                if OUTPUT.read_text(encoding="utf-8") != text:
                    raise ValueError("Weather audit differs; regenerate the report")
            else:
                OUTPUT.parent.mkdir(parents=True, exist_ok=True)
                OUTPUT.write_text(text, encoding="utf-8", newline="\n")
    except (OSError, ValueError, KeyError) as exc:
        print(exc, file=sys.stderr)
        return 1
    print("JP weather static evidence checked" if args.check or args.check_inputs else "JP weather static evidence written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
