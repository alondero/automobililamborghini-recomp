"""Patch-series regression: desktop prefixes and local edits must survive Android setup."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_android", ROOT / "scripts/build_android.py")
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)


class PatchSeriesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="lambo-android-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git("init", "-q")
        self.git("config", "core.autocrlf", "false")
        self.file = self.repo / "build.txt"
        self.file.write_text("original\n")
        self.git("add", ".")
        self.git("-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qm", "base")
        self.patches = []
        for index, value in enumerate(("desktop\n", "android\n")):
            self.file.write_text(value)
            patch = self.root / f"{index}.patch"
            patch.write_bytes(self.git("diff"))
            self.patches.append(patch)
            self.git("add", ".")
        self.git("reset", "--hard", "HEAD")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.repo), *args], stderr=subprocess.STDOUT)

    def test_clean_then_repeat(self):
        android.apply_patch_series(self.repo, self.patches)
        android.apply_patch_series(self.repo, self.patches)
        self.assertEqual(self.file.read_text(), "android\n")
        self.assertEqual(self.git("diff", "--cached"), b"")

    def test_desktop_prefix_and_unrelated_edit(self):
        self.git("apply", str(self.patches[0]))
        personal = self.repo / "personal.txt"
        personal.write_text("keep me\n")
        android.apply_patch_series(self.repo, self.patches)
        self.assertEqual(self.file.read_text(), "android\n")
        self.assertEqual(personal.read_text(), "keep me\n")
        self.assertEqual(self.git("diff", "--cached"), b"")

    def test_conflict_is_reported_without_modifying_index_or_files(self):
        self.file.write_text("my conflicting edit\n")
        self.git("add", ".")
        before = self.git("diff", "--cached")
        with self.assertRaises(RuntimeError):
            android.apply_patch_series(self.repo, self.patches)
        self.assertEqual(self.file.read_text(), "my conflicting edit\n")
        self.assertEqual(self.git("diff", "--cached"), before)


if __name__ == "__main__":
    unittest.main()
