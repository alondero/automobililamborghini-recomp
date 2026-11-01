"""Build the original A21 MIPS smoke and protected-hook rejection packages.

Requires Clang+LLD (or --zig) and a built pinned RecompModTool. No ROM required.
The host-native siblings are CMake targets lambo_mod_native_smoke/bad_api.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--lld", default="ld.lld")
    parser.add_argument("--zig", type=Path, help="use Zig's bundled Clang and LLD")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    output = build / "mod-smoke"
    output.mkdir(parents=True, exist_ok=True)
    compiler = [str(args.zig.resolve()), "cc"] if args.zig else [args.clang]
    linker = [str(args.zig.resolve()), "ld.lld"] if args.zig else [args.lld]
    tool = build / "lib/N64ModernRuntime/librecomp/N64Recomp/RecompModTool"
    if sys.platform == "win32":
        tool = tool.with_suffix(".exe")
    for name, command in (("compiler", compiler), ("linker", linker), ("RecompModTool", [str(tool)])):
        if not shutil.which(command[0]):
            parser.error(f"{name} not found: {command[0]}")
    for protected in (False, True):
        directory = output / "protected" if protected else output
        directory.mkdir(exist_ok=True)
        flags = ["-target", "mips-freestanding" if args.zig else "mips", "-mips2", "-mabi=32",
                 "-O2", "-G0", "-mno-abicalls", "-mno-odd-spreg", "-mno-check-zero-division",
                 "-fno-pic", "-ffunction-sections", "-ffreestanding"]
        if protected:
            flags.append("-DLAMBO_SMOKE_PROTECTED_HOOK")
        obj, elf = directory / "smoke.o", directory / "smoke.elf"
        subprocess.run(compiler + flags + ["-c", str(ROOT / "tests/mods/smoke/smoke.c"), "-o", str(obj)], check=True)
        subprocess.run(linker + ["-nostdlib", "-T", str(ROOT / "tests/mods/smoke/mod.ld"),
                                "--unresolved-symbols=ignore-all", "--emit-relocs", "-e", "0",
                                "--no-nmagic", "--gc-sections", str(obj), "-o", str(elf)], check=True)
        # Stage the manifest at its original directory-relative location only in
        # memory; write an absolute-path config under build for alternate builds.
        text = (ROOT / "tests/mods/smoke/mod.toml").read_text(encoding="utf-8")
        text = text.replace('../../../build/mod-smoke/smoke.elf', elf.as_posix())
        for filename in ("lamborghini.syms.toml", "lamborghini.datasyms.toml"):
            text = text.replace('../../../' + filename, (ROOT / filename).as_posix())
        config = directory / "mod.toml"
        config.write_text(text, encoding="utf-8")
        subprocess.run([str(tool), str(config), str(directory)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
