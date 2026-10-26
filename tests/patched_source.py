"""Replay the port's patched dependency sources for ROM-free host fixtures."""
import subprocess
from pathlib import Path

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

    Patches apply in number order, which is their build order for every file
    the fixtures read.
    """
    # Replay the same source as the build without mutating the user's checkout.
    source = subprocess.check_output(["git", "show", f"HEAD:{path}"], cwd=submodule, text=True)
    target = Path(tree) / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(source.encode("utf-8"))
    for patch in sorted((ROOT / "patches").glob("*.patch")):
        if f"+++ b/{path}\n" in patch.read_text(encoding="utf-8"):
            subprocess.run(["git", "apply", "--ignore-whitespace", f"--include={path}", str(patch)],
                           cwd=tree, check=True, capture_output=True)
    return target.read_text(encoding="utf-8")


def patched_plume_vulkan(tree):
    return patched_file(PLUME, "plume_vulkan.cpp", tree)
