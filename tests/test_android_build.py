"""Patch-series regression: desktop prefixes and local edits must survive Android setup."""
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_android", ROOT / "scripts/build_android.py")
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)


class AssetLayoutTests(unittest.TestCase):
    """Model AssetManager's root and the launcher's actual staging graph.

    The device assetcheck additionally executes the Java extraction code
    against the packaged AssetManager, without a ROM or existing app assets.
    """

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="lambo-assets-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        native_assets = self.root / "native" / "assets"
        (native_assets / "promptfont").mkdir(parents=True)
        (native_assets / "LatoLatin-Regular.ttf").write_text("font")
        (native_assets / "LatoLatin-Bold.ttf").write_text("bold")
        (native_assets / "NotoEmoji-Regular.ttf").write_text("emoji")
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

    def test_runtime_files_keep_assetmanager_subdirectory(self):
        for name in android.REQUIRED_RUNTIME_ASSETS:
            self.assertTrue(
                (self.assets / "assets" / name).is_file(), f"missing runtime asset {name}"
            )

    def test_launcher_sidecar_files_present(self):
        self.assertTrue((self.assets / "lamborghini.syms.toml").is_file())
        self.assertTrue((self.assets / "LICENSE").is_file())
        self.assertTrue((self.assets / "licenses" / "SDL.txt").is_file())

    def test_launcher_staging_graph(self):
        launcher = (
            ROOT
            / "android/app/src/main/java/io/github/alondero/lamborghinirecomp/LauncherActivity.java"
        )
        source = launcher.read_text(encoding="utf-8")
        paths = re.findall(r'copyAsset\("([^"]+)", stage\)', source)
        self.assertEqual(set(paths), {"assets", "lamborghini.syms.toml"})
        stage = self.root / "extracted"

        def copy_asset(path):
            # Gradle's assets.srcDir is AssetManager root; the APK zip's
            # outer assets/ prefix is not part of an AssetManager path.
            bundled = self.assets / path
            destination = stage / path
            children = list(bundled.iterdir()) if bundled.is_dir() else []
            if children:
                destination.mkdir(parents=True, exist_ok=True)
                for child in children:
                    copy_asset(path + "/" + child.name)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                # Like AssetManager.open, raises if path is not a file.
                shutil.copyfile(bundled, destination)

        for path in paths:
            copy_asset(path)
        for name in android.REQUIRED_RUNTIME_ASSETS:
            self.assertEqual((stage / "assets" / name).read_bytes(),
                             (self.assets / "assets" / name).read_bytes())
        self.assertEqual((stage / "lamborghini.syms.toml").read_text(), "symbols")
        self.assertFalse((stage / "assets" / "assets").exists())


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

    def test_following_patches_are_recognized_but_left_for_cmake(self):
        prefix, following = self.patches
        android.apply_patch_series(self.repo, [prefix], [following])
        self.assertEqual(self.file.read_text(), "desktop\n")
        self.git("apply", str(following))
        android.apply_patch_series(self.repo, [prefix], [following])
        self.assertEqual(self.file.read_text(), "android\n")
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
