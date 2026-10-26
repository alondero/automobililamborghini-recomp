"""Behavioural test for the Android touch-control state machine.

TouchControls.java has no Android dependency, so it is compiled with the host
javac and exercised by tests/android/TouchControlsTest.java. Skipped when no
JDK is installed.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "android/app/src/main/java/io/github/alondero/lamborghinirecomp/TouchControls.java"
TEST = ROOT / "tests/android/TouchControlsTest.java"


@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "JDK not installed")
class TouchControlsTests(unittest.TestCase):
    def test_pointer_ownership_and_stick(self):
        with tempfile.TemporaryDirectory() as out:
            subprocess.run(["javac", "-d", out, str(SRC), str(TEST)], check=True)
            result = subprocess.run(
                ["java", "-cp", out, "io.github.alondero.lamborghinirecomp.TouchControlsTest"],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
