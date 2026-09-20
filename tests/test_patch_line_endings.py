"""Patch-stack regression: checked-in patches must survive a CRLF checkout.

Background: the Windows release build checks out this repo with
``core.autocrlf`` enabled, so every ``patches/*.patch`` file gains CRLF line
endings. A blank context line stored as a bare empty line becomes a lone
carriage return after that conversion, and ``git apply`` rejects the patch
as corrupt ("corrupt patch at line N"). Patch 0011 carried 27 such lines and
broke every Windows release build while Linux (LF checkout) stayed green.

The tests below lock in the fix: git's own encoding for a blank context
line is a single space, and the checked-in patch stack must not contain bare
empty lines. See patches/README.md for the patch inventory.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCHES = sorted((ROOT / "patches").glob("*.patch"))


def bare_empty_lines(raw: bytes):
    """Return 1-based numbers of bare empty lines, excluding the EOF artifact."""
    lines = raw.split(b"\n")
    return [
        number
        for number, line in enumerate(lines, 1)
        if line == b"" and number != len(lines)
    ]


class PatchLineEndingTests(unittest.TestCase):
    def test_no_bare_empty_lines_in_patches(self):
        offenders = []
        for patch in PATCHES:
            for number in bare_empty_lines(patch.read_bytes()):
                offenders.append(f"{patch.name}:{number}")
        self.assertEqual(
            offenders,
            [],
            "bare empty lines corrupt the patch after a CRLF checkout; use "
            "git's single-space blank context line instead: "
            + ", ".join(offenders),
        )

    def test_proper_blank_context_survives_crlf_checkout(self):
        # Reproduce the Windows CI condition hermetically: a patch with
        # single-space blank context lines must still apply after every LF
        # becomes CRLF, exactly as actions/checkout delivers it.
        with tempfile.TemporaryDirectory(prefix="lambo-patch-eol-") as tmp:
            repo = Path(tmp) / "repo"
            repo.mkdir()
            self.git(repo, "init", "-q")
            self.git(repo, "config", "core.autocrlf", "false")
            self.git(repo, "config", "user.email", "test@example.invalid")
            self.git(repo, "config", "user.name", "Test")
            target = repo / "file.txt"
            target.write_bytes(b"alpha\n\nbeta\n\nomega\n")
            self.git(repo, "add", ".")
            self.git(repo, "commit", "-qm", "base")
            target.write_bytes(b"alpha\n\nBETA\n\nomega\n")
            patch = Path(tmp) / "change.patch"
            patch.write_bytes(self.git(repo, "diff"))
            self.assertIn(b"\n \n", patch.read_bytes())
            crlf = Path(tmp) / "change-crlf.patch"
            crlf.write_bytes(
                patch.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
            )
            self.git(repo, "checkout", "--", ".")
            self.git(repo, "apply", "--ignore-whitespace", "--check", str(crlf))
            self.git(repo, "apply", "--ignore-whitespace", str(crlf))
            # A CRLF patch writes CRLF into its added lines; what matters is
            # that the change itself landed.
            applied = target.read_bytes().replace(b"\r\n", b"\n")
            self.assertEqual(applied, b"alpha\n\nBETA\n\nomega\n")

    def git(self, repo: Path, *args):
        return subprocess.check_output(
            ["git", "-C", str(repo), *args], stderr=subprocess.STDOUT
        )


if __name__ == "__main__":
    unittest.main()
