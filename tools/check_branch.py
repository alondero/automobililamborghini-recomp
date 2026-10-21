"""Check whitespace across a branch, optionally including uncommitted edits."""
import argparse
import subprocess
from pathlib import Path


def check(base, head="HEAD", *, worktree=False, cwd=None):
    def git(*args, **kwargs):
        return subprocess.run(["git", *args], cwd=cwd, check=True, **kwargs)

    # GitHub uses an all-zero before SHA when a branch is first created.
    if base and set(base) == {"0"}:
        start = git("hash-object", "-t", "tree", "--stdin", input=b"",
                    stdout=subprocess.PIPE).stdout.decode().strip()
    else:
        start = git("merge-base", base, head, stdout=subprocess.PIPE).stdout.decode().strip()
    git("diff", "--check", start, head, "--")
    if worktree:
        git("diff", "--check", "--cached", "--")
        git("diff", "--check", "--")
    return start


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="origin/main")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--worktree", action="store_true", help="Also check staged and unstaged edits")
    args = parser.parse_args()
    try:
        start = check(args.base, args.head, worktree=args.worktree,
                      cwd=Path(__file__).resolve().parents[1])
    except subprocess.CalledProcessError as error:
        return error.returncode
    print(f"Whitespace checks passed: {start}..{args.head}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
