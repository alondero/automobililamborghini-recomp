"""Compare dependency worktrees with HEAD plus the committed patch series.

Uses disposable Git indices; leaves source files and the user's index alone.
Run after the normal platform patch application. --check compares the resulting
record with committed evidence; --output writes a fresh record after success.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.apply_patches import PLATFORMS, ROOT, PatchError, series_for

COMPARISON = ("Temporary Git index: pinned HEAD plus ordered committed patches, "
              "compared with a separate index of tracked and untracked source files.")


def verify_dependency(directory, patches):
    directory = Path(directory)
    if not (directory / ".git").exists():
        raise ValueError(f"dependency is not initialized: {directory}")

    def git(*args, env=None):
        return subprocess.check_output(["git", "-C", str(directory), *args], env=env, text=True).strip()

    with tempfile.TemporaryDirectory(prefix="lambo-verify-deps-") as temporary:
        expected_env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / "expected"))
        actual_env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / "actual"))
        git("read-tree", "HEAD", env=expected_env)
        for patch in patches:
            git("apply", "--cached", "--whitespace=nowarn", str(patch), env=expected_env)
        expected = git("write-tree", env=expected_env)
        git("read-tree", "HEAD", env=actual_env)
        git("add", "-A", "--", ".", env=actual_env)
        actual = git("write-tree", env=actual_env)
    return {"pin": git("rev-parse", "HEAD"), "patch_count": len(patches),
            "patched_tree": expected, "worktree_matches": expected == actual}


def dependency_report(platform, root=ROOT):
    dependencies = []
    for relative, patches in series_for(platform, root).items():
        dependencies.append({"directory": relative,
                             **verify_dependency(root / relative, patches)})
    return {"platform": platform, "comparison": COMPARISON, "dependencies": dependencies}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=PLATFORMS, required=True)
    parser.add_argument("--check", type=Path, help="compare with an existing JSON evidence record")
    parser.add_argument("--output", type=Path, help="write JSON evidence after verification succeeds")
    args = parser.parse_args(argv)
    try:
        report = dependency_report(args.platform)
        for dependency in report["dependencies"]:
            state = "MATCH" if dependency["worktree_matches"] else "MISMATCH"
            print(f"{dependency['directory']}: {state} {dependency['patched_tree']}")
        if not all(item["worktree_matches"] for item in report["dependencies"]):
            return 1
        if args.check and json.loads(args.check.read_text(encoding="utf-8")) != report:
            print(f"evidence differs: {args.check}", file=sys.stderr)
            return 1
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    except (OSError, ValueError, PatchError, subprocess.CalledProcessError) as error:
        print(f"dependency verification failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
