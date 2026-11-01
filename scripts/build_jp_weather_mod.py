"""Build the ROM-free JP weather NRM with explicitly supplied toolchain programs.

Use --wsl-distro Ubuntu --clang /usr/bin/clang when the Windows Clang build
does not include MIPS. Only the compiler runs in WSL; linker and mod tool run
on the host. No tools are downloaded, installed or configured by this script.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
FLAGS = ["-target", "mips", "-mips2", "-mabi=32", "-O2", "-G0",
         "-mno-abicalls", "-mno-odd-spreg", "-mno-check-zero-division",
         "-fno-pic", "-ffunction-sections", "-nostdinc"]


def host_tool(command):
    """Resolve explicit paths before child commands switch to the repo root."""
    if "/" in command or "\\" in command or Path(command).drive:
        return str(Path(command).resolve())
    return command  # Bare commands retain normal PATH lookup.


def compiler_command(clang, source, output, distro=None, translate=None):
    if distro:
        if translate is None:
            def translate(path):
                return subprocess.check_output(
                    ["wsl", "-d", distro, "--exec", "wslpath", "-a", path.as_posix()],
                    text=True).strip()
        return ["wsl", "-d", distro, "--exec", clang, *FLAGS, "-c",
                translate(source), "-o", translate(output)]
    return [clang, *FLAGS, "-c", str(source), "-o", str(output)]


def authoring_manifest(template, elf, symbols):
    # The checked-in authoring manifest stays unchanged. A derived copy uses
    # absolute inputs so --output works outside the default build directory.
    for key, value in (("elf_path", elf), ("func_reference_syms_file", symbols)):
        template, count = re.subn(
            rf"(?m)^{key}\s*=.*$",
            lambda match: f"{key} = {json.dumps(value.as_posix())}", template)
        if count != 1:
            raise ValueError(f"Expected exactly one manifest {key}")
    return template


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", required=True, help="Clang executable (Linux path with --wsl-distro)")
    parser.add_argument("--linker", required=True, help="Host ld.lld executable")
    parser.add_argument("--mod-tool", required=True, help="Host pinned RecompModTool executable")
    parser.add_argument("--wsl-distro", help="Run only Clang in this WSL distribution")
    parser.add_argument("--output", type=Path, default=ROOT / "build/jp-weather",
                        help="Output directory, default: repository build/jp-weather")
    args = parser.parse_args(argv)
    # WSL compiler paths belong to Linux and must not be resolved on the host.
    clang = args.clang if args.wsl_distro else host_tool(args.clang)
    linker, mod_tool = host_tool(args.linker), host_tool(args.mod_tool)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / "mods/jp_weather/weather.c"
    obj, elf = output / "weather.o", output / "weather.elf"
    subprocess.run(compiler_command(clang, source, obj, args.wsl_distro),
                   check=True, cwd=ROOT)
    subprocess.run([linker, "-nostdlib", "-T", str(ROOT / "mods/jp_weather/mod.ld"),
                    "--unresolved-symbols=ignore-all", "--emit-relocs", "-e", "0",
                    "--no-nmagic", "--gc-sections", str(obj), "-o", str(elf)],
                   check=True, cwd=ROOT)
    manifest = output / "build-mod.toml"
    manifest.write_text(authoring_manifest(
        (ROOT / "mods/jp_weather/mod.toml").read_text(encoding="utf-8"),
        elf, ROOT / "lamborghini.syms.toml"), encoding="utf-8", newline="\n")
    subprocess.run([mod_tool, str(manifest), str(output)], check=True, cwd=ROOT)
    package = output / "lambo_jp_weather.nrm"
    print(f"{package}\nSHA-256: {hashlib.sha256(package.read_bytes()).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
