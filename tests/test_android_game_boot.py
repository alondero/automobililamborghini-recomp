"""Exercise the instrumentation's real polling logic with a deterministic clock."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "android/app/src/androidTest/java/io/github/alondero/lamborghinirecomp/GameProcessCheck.java"
FIXTURE = ROOT / "tests/android/GameProcessCheckTest.java"


@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "JDK not installed")
class GameProcessTests(unittest.TestCase):
    def test_startup_and_full_soak(self):
        with tempfile.TemporaryDirectory() as output:
            subprocess.run(["javac", "-d", output, str(SOURCE), str(FIXTURE)], check=True)
            result = subprocess.run(
                ["java", "-cp", output, "io.github.alondero.lamborghinirecomp.GameProcessCheckTest"],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
