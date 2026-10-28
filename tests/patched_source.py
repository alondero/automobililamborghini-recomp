"""Replay the port's patched dependency sources for ROM-free host fixtures."""
import subprocess
from pathlib import Path
from tools.check_patch_series import PLUME_CMAKE_SERIES, resolve_patch, script_series

ROOT = Path(__file__).resolve().parents[1]
PLUME = ROOT / "lib/rt64/src/contrib/plume"


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def patched_file(submodule, path, tree):
    """Return a pinned submodule file with every port patch that edits it.

    Plume replays the platform script prefixes and the checked shared CMake
    suffix. Other fixtures use number order for their independent file hunks.
    Application is strict, like shared CMake, so whitespace drift is an error.
    """
    # Replay the same source as the build without mutating the user's checkout.
    target = Path(tree) / path
    target.parent.mkdir(parents=True, exist_ok=True)
    exists = subprocess.check_output(["git", "ls-tree", "--name-only", "HEAD", "--", path], cwd=submodule)
    if exists:
        source = subprocess.check_output(["git", "show", f"HEAD:{path}"], cwd=submodule, text=True)
        target.write_bytes(source.encode("utf-8"))
    else:
        # Some fixture callers are introduced by the checked-in patch series.
        target.unlink(missing_ok=True)
    if submodule.resolve() == PLUME.resolve():
        directory = "lib/rt64/src/contrib/plume"
        prefixes = dict.fromkeys(name for platform in ("windows", "linux", "android")
                                 for name in script_series(ROOT, platform).get(directory, []))
        patches = [ROOT / "patches" / name for name in prefixes]
        patches += [resolve_patch(ROOT, number) for number in PLUME_CMAKE_SERIES]
    else:
        patches = sorted((ROOT / "patches").glob("*.patch"))
    for patch in patches:
        if f"+++ b/{path}\n" in patch.read_text(encoding="utf-8"):
            command = ["git", "apply", f"--include={path}", str(patch)]
            result = subprocess.run(command, cwd=tree, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(f"Patch replay exited {result.returncode}: {subprocess.list2cmdline(command)}\n"
                                   f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return target.read_text(encoding="utf-8")


def patched_plume_vulkan(tree):
    return patched_file(PLUME, "plume_vulkan.cpp", tree)
