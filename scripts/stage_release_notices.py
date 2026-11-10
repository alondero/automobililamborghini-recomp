"""Stage desktop license notices without copying dependency code or game data."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def stage(destination, platform):
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "LICENSE", destination / "LICENSE")
    notices = destination / "licenses"
    notices.mkdir(exist_ok=True)
    copied = []
    for source_root in [ROOT / "lib"]:
        for source in sorted(source_root.rglob("*")):
            if not source.is_file() or ".git" in source.parts:
                continue
            name = source.name.lower()
            if not ("license" in name or name.startswith(("copying", "copyright", "notices"))):
                continue
            if source.suffix.lower() not in ("", ".txt", ".md", ".lib", ".runtime", ".gplv2", ".gplv3", ".lgplv2.1"):
                continue
            target = notices / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            copied.append(target.relative_to(destination).as_posix())
    # FTL.TXT is the FreeType license selected for the bundled Windows library.
    for source in (ROOT / "assets/licenses").glob("*"):
        if source.is_file():
            target = notices / "upstream" / source.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    if platform == "windows":
        compiler = shutil.which("gcc")
        if not compiler:
            raise RuntimeError("gcc must be on PATH to identify runtime notices")
        toolchain = Path(compiler).resolve().parents[1] / "licenses"
        required = [toolchain / "gcc/COPYING.RUNTIME", toolchain / "gcc/COPYING3",
                    toolchain / "winpthreads/COPYING", toolchain / "mingw-w64/COPYING"]
        for source in required:
            if not source.is_file():
                raise RuntimeError(f"Missing compiler runtime notice: {source}")
            target = notices / "toolchain" / source.relative_to(toolchain)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    else:
        major = subprocess.check_output(["gcc", "-dumpversion"], text=True).strip().split(".")[0]
        for package in [f"gcc-{major}-base", "libsdl2-2.0-0", "libfreetype6"]:
            source = Path("/usr/share/doc") / package / "copyright"
            if source.is_file():
                target = notices / "system" / (package + ".txt")
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
    version = re.search(r"project\(lamborghini_modern VERSION ([\d.]+)",
                        (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)
    (destination / "NOTICE.txt").write_text(
        f"Automobili Lamborghini Recompiled {version}\n\n"
        "Project code is distributed under GNU GPL v3. Dependencies retain their own licenses.\n"
        "See LICENSE, licenses/, and the font license files under assets/.\n"
        "FreeType is used under the FreeType License (FTL.TXT).\n"
        "RecompFrontend is licensed under GPL v3; its upstream COPYING is included.\n\n"
        f"Corresponding project source, dependency pins, local patches and build scripts:\n"
        f"https://github.com/alondero/automobililamborghini-recomp/tree/v{version}\n"
        "Use git clone --recurse-submodules and follow BUILDING.md.\n"
        "The original USA game ROM is supplied by the player and is not included.\n",
        encoding="utf-8")
    print(f"Staged project license and {len(copied)} dependency notices")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--platform", choices=["windows", "linux"], required=True)
    args = parser.parse_args()
    stage(args.destination, args.platform)
