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


class AssetLayoutTests(unittest.TestCase):
    """The APK assets tree must mirror the game runtime assets tree.

    The game resolves UI files as "<program>/assets/<name>" and the launcher
    stages APK assets/ into files/assets/ verbatim (it even expects
    files/assets/LatoLatin-Regular.ttf afterwards). An extra nesting level in
    the APK hides the fonts from the game and aborts startup on device with
    an uncaught exception after failed font loads.
    """

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="lambo-assets-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        native_assets = self.root / "native" / "assets"
        (native_assets / "promptfont").mkdir(parents=True)
        (native_assets / "LatoLatin-Regular.ttf").write_text("font")
        (native_assets / "recomp.rcss").write_text("style")
        (native_assets / "promptfont" / "promptfont.ttf").write_text("font")
        self.root.joinpath("lamborghini.syms.toml").write_text("symbols")
        self.root.joinpath("LICENSE").write_text("license")
        notices = {"SDL.txt": self.root / "SDL-LICENSE.txt"}
        notices["SDL.txt"].write_text("sdl license")
        android.stage_package_assets(
            self.root / "assets", native_assets, self.root, notices
        )
        self.assets = self.root / "assets"

    def test_runtime_files_land_flat(self):
        for name in android.REQUIRED_RUNTIME_ASSETS:
            self.assertTrue(
                (self.assets / name).is_file(), f"missing runtime asset {name}"
            )

    def test_no_extra_nesting_level(self):
        self.assertFalse(
            (self.assets / "assets").exists(),
            "APK assets must not nest the runtime tree one level deeper",
        )

    def test_launcher_sidecar_files_present(self):
        self.assertTrue((self.assets / "lamborghini.syms.toml").is_file())
        self.assertTrue((self.assets / "LICENSE").is_file())
        self.assertTrue((self.assets / "licenses" / "SDL.txt").is_file())

    def test_launcher_expectation_matches_layout(self):
        launcher = (
            ROOT
            / "android/app/src/main/java/io/github/alondero/lamborghinirecomp/LauncherActivity.java"
        )
        staged = launcher.read_text(encoding="utf-8")
        self.assertIn(
            '"assets/LatoLatin-Regular.ttf"',
            staged,
            "launcher must expect the flat staged font path",
        )


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
