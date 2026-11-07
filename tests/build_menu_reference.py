"""Build the complete USA menu-input ROM oracle with frozen sensitivity hooks."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def build(recompiler, output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    rom = ROOT / "Automobili Lamborghini (USA).z64"
    data = rom.read_bytes()
    assert hashlib.sha256(data).hexdigest() == "cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c"
    assert hashlib.sha256(data[0x427d4:0x4384c]).hexdigest() == "081f33e4bf35e3245f386ae299a4997a4e58050d25d4e612d6066cd6a2fe7264"
    sections = tomllib.loads((ROOT / "lamborghini.syms.toml").read_text())["section"]
    target = next(f for section in sections for f in section["functions"]
                  if f["name"] == "func_800427D4")
    assert target["vram"] == 0x80041bd4 and target["size"] == 0x1078
    callees = {"func_80066F84", "func_8003CD84"}
    wanted = callees | {"func_800427D4"}
    symbol_text = ""
    for section in sections:
        symbol_text += "[[section]]\n"
        for key, value in section.items():
            if key != "functions":
                symbol_text += f"{key}={json.dumps(value)}\n"
        symbol_text += "functions=[\n" + ",\n".join(
            '{name=' + json.dumps(f["name"]) + f',vram={f["vram"]},size={f["size"]}' + '}'
            for f in section["functions"] if f["name"] in wanted) + "\n]\n"
    symbols = output / "symbols.toml"
    symbols.write_text(symbol_text, encoding="utf-8")
    text = "[input]\n" + "\n".join(f"{k}={json.dumps(str(v))}" for k, v in {
        "recomp_include": '#include "lambo_guest_calls.h"',
        "symbols_file_path": symbols.as_posix(), "rom_file_path": rom.as_posix(),
        "output_func_path": output.as_posix(),
    }.items()) + "\nsingle_file_output=true\n[patches]\nignored=" + json.dumps(sorted(callees)) + "\n"
    for pc, reg in [(0x80042500, 8), (0x800426e4, 9), (0x800428d4, 15), (0x8004294c, 14)]:
        code = f'{{ extern int menu_reference_scale(int); ctx->r{reg} = (gpr)(int32_t)menu_reference_scale((int32_t)ctx->r{reg}); }}'
        text += f'[[patches.hook]]\nfunc="func_800427D4"\nbefore_vram={pc}\ntext={json.dumps(code)}\n'
    config = output / "menu_reference.toml"
    config.write_text(text, encoding="utf-8")
    subprocess.run([str(recompiler), str(config)], check=True)
    code = (output / ".c").read_text(encoding="utf-8")
    assert code.count("RECOMP_FUNC void func_800427D4(") == 1
    (output / "menu_reference.c").write_text(
        code.replace("RECOMP_FUNC void func_800427D4(", "RECOMP_FUNC void menu_reference("),
        encoding="utf-8")


if __name__ == "__main__":
    build(Path(sys.argv[1]).resolve(), sys.argv[2])
