"""A release's notices must not collect dependency code, ROMs or private files."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("release_notices", Path(__file__).resolve().parents[1] / "scripts/stage_release_notices.py")
notices = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(notices)


class ReleaseNoticeTests(unittest.TestCase):
    def test_android_does_not_probe_host_compiler(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "lib").mkdir()
            (root / "assets/licenses").mkdir(parents=True)
            (root / "LICENSE").write_text("project GPL")
            (root / "CMakeLists.txt").write_text("project(lamborghini_modern VERSION 0.11.0)")
            with patch.object(notices, "ROOT", root), patch.object(notices.shutil, "which", side_effect=AssertionError), patch.object(notices.subprocess, "check_output", side_effect=AssertionError):
                notices.stage(root / "apk-assets", "android")
            self.assertTrue((root / "apk-assets/LICENSE").is_file())
            self.assertTrue((root / "apk-assets/NOTICE.txt").is_file())

    def test_copies_legal_text_and_requires_runtime_notice(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, text in {
                "LICENSE": "project GPL", "CMakeLists.txt": "project(lamborghini_modern VERSION 0.11.0)",
                "lib/example/LICENSE.txt": "dependency MIT", "lib/example/source.cpp": "code",
                "lib/example/game.z64": "private", "lib/example/.git/NOTICE.txt": "git private",
                "assets/licenses/FTL.TXT": "FreeType license", "compiler/bin/gcc.exe": "compiler",
                "compiler/licenses/gcc/COPYING.RUNTIME": "exception",
                "compiler/licenses/gcc/COPYING3": "GPL", "compiler/licenses/winpthreads/COPYING": "MIT",
                "compiler/licenses/mingw-w64/COPYING": "runtime",
            }.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            destination = root / "payload"
            with patch.object(notices, "ROOT", root), patch.object(notices.shutil, "which", return_value=str(root / "compiler/bin/gcc.exe")):
                notices.stage(destination, "windows")
                self.assertEqual((destination / "licenses/lib/example/LICENSE.txt").read_text(), "dependency MIT")
                self.assertEqual((destination / "licenses/upstream/FTL.TXT").read_text(), "FreeType license")
                self.assertFalse(list(destination.rglob("*.z64")))
                self.assertFalse(list(destination.rglob("*.cpp")))
                self.assertFalse(any(".git" in p.parts for p in destination.rglob("*")))
                (root / "compiler/licenses/gcc/COPYING.RUNTIME").unlink()
                with self.assertRaisesRegex(RuntimeError, "Missing compiler runtime notice"):
                    notices.stage(root / "missing-notice", "windows")


if __name__ == "__main__":
    unittest.main()
