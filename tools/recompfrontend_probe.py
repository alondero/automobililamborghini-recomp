"""Compile upstream frontend integration seams without changing the game or saves.

This is a migration experiment, not a replacement frontend build. Dependencies
must already be checked out; this command does not download or modify them.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frontend", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--rt64", required=True, type=Path)
    parser.add_argument("--recompiler", required=True, type=Path)
    parser.add_argument("--sdl-include", required=True, type=Path)
    parser.add_argument("--generated-include", action="append", default=[], type=Path,
                        help="Build include directories, e.g. miniz_export.h's directory")
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    frontend, runtime, rt64 = (p.resolve() for p in
                               (args.frontend, args.runtime, args.rt64))
    ui = frontend / "recompui"
    includes = [
        args.sdl_include.resolve(), args.recompiler.resolve() / "include",
        runtime / "librecomp/include", runtime / "ultramodern/include",
        runtime / "thirdparty", runtime / "thirdparty/concurrentqueue",
        runtime / "thirdparty/miniz", runtime / "thirdparty/sse2neon",
        frontend / "recompinput/include", frontend / "recompinput/src",
        frontend / "lib/GamepadMotionHelpers", frontend / "lib/SlotMap",
        ui / "include", ui / "include/recompui", ui / "src",
        ui / "lib/RmlUi/Include", ui / "lib/RmlUi/Backends",
        rt64 / "include", rt64 / "src", rt64 / "src/rhi", rt64 / "src/render",
        rt64 / "src/contrib",
        rt64 / "src/contrib/hlslpp/include", rt64 / "src/contrib/plume",
        rt64 / "src/contrib/dxc/inc",
        rt64 / "src/contrib/nativefiledialog-extended/src/include",
    ]
    includes.extend(p for p in (ui / "src").iterdir() if p.is_dir())
    includes.extend(p.resolve() for p in args.generated_include)
    sources = [
        frontend / "recompinput/src/profiles.cpp",
        ui / "src/config/ui_config_tab_graphics.cpp",
        ui / "src/renderer/rt64_render_context.cpp",
    ]
    for label, path in (("frontend", frontend), ("runtime", runtime), ("RT64", rt64)):
        revision = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"],
                                  capture_output=True, text=True, check=True)
        print(f"{label}: {revision.stdout.strip()}", flush=True)

    command = [args.cxx, "-std=c++20", "-fsyntax-only", "-fmax-errors=5",
               "-DNOMINMAX", "-DHLSL_CPU", "-DRMLUI_SDL_VERSION_MAJOR=2"]
    for path in includes:
        command.extend(["-I", str(path)])
    # MinGW needs its sibling DLLs even when the compiler has an absolute path.
    env = os.environ.copy()
    compiler = Path(args.cxx)
    if compiler.is_absolute():
        env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    failures = []
    for source in sources:
        print(f"\nChecking {source.relative_to(frontend)}", flush=True)
        result = subprocess.run([*command, str(source)], env=env, check=False)
        if result.returncode:
            failures.append(source.name)
    if failures:
        print("\nNot source-compatible: " + ", ".join(failures), flush=True)
        return 1
    print("\nSource probes passed. Linking, assets, runtime behavior and migration remain untested.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
