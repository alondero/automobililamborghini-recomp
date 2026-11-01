"""Exercise the actual package hook with typed fake guest memory and config."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")


class WeatherModTests(unittest.TestCase):
    @unittest.skipUnless(CC, "a host C compiler is required")
    def test_race_hook_gates_memory_effects_and_baseline_restoration(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "weather-mod.exe"
            subprocess.run([CC, "-std=c99", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "tests/test_jp_weather_mod.c"),
                            "-o", str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
