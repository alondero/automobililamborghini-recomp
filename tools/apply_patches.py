#!/usr/bin/env python3
"""Apply the dependency patch series listed in patches/series.txt.

Every build path calls this tool: CMake configure, build.sh, build.ps1 and
scripts/build_android.py. Repeated runs are safe. A dependency is brought to
the end of its series without resetting anything; local edits that conflict
with the series stop the build instead of being overwritten.
"""
import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ("windows", "linux", "android")
# Composition is checked strictly against the pinned blobs. Working trees may
# hold CRLF checkouts, so whitespace-only context differences are tolerated.
WORKTREE_FLAGS = ("--ignore-whitespace",)
# User diff settings must not change the diffs this tool generates and applies.
GIT_DIFF = ("-c", "diff.noprefix=false", "-c", "diff.mnemonicPrefix=false",
            "diff", "--no-color", "--no-ext-diff", "--binary")


class PatchError(RuntimeError):
    pass


@dataclass(frozen=True)
class Entry:
    directory: str
    patch: Path
    platforms: tuple[str, ...]


def load_series(root=ROOT):
    """Parse and validate series.txt; every patch file must be listed once."""
    manifest = root / "patches" / "series.txt"
    entries, directory = [], None
    for number, raw in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        where = f"{manifest.name}:{number}"
        if line.startswith("["):
            if not line.endswith("]"):
                raise PatchError(f"{where}: malformed directory header {line!r}")
            directory = line[1:-1].strip()
            continue
        if directory is None:
            raise PatchError(f"{where}: patch listed before any [directory]")
        name, *platforms = line.split()
        unknown = sorted(set(platforms) - set(PLATFORMS))
        if unknown:
            raise PatchError(f"{where}: unknown platform {', '.join(unknown)}")
        entries.append(Entry(directory, root / "patches" / name, tuple(platforms or PLATFORMS)))
    listed = [entry.patch.name for entry in entries]
    duplicates = sorted({name for name in listed if listed.count(name) > 1})
    on_disk = {path.name for path in (root / "patches").glob("*.patch")}
    unlisted, missing = sorted(on_disk - set(listed)), sorted(set(listed) - on_disk)
    if duplicates or unlisted or missing:
        raise PatchError(f"{manifest.name} does not match patches/: duplicates={duplicates}, "
                         f"unlisted={unlisted}, missing={missing}")
    return entries


def series_for(platform, root=ROOT):
    """Return {directory: [patch paths in order]} for one platform."""
    if platform not in PLATFORMS:
        raise PatchError(f"unknown platform {platform}")
    series = {}
    for entry in load_series(root):
        if platform in entry.platforms:
            series.setdefault(entry.directory, []).append(entry.patch)
    return series


def apply_series(directory, patches, git="git"):
    """Bring a checkout to the end of an ordered patch series.

    Later patches may rewrite lines an earlier patch added, so an applied
    patch cannot always be recognized by reverse-checking it alone. Instead,
    each cumulative state of the series is built in a temporary index, and the
    longest prefix the checkout contains as a whole is kept. The remainder is
    applied as one diff, so a failure leaves the files untouched. The user's
    index and unrelated files are never modified. Returns the number of
    patches applied.
    """
    if not patches:
        return 0
    directory = Path(directory)
    repo = [git, "-C", str(directory)]
    with tempfile.TemporaryDirectory(prefix="lambo-patches-") as temporary:
        index_env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / "index"))
        diff = Path(temporary) / "series.patch"

        def compose(*steps):
            """Return the tree after the base and after each group of patches."""
            subprocess.run(repo + ["read-tree", "HEAD"], env=index_env, check=True)
            trees = [subprocess.check_output(repo + ["write-tree"], env=index_env, text=True).strip()]
            for step in steps:
                result = subprocess.run(repo + ["apply", "--cached", *map(str, step)],
                                        env=index_env, capture_output=True, text=True)
                if result.returncode:
                    raise PatchError(f"{step[-1].name} does not apply after the patches before it "
                                     f"in {directory}:\n{result.stderr}")
                trees.append(subprocess.check_output(repo + ["write-tree"], env=index_env, text=True).strip())
            return trees

        def git_apply(start, end, *flags):
            diff.write_bytes(subprocess.check_output(repo + [*GIT_DIFF, trees[start], trees[end]]))
            return subprocess.run(repo + ["apply", *WORKTREE_FLAGS, *flags, str(diff)],
                                  capture_output=True, text=True).returncode == 0

        # Fast path for repeated configures: one composition, one check.
        try:
            trees = compose(patches)
            if git_apply(0, 1, "--reverse", "--check"):
                return 0
        except PatchError:
            pass  # The per-patch composition below names the patch that fails.
        trees = compose(*([patch] for patch in patches))
        for applied in range(len(trees) - 1, -1, -1):
            if applied and not git_apply(0, applied, "--reverse", "--check"):
                continue
            if applied == len(patches):
                return 0
            # A present prefix whose remainder cannot apply is a damaged later
            # state, not an earlier valid one; keep looking for a shorter prefix.
            if git_apply(applied, len(patches), "--check"):
                if not git_apply(applied, len(patches)):
                    raise PatchError(f"git apply failed in {directory} after a clean --check")
                return len(patches) - applied
    raise PatchError(
        f"{directory} does not match any prefix of its patch series. It has local edits "
        "that conflict with the series, or it was patched for another platform or an "
        "older series. Preserve any work, then reset it, for example:\n"
        f"  git -C {directory} checkout -- . && git -C {directory} clean -fd")


def apply_platform(platform, root=ROOT, git="git", reset=False):
    for directory, patches in series_for(platform, root).items():
        path = root / directory
        if not (path / ".git").exists():
            raise PatchError(f"{directory} is not checked out; initialize it before applying patches")
        if reset:
            # Nested submodules are left to `git submodule update`.
            subprocess.run([git, "-C", str(path), "checkout", "-q", "--", "."], check=True)
            subprocess.run([git, "-C", str(path), "clean", "-fdq"], check=True)
        count = apply_series(path, patches, git)
        state = f"applied {count} of {len(patches)}" if count else f"all {len(patches)} present"
        print(f"  {directory}: {state}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=PLATFORMS,
                        help="apply this platform's series; without it, only validate series.txt")
    parser.add_argument("--reset", action="store_true",
                        help="discard tracked and untracked changes in each series directory first")
    parser.add_argument("--git", default="git", help="git executable to run")
    args = parser.parse_args()
    if args.reset and not args.platform:
        parser.error("--reset needs --platform")
    try:
        load_series()
        if args.platform:
            apply_platform(args.platform, git=args.git, reset=args.reset)
        else:
            print("patches/series.txt lists every patch once.")
    except (PatchError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
