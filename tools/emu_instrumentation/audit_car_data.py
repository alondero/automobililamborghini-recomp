"""Verify USA car-selection evidence and decode parameters from a supplied ROM.

No disassembler dependency. Instruction checks pin the relevant decoded MIPS
operations to this ROM; output contains small numeric tables, not ROM assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tomllib

ROOT = Path(__file__).resolve().parents[2]
SHA256 = "cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c"
MENU_DIFFICULTY_RECORD_OFFSET = 0x14A184
MENU_DIFFICULTY_TEXT_RECORD_OFFSET = 0x14A1B8
TEXT_POINTER_TABLE_OFFSET = 0x149710
TEXT_BANK_RUNTIME_BASE = 0x801F9490
TEXT_BANK_ROM_OFFSET = 0x158BF0
DIFFICULTY_XORI_RUNTIME = 0x8003DAB8
MODEL_RANGE_CHECK_RUNTIME = 0x8003E9A8
MODEL_CURSOR_LOAD_RUNTIME = 0x8000722C
SLEW_TABLE_OFFSET = 0x89D70
THRESHOLD_TABLE_OFFSET = 0x89F0C
MODEL_DESCRIPTOR_TABLE_OFFSET = 0xAEE80
ENGINE_TABLE_OFFSET = 0xAFAE0
ENGINE_ROW_STRIDE = 52
ENGINE_MAX_GEAR_OFFSET = 8
ENGINE_MAX_GEARS = (5, 6, 6, 6, 5, 6, 5, 5)
ENGINE_CURVE_RUNTIME_BASE = 0x8013D6F0
ENGINE_CURVE_ROM_BASE = 0xAF0E0
DIFFICULTY_E290_OFFSET = 0xAFC80
DIFFICULTY_E2D0_OFFSET = 0xAFCC0


def audit(rom, root=ROOT):
    if hashlib.sha256(rom).hexdigest() != SHA256:
        raise ValueError("Expected the audited big-endian USA ROM (SHA-256 mismatch)")

    def check(offset, fmt, expected):
        actual = struct.unpack_from(fmt, rom, offset)
        if actual != expected:
            raise ValueError(f"ROM {offset:#x}: expected {expected}, got {actual}")

    # Menu record: callback, target; next record displays text IDs 20 / 21.
    check(MENU_DIFFICULTY_RECORD_OFFSET, ">II", (0x8003DA98, 0x800CE7A4))
    check(MENU_DIFFICULTY_TEXT_RECORD_OFFSET, ">IHH", (20, 21, 0))
    # Text pointer table is loaded separately from the string bank.
    def text_at(index):
        address, = struct.unpack_from(">I", rom, TEXT_POINTER_TABLE_OFFSET + index * 4)
        offset = address - TEXT_BANK_RUNTIME_BASE + TEXT_BANK_ROM_OFFSET
        return rom[offset:rom.index(b"\0", offset)].decode("ascii")

    if [text_at(i) for i in (19, 20, 21)] != ["DIFFICULTY", "NOVICE", "EXPERT"]:
        raise ValueError("Difficulty menu text mapping changed")
    # Runtime code maps to ROM at runtime - 0x80000000 + 0xC00.
    check(DIFFICULTY_XORI_RUNTIME - 0x80000000 + 0xC00, ">I", (0x392A0001,))  # xori t2,t1,1
    check(MODEL_RANGE_CHECK_RUNTIME - 0x80000000 + 0xC00, ">I", (0x29810018,))  # slti at,t4,24
    check(MODEL_CURSOR_LOAD_RUNTIME - 0x80000000 + 0xC00, ">I", (0x85EFE7E8,))  # lh t7,model cursor
    check(SLEW_TABLE_OFFSET, ">8h", (4, 7, 1, 1, 1, 1, 1, 2))
    check(THRESHOLD_TABLE_OFFSET, ">8I", (0x3F400000, 0x3EE66666, 0x3727C5AC, 0x358637BD,
                            0x38A7C5AC, 0x370637BD, 0x3727C5AC, 0x358637BD))
    categories = [struct.unpack_from(">H", rom, MODEL_DESCRIPTOR_TABLE_OFFSET + i * 24)[0]
                  for i in range(24)]
    if categories != [c for c in (0, 4, 1, 2, 3, 5, 6, 7) for _ in range(3)]:
        raise ValueError("Model descriptor categories changed")
    parameters = []
    for category in range(8):
        offset = ENGINE_TABLE_OFFSET + category * ENGINE_ROW_STRIDE
        halfwords = struct.unpack_from(">5H", rom, offset)
        max_gear = struct.unpack_from(">H", rom, offset + ENGINE_MAX_GEAR_OFFSET)[0]
        if max_gear != ENGINE_MAX_GEARS[category]:
            raise ValueError(f"ROM engine category {category}: expected max gear "
                             f"{ENGINE_MAX_GEARS[category]}, got {max_gear}")
        curve_address, = struct.unpack_from(">I", rom, offset + 48)
        curve_offset = curve_address - ENGINE_CURVE_RUNTIME_BASE + ENGINE_CURVE_ROM_BASE
        curve = struct.unpack_from(">160h", rom, curve_offset)
        parameters.append({
            "category": category, "rom_offset": f"0x{offset:X}",
            "engine_halfwords": list(halfwords),
            "ratio_multiplier": struct.unpack_from(">f", rom, offset + 12)[0],
            "gear_ratios_including_neutral": list(struct.unpack_from(">7f", rom, offset + 16)),
            "coefficient_2c": struct.unpack_from(">f", rom, offset + 44)[0],
            "engine_curve_address": f"0x{curve_address:X}",
            "engine_curve_peak_raw": max(curve),
        })
    symbols = tomllib.loads((root / "lamborghini.syms.toml").read_text(encoding="utf-8"))
    functions = {f["name"]: f for s in symbols["section"] for f in s.get("functions", [])}
    head, tail = (functions[n] for n in ("func_80019D20", "func_8001E01C"))
    covered = head["vram"] <= tail["vram"] and head["vram"] + head["size"] >= tail["vram"] + tail["size"]
    if not covered:
        raise ValueError("Updater no longer includes the force-stubbed tail range")
    return {
        "rom_sha256": SHA256,
        "difficulty": {"cursor": "0x800CE7A4", "race_value": "0x800CE79C",
                       "labels": [text_at(20), text_at(21)]},
        "model_cursor_base": "0x800CE7E8", "model_categories": categories,
        "availability": {"flags": "0x800985C0", "progress": "0x800A4170",
                         "unconditional_categories": [0, 4],
                         "progress_masks_by_category": {"1": 4, "2": 8, "3": 2,
                                                        "5": 1, "6": 32, "7": 16}},
        "slew_region_halfwords_not_eight_cars": list(struct.unpack_from(">8h", rom, SLEW_TABLE_OFFSET)),
        "threshold_region_floats_not_eight_cars": list(struct.unpack_from(">8f", rom, THRESHOLD_TABLE_OFFSET)),
        "engine_parameters": parameters,
        "category_difficulty_e290": [list(struct.unpack_from(">2f", rom, DIFFICULTY_E290_OFFSET + i * 8)) for i in range(8)],
        "category_difficulty_e2d0": [list(struct.unpack_from(">2f", rom, DIFFICULTY_E2D0_OFFSET + i * 8)) for i in range(8)],
        "stub_tail_covered_by_updater": covered,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(audit(args.rom.read_bytes()), indent=2) + "\n"
    if args.output:
        args.output.write_text(result, encoding="utf-8", newline="\n")
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
