"""Generate a ROM oracle for pit parity, independently of production overrides.

The only stock changes are the port's participant-table relocations. Three test
hooks reproduce the pre-migration assistance for enabled-mode comparisons; they
are never linked into the game. No ROM bytes or generated C are checked in.
"""
import json
from pathlib import Path
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from race_field_patches import references


def build(recompiler, output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    symbols = ROOT / "lamborghini.syms.toml"
    rom_path = ROOT / "Automobili Lamborghini (USA).z64"
    sections = tomllib.loads(symbols.read_text())["section"]
    funcs = [f for s in sections for f in s["functions"]]
    target = next(f for f in funcs if f["name"] == "func_8006B470")
    callees = {"func_80066F84", "func_800670BC", "func_800673A0",
               "func_80067428", "func_8006B320", "func_80075730"}
    # Keep genuine sizes: the recompiler elides calls to zero-size functions.
    # Only the handler is emitted; services are supplied by the test harness.
    reference_symbols = output / "symbols.toml"
    symbol_text = ""
    for section in sections:
        symbol_text += "[[section]]\n"
        for key, value in section.items():
            if key != "functions": symbol_text += f"{key}={json.dumps(value)}\n"
        symbol_text += "functions=[\n" + ",\n".join(
            '{name=' + json.dumps(f["name"]) + f',vram={f["vram"]},size={f["size"]}' + '}'
            for f in section["functions"] if f == target or f["name"] in callees) + "\n]\n"
    reference_symbols.write_text(symbol_text, encoding="utf-8")
    text = "[input]\n" + "\n".join(f"{k}={json.dumps(str(v))}" for k, v in {
        "recomp_include": '#include "lambo_guest_calls.h"',
        "symbols_file_path": reference_symbols.as_posix(), "rom_file_path": rom_path.as_posix(),
        "output_func_path": output.as_posix(),
    }.items()) + "\nsingle_file_output=true\n[patches]\nignored=" + json.dumps(sorted(callees)) + "\n"
    for name, pc, _, code in references(rom_path.read_bytes(), [(target["name"], target["vram"], target["size"])]):
        text += f'[[patches.instruction]]\nfunc="{name}"\nvram={pc}\nvalue=0\n'
        text += f'[[patches.hook]]\nfunc="{name}"\nbefore_vram={pc}\ntext={json.dumps(code)}\n'
    for pc, kind in [(0x8006aad8, "start"), (0x8006ac8c, "fuel"), (0x8006b610, "tyres")]:
        code = f'{{ extern void pit_reference_{kind}(uint8_t*, recomp_context*); pit_reference_{kind}(rdram, ctx); }}'
        text += f'[[patches.hook]]\nfunc="func_8006B470"\nbefore_vram={pc}\ntext={json.dumps(code)}\n'
    config = output / "pit_reference.toml"
    config.write_text(text, encoding="utf-8")
    subprocess.run([str(recompiler), str(config)], check=True)
    # Rename only the oracle's definition so the real patch can link alongside it.
    source = output / "pit_reference.c"
    # In ROM+symbols mode single-file output uses the empty ELF stem: '.c'.
    code = (output / ".c").read_text(encoding="utf-8")
    assert code.count("RECOMP_FUNC void func_8006B470(") == 1
    source.write_text(code.replace("RECOMP_FUNC void func_8006B470(",
                                   "RECOMP_FUNC void pit_reference("), encoding="utf-8")


if __name__ == "__main__":
    build(Path(sys.argv[1]).resolve(), sys.argv[2])
