"""Build the complete USA vehicle-update ROM oracle with frozen port semantics.

This does not consume production hook lists: source migration removes those.
Only race-table relocations are shared. Frozen host helpers live in
tests/vehicle_reference; generated ROM code stays in the build directory.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from race_field_patches import ROM_SHA256, references

BODY_SHA256 = "03213bd6766ae072f5c6ef0e0797c552f5a1573ccc617fb0b1df4a089c4aad11"

CALLEES = {
    "func_80014D50", "func_80025834", "func_8002622C", "func_80028250",
    "func_80029DCC", "func_8002A070", "func_8002D37C", "func_80037BC0",
    "func_80075720", "func_80075730", "func_800758F0",
}


def build(recompiler, output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    sections = tomllib.loads((ROOT / "lamborghini.syms.toml").read_text(encoding="utf-8"))["section"]
    target_name = "func_80019D20"
    target = next(f for s in sections for f in s["functions"] if f["name"] == target_name)
    assert target["vram"] == 0x80019120
    # Complete function verified against the ROM, including its final epilogue.
    target = {**target, "size": 0x4EFC}
    symbols = output / "symbols.toml"
    text = ""
    for section in sections:
        text += "[[section]]\n"
        for key, value in section.items():
            if key != "functions":
                text += f"{key}={json.dumps(value)}\n"
        selected = [target if f["name"] == target_name else f
                    for f in section["functions"]
                    if f["name"] == target_name or f["name"] in CALLEES]
        text += "functions=[\n" + ",\n".join(
            "{name=" + json.dumps(f["name"]) +
            f',vram={f["vram"]},size={f["size"]}' + "}" for f in selected) + "\n]\n"
    symbols.write_text(text, encoding="utf-8")
    rom_path = ROOT / "Automobili Lamborghini (USA).z64"
    rom = rom_path.read_bytes()
    if hashlib.sha256(rom).hexdigest() != ROM_SHA256:
        raise ValueError("Vehicle oracle requires the verified USA ROM")
    if hashlib.sha256(rom[0x19D20:0x1EC1C]).hexdigest() != BODY_SHA256:
        raise ValueError("Vehicle oracle body differs from verified function")
    text = "[input]\n" + "\n".join(f"{k}={json.dumps(str(v))}" for k, v in {
        "recomp_include": '#include "lambo_guest_calls.h"',
        "symbols_file_path": symbols.as_posix(),
        "rom_file_path": rom_path.as_posix(),
        "output_func_path": output.as_posix(),
    }.items()) + "\nsingle_file_output=true\n[patches]\nignored=" + json.dumps(sorted(CALLEES)) + "\n"

    def hook(pc, code):
        return (f'[[patches.hook]]\nfunc="{target_name}"\nbefore_vram={pc}\n'
                f'text={json.dumps(code)}\n')

    for _, pc, _, code in references(rom,
                                    [(target_name, target["vram"], target["size"])]):
        text += f'[[patches.instruction]]\nfunc="{target_name}"\nvram={pc}\nvalue=0\n'
        text += hook(pc, code)
    for pc, name in ((0x80019FBC, "lambo_analog_throttle_begin"),
                     (0x8001A120, "lambo_analog_throttle_apply"),
                     (0x8001A9A0, "lambo_analog_brake_apply"),
                     (0x8001B8E0, "lambo_steering_direct")):
        name = "vehicle_reference_" + name
        text += hook(pc, f"{{ extern void {name}(uint8_t*); {name}(rdram); }}")
    text += hook(0x8001AA90,
                 "{ extern int32_t vehicle_reference_lambo_steering_raw(uint8_t*, int32_t); "
                 "ctx->r10 = (gpr)(int64_t)vehicle_reference_lambo_steering_raw(rdram, (int32_t)ctx->r10); }")
    text += hook(0x800191A4,
                 "{ extern int vehicle_reference_field_car(uint8_t*, int); "
                 "if (ctx->r10 >= 13 && vehicle_reference_field_car(rdram, "
                 "MEM_H(0, (gpr)(int32_t)0x80098398))) ctx->r10 = 5; }")
    config = output / "vehicle_reference.toml"
    config.write_text(text, encoding="utf-8")
    subprocess.run([str(recompiler), str(config)], check=True)
    code = (output / ".c").read_text(encoding="utf-8")
    definition = "RECOMP_FUNC void func_80019D20("
    assert code.count(definition) == 1
    (output / "vehicle_reference.c").write_text(
        code.replace(definition, "RECOMP_FUNC void func_80019D20_reference("), encoding="utf-8")


if __name__ == "__main__":
    build(Path(sys.argv[1]).resolve(), sys.argv[2])
