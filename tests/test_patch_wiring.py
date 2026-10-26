"""Detect build/replay drift and dependency patches with no build owner."""
from pathlib import Path
import shutil
import tempfile
import unittest
from tools.check_patch_series import ROOT, validate


class PatchWiringChecks(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for path in ('build.ps1', 'build.sh', 'scripts/build_android.py',
                     'CMakeLists.txt', 'cmake/Frontend.cmake'):
            destination = self.root / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / path, destination)
        (self.root / 'patches').mkdir()
        for patch in (ROOT / 'patches').glob('*.patch'):
            (self.root / 'patches' / patch.name).touch()

    def test_reordered_linux_prefix_fails(self):
        path = self.root / 'build.sh'
        source = path.read_text(encoding='utf-8')
        source = source.replace('lib/rt64:0009-', 'lib/rt64:9999-', 1)
        path.write_text(source, encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'linux RT64 replay matrix'):
            validate(self.root)

    def test_missing_cmake_series_entry_fails(self):
        path = self.root / 'CMakeLists.txt'
        source = path.read_text(encoding='utf-8').replace(
            'lambo_frontend_patch("${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64" 0033-',
            'retired_patch("${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64" 0033-', 1)
        path.write_text(source, encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'CMake RT64 replay matrix'):
            validate(self.root)

    def test_new_patch_without_application_fails(self):
        (self.root / 'patches/9999-unwired.patch').touch()
        with self.assertRaisesRegex(ValueError, 'unwired=.*9999-unwired.patch'):
            validate(self.root)

    def test_android_composed_series_drift_fails(self):
        path = self.root / 'scripts/build_android.py'
        source = path.read_text(encoding='utf-8').replace('"0038", "0039", "0042"', '"0038", "0039"', 1)
        path.write_text(source, encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Android recognized CMake series'):
            validate(self.root)

    def test_removed_shared_patch_fails(self):
        (self.root / 'patches/0037-recompfrontend-optional-ui-debugger.patch').unlink()
        with self.assertRaisesRegex(ValueError, 'missing=.*0037'):
            validate(self.root)
