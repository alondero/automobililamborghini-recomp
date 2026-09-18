"""Run repository-local documentation checks without a ROM or build tree."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
IGNORED_MARKDOWN_PARTS = {
    ".git",
    ".claude",
    ".venv",
    "build",
    "build-android",
    "dist",
    "emulators",
    "env",
    "lib",
    "node_modules",
    "venv",
}
MARKDOWN = sorted(
    path
    for path in ROOT.rglob("*.md")
    if not IGNORED_MARKDOWN_PARTS
    & set(path.relative_to(ROOT).parts)
)

LOCAL_LINK = re.compile(r"\[[^\]]*\]\(([^)\n]+)\)")
HEADING = re.compile(r"^\s{0,3}#{1,6}\s+(.+?)\s*#*\s*$")
HTML_ANCHOR = re.compile(r"""(?:id|name)\s*=\s*["']([^"']+)["']""", re.IGNORECASE)
SCRIPT_REFERENCE = re.compile(
    r"(?<![\w./-])((?:tools|scripts|tests)/[\w./-]+\.(?:py|ps1|sh|bat|cmd))"
    r"|(?<![\w./-])((?:build\.sh|build\.ps1))"
)
TEST_TOKENS = (
    "ctest",
    "run_game_scenario.py",
    "build.sh",
    "build.ps1",
    "unittest",
)


def display(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def report(errors: list[str], path: Path, line: int, message: str) -> None:
    errors.append(f"{display(path)}:{line}: {message}")


def is_external(target: str) -> bool:
    lowered = target.lower()
    return (
        lowered.startswith(("http://", "https://", "mailto:", "ftp://"))
        or lowered.startswith("#")
    )


def anchor_slug(value: str) -> str:
    value = re.sub(r"<[^>]+>", "", value)
    value = re.sub(r"[^\w\s-]", "", value, flags=re.UNICODE)
    return re.sub(r"\s+", "-", value.strip().lower())


def local_anchors(path: Path) -> set[str]:
    anchors: set[str] = set()
    occurrences: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        for match in HTML_ANCHOR.finditer(line):
            anchors.add(match.group(1).strip().lower())
        heading = HEADING.match(line)
        if not heading:
            continue
        slug = anchor_slug(heading.group(1))
        if not slug:
            continue
        suffix = occurrences.get(slug, 0)
        anchors.add(slug if suffix == 0 else f"{slug}-{suffix}")
        occurrences[slug] = suffix + 1
    return anchors


def check_links(errors: list[str]) -> None:
    for path in MARKDOWN:
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_number, line in enumerate(text.splitlines(), 1):
            for match in LOCAL_LINK.finditer(line):
                target = match.group(1).strip().strip("<>")
                if is_external(target):
                    continue
                path_target, separator, fragment = target.partition("#")
                path_target = path_target.split("?", 1)[0].strip()
                if not path_target:
                    continue
                candidate = (path.parent / path_target).resolve()
                if candidate.is_dir():
                    candidate = next(
                        (
                            child
                            for child in (candidate / "README.md", candidate / "index.md")
                            if child.is_file()
                        ),
                        candidate,
                    )
                if not candidate.is_file():
                    report(errors, path, line_number, f"broken local link: {path_target}")
                elif separator and fragment:
                    wanted = unquote(fragment).strip().lower()
                    if wanted not in local_anchors(candidate):
                        report(
                            errors,
                            path,
                            line_number,
                            f"broken local anchor: {fragment}",
                        )


def check_scripts(errors: list[str]) -> None:
    for path in MARKDOWN:
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_number, line in enumerate(text.splitlines(), 1):
            for match in SCRIPT_REFERENCE.finditer(line):
                target = next(group for group in match.groups() if group)
                if not (ROOT / target).is_file():
                    report(errors, path, line_number, f"missing referenced script: {target}")


def check_forbidden_text(errors: list[str]) -> None:
    patterns = (
        (
            re.compile(
                r"(?i)(?:claude\.ai/share|chat\.openai\.com/share|chatgpt\.com/share|"
                r"app://|\.claude[\\/]+worktrees)"
            ),
            "private session or machine-local reference",
        ),
        (
            re.compile(
                r"(?i)(?:https?://)?(?:example\.(?:com|org|net)|TODO_URL|"
                r"PLACEHOLDER_URL|your-url|replace-with-your)"
            ),
            "placeholder URL",
        ),
        (
            re.compile(
                r"(?<!https:)(?<!http:)(?<![\w.])(?:[A-Za-z]:[\\/](?:Users|src|home|"
                r"workspace|worktrees|Program Files|ProgramData|tmp|var)|"
                r"/(?:Users|home|workspace|worktrees|mnt|src)/)"
            ),
            "absolute machine-specific path",
        ),
    )
    for path in MARKDOWN:
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_number, line in enumerate(text.splitlines(), 1):
            for pattern, label in patterns:
                if pattern.search(line):
                    report(errors, path, line_number, label)


def check_test_documentation(errors: list[str]) -> None:
    testing = ROOT / "docs" / "testing.md"
    if not testing.is_file():
        errors.append("docs/testing.md is required")
        return
    testing_text = testing.read_text(encoding="utf-8", errors="replace")
    for path in MARKDOWN:
        if path == testing:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for token in TEST_TOKENS:
            if token in text and token not in testing_text:
                errors.append(
                    f"{display(path)} uses test command token {token!r} "
                    "that is not documented in docs/testing.md"
                )


def main() -> int:
    errors: list[str] = []
    check_links(errors)
    check_scripts(errors)
    check_forbidden_text(errors)
    check_test_documentation(errors)
    if errors:
        print("Documentation checks failed:")
        print("\n".join(f"- {error}" for error in errors))
        return 1
    print(f"Documentation checks passed for {len(MARKDOWN)} Markdown files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
