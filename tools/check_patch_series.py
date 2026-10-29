"""Validate the RT64 replay matrix and patch wiring against the build inputs."""
import argparse
import ast
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SCRIPT_SERIES = {
    "windows": ["0006", "0005", "0009", "0010", "0011"],
    "linux": ["0006", "0009", "0010", "0011"],
    "android": ["0006", "0009", "0010", "0011", "0013"],
}
CMAKE_SERIES = ["0021", "0022", "0024", "0025", "0026", "0027", "0029", "0030", "0033", "0038", "0039", "0042", "0046", "0056", "0058", "0059", "0060"]
PLUME_CMAKE_SERIES = ["0023", "0028", "0041", "0043", "0044", "0047", "0049", "0050", "0051", "0052", "0053", "0055"]


def resolve_patch(root, number):
    matches = list((root / "patches").glob(f"{number}-*.patch"))
    if len(matches) != 1:
        raise ValueError(f"expected one patch for {number}: {matches}")
    return matches[0]


def android_series(root, name):
    tree = ast.parse((root / "scripts/build_android.py").read_text(encoding="utf-8"))
    return next(ast.literal_eval(node.value) for node in tree.body
                if isinstance(node, ast.Assign)
                and any(isinstance(target, ast.Name) and target.id == name
                        for target in node.targets))


def script_series(root, platform):
    if platform == "android":
        numbers = android_series(root, "PATCHES")
        return {directory: [resolve_patch(root, number).name for number in series]
                for directory, series in numbers.items()}
    if platform == "windows":
        source = (root / "build.ps1").read_text(encoding="utf-8")
        block = re.search(r"\$patches = @\((.*?)\n    \)", source, re.S).group(1)
        entries = re.findall(r"Sub = '([^']+)';\s*Patch = 'patches/([^']+)'", block)
    else:
        source = (root / "build.sh").read_text(encoding="utf-8")
        block = re.search(r"\nPATCHES=\((.*?)\n\)", source, re.S).group(1)
        entries = re.findall(r'"([^":]+):([^"\n]+)"', block)
    result = {}
    for directory, name in entries:
        result.setdefault(directory, []).append(name)
    return result


def validate(root=ROOT):
    wired = set()
    for platform, expected in SCRIPT_SERIES.items():
        actual = script_series(root, platform)
        numbers = [name[:4] for name in actual.get("lib/rt64", [])]
        if numbers != expected:
            raise ValueError(f"{platform} RT64 replay matrix {expected} != build order {numbers}")
        wired.update(name for names in actual.values() for name in names)
    source = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    actual = re.findall(r'lambo_frontend_patch\("\$\{CMAKE_CURRENT_SOURCE_DIR\}/lib/rt64"\s+(\d{4})-[^\s)]+\)', source)
    if actual != CMAKE_SERIES:
        raise ValueError(f"CMake RT64 replay matrix {CMAKE_SERIES} != build order {actual}")
    plume = re.findall(r'lambo_frontend_patch\("\$\{CMAKE_CURRENT_SOURCE_DIR\}/lib/rt64/src/contrib/plume"\s+(\d{4})-[^\s)]+\)', source)
    if plume != PLUME_CMAKE_SERIES:
        raise ValueError(f"CMake Plume replay matrix {PLUME_CMAKE_SERIES} != build order {plume}")
    recognized = android_series(root, "CMAKE_PATCHES").get("lib/rt64", [])
    if recognized != actual:
        raise ValueError(f"Android recognized CMake series {recognized} != build order {actual}")
    for path in (root / "CMakeLists.txt", root / "cmake/Frontend.cmake"):
        wired.update(re.findall(r'lambo_frontend_patch\("[^"\n]+"\s+([^\s)]+\.patch)\)',
                                path.read_text(encoding="utf-8")))
    # SDL is fetched separately by Android rather than being a port submodule.
    wired.update(resolve_patch(root, number).name for number in android_series(root, "SDL_PATCHES"))
    patches = {path.name for path in (root / "patches").glob("*.patch")}
    if patches != wired:
        raise ValueError(f"patch wiring mismatch: unwired={sorted(patches - wired)}, missing={sorted(wired - patches)}")


def apply_script(platform, root=ROOT):
    """Apply only the script prefix; CMake owns the remaining shared patches."""
    for directory, names in script_series(root, platform).items():
        for name in names:
            command = ["git", "-C", str(root / directory), "apply", "--ignore-whitespace"]
            patch = str(root / "patches" / name)
            if subprocess.run(command + ["--reverse", "--check", patch],
                              capture_output=True).returncode == 0:
                continue
            subprocess.run(command + ["--check", patch], check=True)
            subprocess.run(command + [patch], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply-script", choices=SCRIPT_SERIES,
                        help="Apply a platform's script prefix without needing a ROM")
    args = parser.parse_args()
    try:
        validate()
        if args.apply_script:
            apply_script(args.apply_script)
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")
    print("Patch wiring and replay matrix match the build inputs.")


if __name__ == "__main__":
    main()
