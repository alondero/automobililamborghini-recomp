"""Compile readable guest C to MIPS, then recompile it with the pinned N64Recomp.

Clang and LLD must support MIPS. --zig uses Zig's bundled toolchain. On Windows
--wsl-distro runs Clang/LLD in WSL while N64Recomp runs on the host.
Output is entirely build-local.
"""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def build(output, recompiler, clang="clang", linker="ld.lld", distro=None, zig=None):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)

    def path(value):
        value = str(Path(value).resolve())
        if distro:
            return subprocess.check_output(
                ["wsl", "-d", distro, "--exec", "wslpath", "-a", value], text=True).strip()
        return value

    def run_tool(tool, args):
        command = ["wsl", "-d", distro, "--exec", tool] if distro else [tool]
        if zig:
            command = [zig, "cc" if tool == clang else "ld.lld"]
        subprocess.run(command + args, check=True)

    elf = output / "guest_patches.elf"
    # Zig's target features override Clang's -m flags, including odd FPR use.
    cpu = "mips2+noabicalls+nooddspreg" if zig else "mips2"
    objects = []
    for source in ("pit_stop", "vehicle_select", "menu_input", "vehicle_update"):
        obj = output / f"{source}.o"
        objects.append(path(obj))
        run_tool(clang, ["-target", "mips-freestanding" if zig else "mips", f"-mcpu={cpu}", "-mabi=32", "-O2", "-G0",
        "-mno-abicalls", "-mno-odd-spreg", "-mno-check-zero-division", "-fno-pic",
        "-ffreestanding", "-fno-builtin", "-fno-strict-aliasing", "-ffp-contract=off",
        "-fno-jump-tables", "-Wall", "-Wextra", "-Werror", "-c",
        path(ROOT / f"guest/{source}.c"), "-o", path(obj)])
    run_tool(linker, ["-nostdlib", "-T", path(ROOT / "guest/patches.ld"),
        "-T", path(ROOT / "guest/symbols.ld"), "--emit-relocs",
        "--unresolved-symbols=ignore-all", "-e", "0", *objects, "-o", path(elf)])
    config = output / "recompile.toml"
    config.write_text("[input]\n" + "\n".join(
        f"{key} = {json.dumps(str(value))}" for key, value in {
            "recomp_include": '#include "lambo_guest_calls.h"',
            "elf_path": elf.as_posix(),
            "output_func_path": output.as_posix(),
            "output_binary_path": (output / "guest_patches.bin").as_posix(),
            "func_reference_syms_file": (ROOT / "lamborghini.syms.toml").as_posix(),
        }.items()) + "\nuse_absolute_symbols = true\nsingle_file_output = true\n",
        encoding="utf-8")
    subprocess.run([str(recompiler), str(config)], check=True)
    data = (output / "guest_patches.bin").read_bytes()
    (output / "guest_patch_data.inc").write_text(
        "// Generated from guest C; do not edit.\n"
        "static const unsigned char guest_patch_data[] = {\n" +
        "\n".join(",".join(f"0x{b:02x}" for b in data[i:i+24]) + ","
                  for i in range(0, len(data), 24)) + "\n};\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--recompiler", type=Path, required=True)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--linker", default="ld.lld")
    parser.add_argument("--wsl-distro")
    parser.add_argument("--zig", help="Use Zig's bundled Clang and LLD on the host")
    args = parser.parse_args()
    if args.zig and args.wsl_distro:
        parser.error("--zig and --wsl-distro are mutually exclusive")
    build(args.output, args.recompiler.resolve(), args.clang, args.linker, args.wsl_distro, args.zig)


if __name__ == "__main__":
    main()
