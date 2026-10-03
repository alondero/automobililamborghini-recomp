from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
try:  # The capture checker needs numpy; the CI docs job installs no packages.
    import numpy as np
    from check_rt_shadow_capture import darkening, load_swap, production_log
    from rt_swap_png import crop, read_swap
except ImportError:  # pragma: no cover - exercised only without numpy
    np = None


def write_capture(render: Path, sequence: int, rgb, row_padding: int = 8) -> None:
    height, width, _ = rgb.shape
    row = width * 4 + row_padding
    raw = np.zeros((height, row), np.uint8)
    bgra = np.zeros((height, width, 4), np.uint8)
    bgra[..., 0], bgra[..., 1], bgra[..., 2], bgra[..., 3] = rgb[..., 2], rgb[..., 1], rgb[..., 0], 255
    raw[:, :width * 4] = bgra.reshape(height, width * 4)
    render.mkdir(parents=True, exist_ok=True)
    (render / f"task-{sequence}-swap.bgra").write_bytes(raw.tobytes())
    (render / f"task-{sequence}-present.json").write_text(json.dumps({
        "format": "BGRA8", "width": width, "height": height, "row_bytes": row,
        "successful_present": True}), encoding="utf-8")


@unittest.skipUnless(np is not None, "numpy is required by tools/check_rt_shadow_capture.py")
class ShadowCaptureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def scene(self, name: str, images: dict, log: str = "") -> Path:
        run = self.root / name
        for sequence, image in images.items():
            write_capture(run / "rt-render", sequence, image)
        (run / "stderr.log").write_text(log, encoding="utf-8")
        return run

    def test_capture_round_trip_strips_row_padding_and_channel_order(self) -> None:
        image = np.arange(4 * 3 * 3, dtype=np.uint8).reshape(4, 3, 3)
        write_capture(self.root / "r", 60, image)
        self.assertTrue(np.array_equal(load_swap(self.root / "r", 60), image))
        width, height, rgb = read_swap(self.root / "r", 60)
        self.assertEqual((width, height), (3, 4))
        self.assertEqual(rgb, image.tobytes())
        cropped_width, cropped_height, cropped = crop(width, rgb, (1, 1, 3, 2), 2)
        self.assertEqual((cropped_width, cropped_height), (4, 2))
        self.assertEqual(len(cropped), 4 * 2 * 3)

    def test_darkening_reports_transmission_and_brightening(self) -> None:
        reference = np.full((2, 2, 3), 100, np.uint8)
        image = reference.copy()
        image[0, 0] = 51
        image[1, 1] = 120
        result = darkening(reference, image)
        self.assertEqual((result["changed_pixels"], result["darker_pixels"], result["brighter_pixels"]), (2, 1, 1))
        self.assertAlmostEqual(result["transmission_percentiles"]["50"], 0.51)
        with self.assertRaises(ValueError):
            darkening(reference, np.zeros((3, 2, 3), np.uint8))

    def test_production_log_parses_ready_state(self) -> None:
        run = self.root / "log"
        run.mkdir()
        (run / "stderr.log").write_text(
            '[x] [rt-shadow] workload=61 task=60 epoch=3 ready=1 reason="" casters=1\n'
            '[x] [rt-shadow] workload=62 task=61 epoch=3 ready=0 reason="no visible receiver draw" casters=0\n',
            encoding="utf-8")
        states = production_log(run)
        self.assertTrue(states[60]["ready"])
        self.assertEqual(states[61]["reason"], "no visible receiver draw")

    def run_checker(self, native: Path, unshadowed: Path, traced: Path, *extra: str):
        return subprocess.run([sys.executable, str(ROOT / "tools" / "check_rt_shadow_capture.py"), str(native),
                               str(unshadowed), str(traced), "--sequences", "60", "300", *extra],
                              capture_output=True, text=True, timeout=30)

    def test_checker_requires_native_core_parity_ready_state_and_no_brightening(self) -> None:
        lit = np.full((4, 4, 3), 200, np.uint8)
        shadowed = lit.copy()
        shadowed[:2, :2] = 102  # 0.51 transmission
        unshadowed = self.scene("unshadowed", {60: lit, 300: lit})
        native = self.scene("native", {60: shadowed, 300: shadowed})
        ready = ('[x] [rt-shadow] workload=61 task=60 epoch=3 ready=1 reason=""\n'
                 '[x] [rt-shadow] workload=301 task=300 epoch=3 ready=1 reason=""\n')
        traced = self.scene("traced", {60: shadowed, 300: shadowed}, ready)
        self.assertEqual(self.run_checker(native, unshadowed, traced).returncode, 0)

        lighter = shadowed.copy()
        lighter[:2, :2] = 150
        light_core = self.scene("light", {60: lighter, 300: lighter}, ready)
        completed = self.run_checker(native, unshadowed, light_core)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("hard shadow core differs", completed.stdout)
        self.assertEqual(self.run_checker(native, unshadowed, light_core, "--soft").returncode, 0)

        not_ready = self.scene("pending", {60: shadowed, 300: shadowed}, ready.replace("task=300 epoch=3 ready=1",
                                                                                       "task=300 epoch=3 ready=0"))
        self.assertIn("replacement not ready", self.run_checker(native, unshadowed, not_ready).stdout)

        brighter = shadowed.copy()
        brighter[3, 3] = 255
        warmup_only = self.scene("warmup", {60: brighter, 300: shadowed}, ready)
        self.assertEqual(self.run_checker(native, unshadowed, warmup_only).returncode, 0)
        steady = self.scene("steady", {60: shadowed, 300: brighter}, ready)
        completed = self.run_checker(native, unshadowed, steady)
        self.assertIn("task 300: 1 RT pixels brighter than unshadowed", completed.stdout)


if __name__ == "__main__":
    unittest.main()
