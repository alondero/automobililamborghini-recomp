"""Build and run the ROM-free Vulkan upload/draw/readback probe.

Desktop: python tools/run_vulkan_mapped_memory_probe.py
Android: add --ndk PATH --device ADB_SERIAL (does not install or change the game).
"""
import argparse
import os
import platform
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tests.patched_source import PLUME, patched_file  # noqa: E402


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path)
    parser.add_argument("--device", help="ADB serial for an Android system-driver run")
    parser.add_argument("--dxc", type=Path)
    parser.add_argument("--require-noncoherent", nargs="?", const="any",
                        choices=("any", "upload", "readback", "both"),
                        help="Exit 2 if the requested physical coverage is unavailable")
    args = parser.parse_args()
    if os.name != "nt" and not sys.platform.startswith("linux"):
        parser.error("probe builds need a Windows or Linux host")
    if bool(args.ndk) != bool(args.device):
        parser.error("--ndk and --device must be used together")
    host = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    suffix = ".exe" if os.name == "nt" else ""
    output = ROOT / "build-vulkan-mapped-memory" / ("android" if args.ndk else "desktop")
    output.mkdir(parents=True, exist_ok=True)
    # git apply in a repository subdirectory filters paths relative to that
    # repository. Replay outside it, just as the recording fixtures do.
    with tempfile.TemporaryDirectory(prefix="lambo-vulkan-probe-") as temporary:
        for name in ("plume_vulkan.cpp", "plume_vulkan.h", "plume_render_interface.h",
                     "plume_render_interface_types.h"):
            source = patched_file(PLUME, name, temporary)
            (output / name).write_bytes(source.encode("utf-8"))
    architecture = "arm64" if os.name != "nt" and platform.machine().lower() in ("arm64", "aarch64") else "x64"
    dxc_root = ROOT / "lib/rt64/src/contrib/dxc"
    dxc = args.dxc or dxc_root / "bin" / architecture / ("dxc.exe" if os.name == "nt" else "dxc-linux")
    shader_environment = os.environ.copy()
    if os.name != "nt" and not args.dxc:
        # Match RT64's pinned host compiler and its shared-library directory.
        shader_environment["LD_LIBRARY_PATH"] = os.pathsep.join(
            [str(dxc_root / "lib" / architecture), os.environ.get("LD_LIBRARY_PATH", "")])
    shaders = []
    for stage, entry in (("vs", "VSMain"), ("ps", "PSMain")):
        target = output / f"{stage}.spv"
        run(dxc, "-spirv", "-T", f"{stage}_6_0", "-E", entry, "-Fo", target,
            ROOT / "tests/gpu/vulkan_mapped_memory.hlsl", env=shader_environment)
        shaders.append(target)
    compiler = shutil.which("g++")
    if args.ndk:
        compiler = args.ndk / "toolchains/llvm/prebuilt" / host / "bin" / ("clang++" + suffix)
    if not compiler:
        parser.error("g++ compiler unavailable")
    executable = output / ("probe" if args.ndk else "probe" + suffix)
    includes = [output, PLUME, PLUME / "contrib/volk", PLUME / "contrib/Vulkan-Headers/include",
                PLUME / "contrib/VulkanMemoryAllocator/include"]
    flags = ["-std=c++17", "-O1", "-g", "-pthread"]
    if args.ndk:
        flags += ["--target=aarch64-linux-android28", "-static-libstdc++", "-ldl", "-landroid"]
    elif sys.platform.startswith("linux"):
        flags += ["-ldl", "-lX11"]
    run(compiler, ROOT / "tests/gpu/vulkan_mapped_memory.cpp", output / "plume_vulkan.cpp",
        *[f"-I{path}" for path in includes], *flags, "-o", executable)
    if args.device:
        adb = ["adb", "-s", args.device]
        remote = "/data/local/tmp/lambo-mapped-memory"
        run(*adb, "shell", "mkdir", "-p", remote)
        for path in [executable, *shaders]:
            run(*adb, "push", path, f"{remote}/{path.name}")
        run(*adb, "shell", "chmod", "755", f"{remote}/probe")
        command = [*adb, "shell", f"{remote}/probe", *[f"{remote}/{path.name}" for path in shaders]]
    else:
        command = [str(executable), *map(str, shaders)]
    if args.require_noncoherent:
        command.append(args.require_noncoherent)
    return subprocess.run(command).returncode


if __name__ == "__main__":
    raise SystemExit(main())
