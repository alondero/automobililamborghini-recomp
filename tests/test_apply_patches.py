"""Dependency patch series: one manifest, one idempotent applier.

patches/series.txt is the only list of patches, and tools/apply_patches.py is
the only code that applies them. These fixtures use throwaway repositories, so
they need neither a ROM nor initialized submodules. The real series is
replayed in tests/test_patch_composability.py.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.apply_patches import PatchError, ROOT, apply_series, load_series, series_for


class ManifestTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="lambo-series-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "patches").mkdir()
        for name in ("0001-a.patch", "0002-b.patch", "0003-c.patch"):
            (self.root / "patches" / name).touch()

    def write(self, text):
        (self.root / "patches" / "series.txt").write_text(text, encoding="utf-8")

    def test_checked_in_series_lists_every_patch_once(self):
        load_series(ROOT)

    def test_platforms_filter_and_keep_order(self):
        self.write("[lib/a]\n0002-b.patch\n0001-a.patch windows\n"
                   "# comment\n[lib/b]\n0003-c.patch android linux  # trailing\n")
        self.assertEqual({d: [p.name for p in ps] for d, ps in series_for("windows", self.root).items()},
                         {"lib/a": ["0002-b.patch", "0001-a.patch"]})
        self.assertEqual({d: [p.name for p in ps] for d, ps in series_for("linux", self.root).items()},
                         {"lib/a": ["0002-b.patch"], "lib/b": ["0003-c.patch"]})

    def test_unlisted_patch_fails(self):
        self.write("[lib/a]\n0001-a.patch\n0002-b.patch\n")
        with self.assertRaisesRegex(PatchError, r"unlisted=\['0003-c.patch'\]"):
            load_series(self.root)

    def test_missing_patch_fails(self):
        self.write("[lib/a]\n0001-a.patch\n0002-b.patch\n0003-c.patch\n0004-gone.patch\n")
        with self.assertRaisesRegex(PatchError, r"missing=\['0004-gone.patch'\]"):
            load_series(self.root)

    def test_duplicate_patch_fails(self):
        self.write("[lib/a]\n0001-a.patch\n0002-b.patch\n0003-c.patch\n[lib/b]\n0001-a.patch\n")
        with self.assertRaisesRegex(PatchError, r"duplicates=\['0001-a.patch'\]"):
            load_series(self.root)

    def test_unknown_platform_fails(self):
        self.write("[lib/a]\n0001-a.patch macos\n0002-b.patch\n0003-c.patch\n")
        with self.assertRaisesRegex(PatchError, "series.txt:2: unknown platform macos"):
            load_series(self.root)

    def test_patch_before_directory_fails(self):
        self.write("0001-a.patch\n")
        with self.assertRaisesRegex(PatchError, "before any"):
            load_series(self.root)


class ApplySeriesTests(unittest.TestCase):
    """The second patch rewrites the line the first one added.

    That overlap is why reverse-checking one patch at a time could not tell an
    applied series from an unapplied one.
    """

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="lambo-apply-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
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
            patch = self.root / f"000{index}.patch"
            patch.write_bytes(self.git("diff"))
            self.patches.append(patch)
            self.git("add", ".")
        self.git("reset", "-q", "--hard", "HEAD")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.repo), *args], stderr=subprocess.STDOUT)

    def test_clean_then_repeat(self):
        self.assertEqual(apply_series(self.repo, self.patches), 2)
        self.assertEqual(apply_series(self.repo, self.patches), 0)
        self.assertEqual(self.file.read_text(), "android\n")
        self.assertEqual(self.git("diff", "--cached"), b"")

    def test_empty_series_is_a_no_op(self):
        self.assertEqual(apply_series(self.repo, []), 0)
        self.assertEqual(self.file.read_text(), "original\n")

    def test_applied_series_is_recognized_despite_overlap(self):
        apply_series(self.repo, self.patches)
        earlier = subprocess.run(["git", "-C", str(self.repo), "apply", "--reverse", "--check",
                                  str(self.patches[0])], capture_output=True)
        self.assertNotEqual(earlier.returncode, 0, "the fixture no longer overlaps")
        self.assertEqual(apply_series(self.repo, self.patches), 0)

    def test_partial_series_is_completed_and_unrelated_files_kept(self):
        self.git("apply", str(self.patches[0]))
        personal = self.repo / "personal.txt"
        personal.write_text("keep me\n")
        self.assertEqual(apply_series(self.repo, self.patches), 1)
        self.assertEqual(self.file.read_text(), "android\n")
        self.assertEqual(personal.read_text(), "keep me\n")
        self.assertEqual(self.git("diff", "--cached"), b"")

    def test_conflict_is_reported_without_modifying_index_or_files(self):
        self.file.write_text("my conflicting edit\n")
        self.git("add", ".")
        before = self.git("diff", "--cached")
        with self.assertRaisesRegex(PatchError, "does not match any prefix"):
            apply_series(self.repo, self.patches)
        self.assertEqual(self.file.read_text(), "my conflicting edit\n")
        self.assertEqual(self.git("diff", "--cached"), before)

    def test_broken_series_names_the_patch(self):
        broken = self.root / "broken.patch"
        shutil.copyfile(self.patches[0], broken)
        with self.assertRaisesRegex(PatchError, "broken.patch does not apply after"):
            apply_series(self.repo, [self.patches[0], broken])
        self.assertEqual(self.file.read_text(), "original\n")

    def test_user_diff_settings_do_not_change_generated_diffs(self):
        self.git("config", "diff.noprefix", "true")
        self.git("config", "diff.external", "false")
        self.git("apply", str(self.patches[0]))
        self.assertEqual(apply_series(self.repo, self.patches), 1)
        self.assertEqual(self.file.read_text(), "android\n")


if __name__ == "__main__":
    unittest.main()
