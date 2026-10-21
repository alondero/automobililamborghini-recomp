"""Check whitespace across a branch, optionally including uncommitted edits."""
import argparse
import json
import subprocess
from pathlib import Path


def github_base(event):
    if 'pull_request' in event:
        return event['pull_request']['base']['sha']
    before = event.get('before', '')
    if before and set(before) != {'0'} and not event.get('forced'):
        return before
    return 'origin/' + event['repository']['default_branch']


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
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--base", default="origin/main")
    group.add_argument("--event-file", type=Path, help="Select the base from a GitHub push or pull-request event")
    parser.add_argument("--head", default="HEAD")
    parser.add_argument("--worktree", action="store_true", help="Also check staged and unstaged edits")
    args = parser.parse_args()
    base = github_base(json.loads(args.event_file.read_text(encoding='utf-8'))) if args.event_file else args.base
    try:
        start = check(base, args.head, worktree=args.worktree,
                      cwd=Path(__file__).resolve().parents[1])
    except subprocess.CalledProcessError as error:
        return error.returncode
    print(f"Whitespace checks passed: {start}..{args.head}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
