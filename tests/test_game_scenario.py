from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "run_game_scenario.py"

FAKE_GAME = textwrap.dedent(
    r'''
    import json, os, sys, time
    from pathlib import Path
    mode = os.environ.get("FAKE_GAME_MODE", "success")
    if mode == "timeout":
        time.sleep(30)
        raise SystemExit(0)
    result_path = os.environ["LAMBO_HARNESS_RESULT"]
    print(json.dumps({k: v for k, v in os.environ.items() if k.startswith("LAMBO_")}))
    if mode == "missing":
        raise SystemExit(0)
    if mode == "invalid":
        open(result_path, "w", encoding="utf-8").write("{")
        raise SystemExit(0)
    warp = os.environ.get("LAMBO_WARP")
    warp_values = [int(part) for part in warp.split(":")] if warp else [0, 0, 0, 0]
    warp_values += [3, 0, 1][:4-len(warp_values)]
    replay = bool(os.environ.get("LAMBO_INPUT_REPLAY"))
    complete = replay and mode != "partial" and mode != "incomplete"
    result = {
        "schema": 1, "outcome": "passed", "reason": "fake", "exit_code": 0,
        "vis": 40, "swaps": 50, "max_state": 8, "final_state": 8,
        "menu_screen": 0, "loaded_circuit": warp_values[0],
        "player_vehicle": 0, "player_speed": 70, "max_abs_player_speed": 70,
        "warp": {"requested": bool(warp), "applied": bool(warp), "failed": False,
                  "circuit": warp_values[0], "laps": warp_values[1],
                  "car": warp_values[2], "players": warp_values[3],
                  "mode": int(os.environ.get("LAMBO_WARP_MODE", "2"))},
        "state_load": {"requested": bool(os.environ.get("LAMBO_STATE_LOAD")),
                       "applied": bool(os.environ.get("LAMBO_STATE_LOAD")), "failed": False},
        "replay": {"configured": replay, "recording": False, "active": replay,
                    "complete": complete, "failed": False, "total_frames": 3 if replay else 0,
                    "frames_consumed": 3 if complete else (1 if replay else 0),
                    "guest_frames_verified": 3 if complete else (1 if replay else 0),
                    "dispatcher_ticks": 4},
    }
    capture = os.environ.get("LAMBO_DL_RENDER_OUT")
    if capture and mode != "no_capture":
        Path(capture).parent.mkdir(parents=True, exist_ok=True)
        Path(capture).write_bytes(b"fake capture")
    Path(result_path).write_text(json.dumps(result), encoding="utf-8")
    raise SystemExit(0)
    '''
)


class ScenarioRunnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.scenario_dir = self.root / "scenarios"
        self.scenario_dir.mkdir()
        self.fake = self.root / "fake_game.py"
        self.fake.write_text(FAKE_GAME, encoding="utf-8")
        self.artifacts = self.root / "artifacts"

    def tearDown(self) -> None:
        self.temp.cleanup()

    def run_scenario(self, document: dict, mode: str = "success", timeout: float | None = None):
        scenario = self.scenario_dir / "scenario.json"
        scenario.write_text(json.dumps(document), encoding="utf-8")
        environment = os.environ.copy()
        environment["FAKE_GAME_MODE"] = mode
        environment["LAMBO_MODERN_INPUT"] = "stale-input"
        command = [sys.executable, str(RUNNER), str(scenario), "--exe", str(self.fake),
                   "--artifacts-dir", str(self.artifacts)]
        if timeout is not None:
            command += ["--timeout", str(timeout)]
        completed = subprocess.run(command, cwd=self.root, env=environment,
                                   capture_output=True, text=True, timeout=10)
        directories = list(self.artifacts.iterdir()) if self.artifacts.exists() else []
        self.assertEqual(len(directories), 1, completed.stdout + completed.stderr)
        return completed, directories[0]

    def test_success_stages_fixture_and_isolates_environment(self) -> None:
        replay = self.scenario_dir / "lap.jsonl"
        replay.write_text("trace", encoding="utf-8")
        scenario = {"schema": 1, "name": "success", "warp": "2:3:0:1", "warp_mode": 0,
                    "input": {"replay": "lap.jsonl"}, "capture": {"path": "frame.bmp"},
                    "expect": {"max_state_at_least": 8, "replay_complete": True,
                               "min_swaps": 10, "capture": True}}
        completed, artifact = self.run_scenario(scenario)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertIn("PASS success", completed.stdout)
        self.assertEqual((artifact / "fixtures" / "input-replay.jsonl").read_text(), "trace")
        environment = json.loads((artifact / "harness-environment.json").read_text())
        self.assertNotIn("LAMBO_MODERN_INPUT", environment)
        self.assertTrue((artifact / "capture" / "frame.bmp").is_file())
        self.assertEqual(json.loads((artifact / "runner-result.json").read_text())["failures"], [])

    def test_incomplete_replay_fails_unless_explicitly_expected(self) -> None:
        (self.scenario_dir / "lap.jsonl").write_text("trace", encoding="utf-8")
        scenario = {"schema": 1, "name": "partial", "input": {"replay": "lap.jsonl"}}
        completed, _ = self.run_scenario(scenario, mode="incomplete")
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("replay_complete was False, expected True", completed.stdout)
        self.artifacts = self.root / "artifacts-explicit"
        scenario["expect"] = {"replay_complete": False}
        completed, _ = self.run_scenario(scenario, mode="partial")
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_missing_result_and_timeout_preserve_artifacts(self) -> None:
        scenario = {"schema": 1, "name": "missing", "max_vis": 20}
        completed, artifact = self.run_scenario(scenario, mode="missing")
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("native result is missing or invalid", completed.stdout)
        self.assertTrue((artifact / "stdout.log").is_file())
        self.artifacts = self.root / "artifacts-timeout"
        completed, artifact = self.run_scenario(
            {"schema": 1, "name": "timeout", "max_vis": 20}, mode="timeout", timeout=0.1
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("timed out after 0.1s", completed.stdout)
        self.assertTrue(json.loads((artifact / "runner-result.json").read_text())["timed_out"])

    def test_schema_errors_are_reported_before_starting_game(self) -> None:
        state = self.scenario_dir / "state.lstate"
        state.write_bytes(b"state")
        scenario = {"schema": 1, "name": "ambiguous", "warp": 1, "state_load": "state"}
        path = self.scenario_dir / "bad.json"
        path.write_text(json.dumps(scenario), encoding="utf-8")
        completed = subprocess.run(
            [sys.executable, str(RUNNER), str(path), "--exe", str(self.fake)],
            cwd=self.root, capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertIn("alternative bootstraps", completed.stderr)


if __name__ == "__main__":
    unittest.main()
