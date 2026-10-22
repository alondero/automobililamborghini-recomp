from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_rt_shadow_lap import developer_env, scenario, summarize  # noqa: E402


class LapScenarioTests(unittest.TestCase):
    def test_lap_is_a_windowed_traced_replay_without_capture(self) -> None:
        lap = scenario(4, 1.0, 16)
        self.assertEqual(lap["name"], "rt-shadow-lap-c4")
        self.assertFalse(lap["headless"])
        self.assertEqual(lap["graphics"], {"rt_shadows": True, "rt_shadow_rays": 16, "rt_shadow_softness": 1.0})
        self.assertTrue(Path(lap["input"]["replay"]).is_absolute())
        self.assertNotIn("capture", lap)
        self.assertNotIn("developer_env", lap)

    def test_developer_variables_reach_the_scenario(self) -> None:
        lap = scenario(1, 0.5, 8, {"LAMBO_RT_SHADOW_DEBUG": "2"})
        self.assertEqual(lap["developer_env"], {"LAMBO_RT_SHADOW_DEBUG": "2"})

    def test_developer_variables_parse_as_key_value_pairs(self) -> None:
        self.assertEqual(developer_env(["LAMBO_RT_SHADOW_DEBUG=1", "LAMBO_RT_SHADOW_SKY_OPEN=0.6"]),
                         {"LAMBO_RT_SHADOW_DEBUG": "1", "LAMBO_RT_SHADOW_SKY_OPEN": "0.6"})
        self.assertEqual(developer_env(None), {})
        with self.assertRaises(ValueError):
            developer_env(["LAMBO_RT_SHADOW_DEBUG"])


class LapSummaryTests(unittest.TestCase):
    def test_summary_lines_are_totalled(self) -> None:
        log = "\n".join([
            "[2026-10-22] [info] [rt-shadow] lap_summary tasks=120 ready=118 gate_1=2",
            "[2026-10-22] [info] [rt-shadow] lap_summary tasks=120 ready=120",
            "[2026-10-22] [info] [rt-shadow] lap_summary tasks=60 ready=55 gate_1=1 no_traced_receivers=4",
        ])
        self.assertEqual(summarize(log), {"source": "lap_summary", "tasks": 300, "ready": 293,
                                          "reasons": {"gate_1": 3, "no_traced_receivers": 4}})

    def test_older_logs_are_reconstructed_from_fallbacks(self) -> None:
        log = "\n".join([
            '[rt-shadow] workload=3 task=1 epoch=2 ready=1 reason="" casters=93',
            "[rt-shadow] task=5 native: admission gate 3 circuit=0 phase=8 players=1 mode=0",
            "[rt-shadow] task=6 native: admission gate 3 circuit=0 phase=8 players=1 mode=0",
            '[rt-shadow] workload=11 task=10 epoch=2 ready=1 reason="" casters=93',
        ])
        self.assertEqual(summarize(log), {"source": "reconstructed", "tasks": 10, "ready": 8,
                                          "reasons": {"admission gate 3": 2}})


if __name__ == "__main__":
    unittest.main()
