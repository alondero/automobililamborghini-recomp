"""Compile and run the package's portable weather resolver without a ROM."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")


class WeatherStateTests(unittest.TestCase):
    @unittest.skipUnless(CC, "a host C compiler is required")
    def test_native_modes_round_slots_and_invalid_state(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "weather-state.exe"
            subprocess.run([CC, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests/test_jp_weather_state.c"),
                            "-o", str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
