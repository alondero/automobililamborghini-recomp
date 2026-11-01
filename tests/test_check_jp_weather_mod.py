import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("checker", ROOT / "tools/check_jp_weather_mod.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class WeatherMarkerTests(unittest.TestCase):
    def test_enabled_requires_exact_resolution_and_verified_writes(self):
        for mode in ("dry", "wet", "night"):
            self.assertTrue(checker.markers_match(["data verified", mode], mode))
            for markers in ([], [mode], ["data mismatch", mode],
                            ["data verified", mode, mode], ["data verified", "US default"]):
                self.assertFalse(checker.markers_match(markers, mode))

    def test_absent_and_default_reject_weather_side_effect_markers(self):
        self.assertTrue(checker.markers_match([], None))
        self.assertTrue(checker.markers_match(["US default"], "US default"))
        self.assertFalse(checker.markers_match(["data verified", "US default"], "US default"))
        self.assertFalse(checker.markers_match(["US default"], None))


if __name__ == "__main__":
    unittest.main()
