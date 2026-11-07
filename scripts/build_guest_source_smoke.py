"""Build entry/return and replacement-conflict fixtures for guest source patches."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", type=Path, required=True)
    parser.add_argument("--mod-tool", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "build/guest-source-smoke")
    args = parser.parse_args()
    for variant in ["hooks", "func_8003F40C", "func_8003F56C", "func_800427D4", "func_80019D20"]:
        output = args.output.resolve() / variant
        output.mkdir(parents=True, exist_ok=True)
        source = "fixture.c" if variant == "hooks" else "conflict.c"
        obj, elf = output / "fixture.o", output / "fixture.elf"
        flags = ["-target", "mips-freestanding", "-mcpu=mips2+noabicalls+nooddspreg",
                 "-mabi=32", "-O2", "-G0", "-mno-abicalls", "-mno-odd-spreg",
                 "-mno-check-zero-division", "-fno-pic", "-ffunction-sections",
                 "-ffreestanding", "-fno-builtin"]
        if variant != "hooks":
            flags += [f"-DREPLACEMENT={variant}"]
        subprocess.run([str(args.zig.resolve()), "cc", *flags, "-c",
                        str(ROOT / "tests/mods/guest_source" / source), "-o", str(obj)], check=True)
        subprocess.run([str(args.zig.resolve()), "ld.lld", "-nostdlib", "-T",
                        str(ROOT / "tests/mods/smoke/mod.ld"), "--unresolved-symbols=ignore-all",
                        "--emit-relocs", "-e", "0", "--no-nmagic", "--gc-sections",
                        str(obj), "-o", str(elf)], check=True)
        manifest = {
            "id": "guest_source_smoke", "version": "1.0.0",
            "display_name": "Guest source compatibility fixture",
            "description": "Developer-only source patch hook/conflict test",
            "authors": ["Lamborghini Recompiled contributors"],
            "game_id": "lamborghini", "minimum_recomp_version": "0.5.0",
        }
        text = "[manifest]\n" + "\n".join(f"{k}={json.dumps(v)}" for k,v in manifest.items())
        text += "\n[inputs]\n" + "\n".join(f"{k}={json.dumps(v)}" for k,v in {
            "elf_path": elf.as_posix(), "mod_filename": "guest_source_smoke",
            "func_reference_syms_file": (ROOT / "lamborghini.syms.toml").as_posix(),
            "data_reference_syms_files": [(ROOT / "lamborghini.datasyms.toml").as_posix()],
        }.items()) + "\n"
        config = output / "mod.toml"
        config.write_text(text, encoding="utf-8")
        subprocess.run([str(args.mod_tool.resolve()), str(config), str(output)], check=True)


if __name__ == "__main__":
    main()
