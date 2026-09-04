#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]
RUNNER = REPOSITORY / "tools" / "run_game_scenario.py"

FAKE_GAME = r'''#!/usr/bin/env python3
import json
import os
import pathlib
import sys
import time

if sys.argv[1:] != ["--console", "--verbose"]:
    print("runner omitted diagnostic CLI flags", file=sys.stderr)
    raise SystemExit(9)

interesting = {
    key: value for key, value in os.environ.items()
    if key.startswith("LAMBO_")
}
print(json.dumps(interesting, sort_keys=True))

mode = os.environ.get("FAKE_GAME_MODE", "success")
if mode == "timeout":
    time.sleep(10)
if mode == "missing":
    raise SystemExit(0)

result_path = pathlib.Path(os.environ["LAMBO_HARNESS_RESULT"])
if mode == "invalid":
    result_path.write_text("{ definitely not JSON", encoding="utf-8")
    raise SystemExit(0)

capture = os.environ.get("LAMBO_DL_RENDER_OUT")
if capture and mode != "no_capture":
    capture_path = pathlib.Path(capture)
    capture_path.parent.mkdir(parents=True, exist_ok=True)
    header = bytearray(54)
    header[0:2] = b"BM"
    header[2:6] = (70).to_bytes(4, "little")
    header[10:14] = (54).to_bytes(4, "little")
    header[14:18] = (40).to_bytes(4, "little")
    header[18:22] = (2).to_bytes(4, "little", signed=True)
    header[22:26] = (2).to_bytes(4, "little", signed=True)
    header[26:28] = (1).to_bytes(2, "little")
    header[28:30] = (24).to_bytes(2, "little")
    header[34:38] = (16).to_bytes(4, "little")
    bmp = bytes(header) + bytes(16)
    if mode == "corrupt_capture":
        bmp = bmp[:-3]
    capture_path.write_bytes(bmp)
    if "LAMBO_DL_RENDER_EVERY" in os.environ:
        pathlib.Path(str(capture_path) + ".20.bmp").write_bytes(bmp)

warp_text = os.environ.get("LAMBO_WARP")
warp_values = [0, 0, 0, 0]
if warp_text:
    parsed = [int(part) for part in warp_text.split(":")]
    parsed.extend((3, 0, 1)[len(parsed) - 1:])
    warp_values = parsed
replay_requested = "LAMBO_INPUT_REPLAY" in os.environ
state_load_requested = "LAMBO_STATE_LOAD" in os.environ
result = {
    "schema": 1,
    "outcome": "passed",
    "reason": "replay_complete",
    "exit_code": 0,
    "vis": 240,
    "swaps": 37,
    "max_state": 8,
    "final_state": 8,
    "menu_screen": -1,
    "loaded_circuit": warp_values[0] if warp_text else 1,
    "player_vehicle": 0,
    "player_speed": -42,
    "max_abs_player_speed": 73,
    "warp": {
        "requested": warp_text is not None,
        "applied": warp_text is not None,
        "failed": False,
        "circuit": warp_values[0],
        "laps": warp_values[1],
        "car": warp_values[2],
        "players": warp_values[3],
        "mode": int(os.environ.get("LAMBO_WARP_MODE", "2")) if warp_text else 0,
    },
    "state_load": {
        "requested": state_load_requested,
        "applied": state_load_requested,
        "failed": False,
    },
    "replay": {
        "configured": replay_requested,
        "recording": False,
        "active": replay_requested,
        "complete": replay_requested,
        "failed": False,
        "total_frames": 180 if replay_requested else 0,
        "frames_consumed": 180 if replay_requested else 0,
        "guest_frames_verified": 180 if replay_requested else 0,
        "dispatcher_ticks": 182,
    },
}
if mode == "incomplete":
    result["reason"] = "max_vis"
    result["replay"]["active"] = True
    result["replay"]["complete"] = False
    result["replay"]["frames_consumed"] = 0
    result["replay"]["guest_frames_verified"] = 0
if mode == "partial":
    result["reason"] = "max_vis"
    result["replay"]["active"] = True
    result["replay"]["complete"] = False
    result["replay"]["frames_consumed"] = 1
    result["replay"]["guest_frames_verified"] = 1
if mode == "warp_not_applied":
    result["warp"]["applied"] = False
if mode == "warp_wrong_track":
    result["loaded_circuit"] = 6 if warp_values[0] != 6 else 5
if mode == "state_load_failed":
    result["state_load"]["applied"] = False
    result["state_load"]["failed"] = True
if mode == "replay_not_started":
    result["replay"]["active"] = False
if mode == "guest_unverified":
    result["replay"]["guest_frames_verified"] = 179
result_path.write_text(json.dumps(result), encoding="utf-8")
'''


class GameScenarioRunnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.scenario_directory = self.root / "nested" / "scenarios"
        self.scenario_directory.mkdir(parents=True)
        self.fake_game = self.root / "fake_game.py"
        self.fake_game.write_text(FAKE_GAME, encoding="utf-8")
        self.artifacts = self.root / "artifacts"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_scenario(self, scenario: dict, name: str = "scenario.json") -> Path:
        path = self.scenario_directory / name
        path.write_text(json.dumps(scenario), encoding="utf-8")
        return path

    def run_scenario(
        self,
        scenario: dict,
        *,
        mode: str = "success",
        timeout: float | None = None,
        inherited: dict[str, str] | None = None,
    ) -> tuple[subprocess.CompletedProcess[str], Path]:
        scenario_path = self.write_scenario(scenario)
        environment = os.environ.copy()
        environment["FAKE_GAME_MODE"] = mode
        if inherited:
            environment.update(inherited)
        command = [
            sys.executable,
            str(RUNNER),
            str(scenario_path),
            "--exe",
            str(self.fake_game),
            "--artifacts-dir",
            str(self.artifacts),
        ]
        if timeout is not None:
            command.extend(("--timeout", str(timeout)))
        completed = subprocess.run(
            command,
            cwd=self.root,
            env=environment,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
        )
        artifact_directories = list(self.artifacts.iterdir())
        self.assertEqual(len(artifact_directories), 1, completed.stdout + completed.stderr)
        return completed, artifact_directories[0]

    def test_success_isolated_environment_and_relative_paths(self) -> None:
        replay = self.scenario_directory / "data" / "lap.jsonl"
        replay.parent.mkdir()
        replay.write_text('{"schema":1}\n', encoding="utf-8")
        scenario = {
            "schema": 1,
            "name": "complete lap",
            "warp": "2:3:0:1",
            "warp_mode": 0,
            "input": {
                "replay": "data/lap.jsonl",
                "start_state": 8,
                "start_delay": 3,
                "exit_on_end": True,
            },
            "max_vis": 500,
            "capture": {"path": "captures/finish.bmp", "state": 8, "every": 20},
            "expect": {
                "max_state_at_least": 8,
                "replay_complete": True,
                "min_swaps": 30,
                "min_abs_player_speed": 50,
                "capture": True,
            },
        }
        inherited = {
            "LAMBO_MODERN_INPUT": "ffff:80:80",
            "LAMBO_INPUT_PULSE": "1000:1:1",
            "LAMBO_ANALOG_THROTTLE": "1",
            "LAMBO_INPUT_REPLAY": "wrong-replay",
            "LAMBO_STATE_SAVE": "wrong-state",
            "LAMBO_DL_RENDER_OUT": "wrong-capture",
            "LAMBO_WARP_MODE": "99",
            "LAMBO_DRAW_DISTANCE": "999999",
            "LAMBO_ASSET_DIR": str(self.root / "custom-assets"),
        }

        completed, artifact = self.run_scenario(scenario, inherited=inherited)

        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertIn("PASS complete lap", completed.stdout)
        self.assertIn(f"artifacts={artifact.resolve()}", completed.stdout)
        environment = json.loads((artifact / "stdout.log").read_text(encoding="utf-8"))
        staged_replay = artifact / "fixtures" / "input-replay.jsonl"
        self.assertEqual(Path(environment["LAMBO_INPUT_REPLAY"]), staged_replay.resolve())
        self.assertEqual(staged_replay.read_bytes(), replay.read_bytes())
        self.assertEqual(
            Path(environment["LAMBO_DL_RENDER_OUT"]),
            (artifact / "capture" / "captures" / "finish.bmp").resolve(),
        )
        self.assertEqual(environment["LAMBO_INPUT_START_STATE"], "8")
        self.assertEqual(environment["LAMBO_INPUT_START_DELAY"], "3")
        self.assertEqual(environment["LAMBO_INPUT_EXIT_ON_END"], "1")
        self.assertEqual(environment["LAMBO_MODERN_MAX_VIS"], "500")
        self.assertEqual(environment["LAMBO_HEADLESS"], "1")
        self.assertEqual(environment["LAMBO_WARP_MODE"], "0")
        self.assertNotIn("LAMBO_MODERN_INPUT", environment)
        self.assertNotIn("LAMBO_INPUT_PULSE", environment)
        self.assertNotIn("LAMBO_ANALOG_THROTTLE", environment)
        self.assertNotIn("LAMBO_STATE_SAVE", environment)
        self.assertNotIn("LAMBO_DRAW_DISTANCE", environment)
        self.assertEqual(environment["LAMBO_ASSET_DIR"], inherited["LAMBO_ASSET_DIR"])
        self.assertEqual(Path(environment["LAMBO_GRAPHICS_CONFIG"]).parent, artifact.resolve())
        self.assertEqual(Path(environment["LAMBO_CONTROLLER_PAK_FILE"]).parent, artifact.resolve())
        self.assertEqual(Path(environment["LAMBO_HARNESS_RESULT"]).parent, artifact.resolve())
        self.assertTrue((artifact / "capture" / "captures" / "finish.bmp").is_file())
        self.assertTrue(
            (artifact / "capture" / "captures" / "finish.bmp.20.bmp").is_file()
        )
        self.assertTrue((artifact / "scenario.json").is_file())
        resolved = json.loads((artifact / "scenario-resolved.json").read_text(encoding="utf-8"))
        self.assertEqual(resolved["input"]["replay"], "fixtures/input-replay.jsonl")
        fixture_manifest = json.loads(
            (artifact / "fixtures-manifest.json").read_text(encoding="utf-8")
        )
        self.assertEqual(fixture_manifest["fixtures"][0]["role"], "input-replay")
        self.assertEqual(fixture_manifest["fixtures"][0]["bytes"], replay.stat().st_size)
        self.assertEqual(len(fixture_manifest["fixtures"][0]["sha256"]), 64)
        self.assertTrue((artifact / "harness-environment.json").is_file())
        recorded_environment = json.loads(
            (artifact / "harness-environment.json").read_text(encoding="utf-8")
        )
        self.assertEqual(
            recorded_environment["LAMBO_ASSET_DIR"], inherited["LAMBO_ASSET_DIR"]
        )
        runner_result = json.loads((artifact / "runner-result.json").read_text(encoding="utf-8"))
        self.assertEqual(runner_result["outcome"], "passed")
        self.assertEqual(runner_result["failures"], [])
        self.assertEqual(runner_result["native_result"]["max_state"], 8)

    def test_oracle_failure_is_nonzero_and_preserves_result(self) -> None:
        scenario = {
            "schema": 1,
            "name": "too shallow",
            "max_vis": 300,
            "expect": {
                "max_state_at_least": 9,
                "min_swaps": 40,
                "min_abs_player_speed": 80,
            },
        }

        completed, artifact = self.run_scenario(scenario)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("FAIL too shallow", completed.stdout)
        self.assertIn("max_state 8 < 9", completed.stdout)
        self.assertIn("swaps 37 < 40", completed.stdout)
        self.assertIn("max_abs_player_speed 73 < 80", completed.stdout)
        self.assertTrue((artifact / "harness-result.json").is_file())
        self.assertTrue((artifact / "stdout.log").is_file())
        runner_result = json.loads((artifact / "runner-result.json").read_text(encoding="utf-8"))
        self.assertEqual(runner_result["outcome"], "failed")
        self.assertIn("max_state 8 < 9", runner_result["failures"])

    def test_replay_completion_is_required_by_default(self) -> None:
        replay = self.scenario_directory / "lap.jsonl"
        replay.write_text('{"schema":1}\n', encoding="utf-8")
        scenario = {
            "schema": 1,
            "name": "unfinished lap",
            "input": {"replay": "lap.jsonl"},
        }

        completed, _ = self.run_scenario(scenario, mode="incomplete")

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("replay_complete was false, expected true", completed.stdout)

    def test_non_exiting_replay_must_still_consume_a_frame(self) -> None:
        replay = self.scenario_directory / "lap.jsonl"
        replay.write_text('{"schema":1}\n', encoding="utf-8")
        scenario = {
            "schema": 1,
            "name": "zero progress",
            "input": {"replay": "lap.jsonl", "exit_on_end": False},
        }

        completed, _ = self.run_scenario(scenario, mode="incomplete")

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("native replay did not consume any frames", completed.stdout)

    def test_partial_replay_requires_an_explicit_expectation(self) -> None:
        replay = self.scenario_directory / "lap.jsonl"
        replay.write_text('{"schema":1}\n', encoding="utf-8")
        scenario = {
            "schema": 1,
            "name": "partial progress",
            "input": {"replay": "lap.jsonl", "exit_on_end": False},
        }

        completed, _ = self.run_scenario(scenario, mode="partial")
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("replay_complete was false, expected true", completed.stdout)

        self.artifacts = self.root / "artifacts-explicit-partial"
        scenario["expect"] = {"replay_complete": False}
        completed, _ = self.run_scenario(scenario, mode="partial")
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_missing_and_invalid_native_results_fail_cleanly(self) -> None:
        scenario = {"schema": 1, "name": "bad native result", "max_vis": 20}
        for mode, expected in (
            ("missing", "native result file is missing"),
            ("invalid", "native result JSON is invalid"),
        ):
            with self.subTest(mode=mode):
                # Each subtest needs its own artifact parent because run_scenario
                # deliberately asserts that one unique run directory was made.
                self.artifacts = self.root / f"artifacts-{mode}"
                completed, artifact = self.run_scenario(scenario, mode=mode)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn(expected, completed.stdout)
                self.assertTrue((artifact / "stdout.log").is_file())
                runner_result = json.loads(
                    (artifact / "runner-result.json").read_text(encoding="utf-8")
                )
                self.assertEqual(runner_result["outcome"], "failed")
                self.assertIsNone(runner_result["native_result"])

    def test_timeout_kills_the_process_and_preserves_logs(self) -> None:
        scenario = {"schema": 1, "name": "hung game", "max_vis": 20}

        completed, artifact = self.run_scenario(scenario, mode="timeout", timeout=0.2)

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("timed out after 0.2s", completed.stdout)
        self.assertTrue((artifact / "stdout.log").is_file())
        runner_result = json.loads((artifact / "runner-result.json").read_text(encoding="utf-8"))
        self.assertTrue(runner_result["timed_out"])

    def test_stale_capture_cannot_satisfy_capture_oracle(self) -> None:
        capture = self.scenario_directory / "captures" / "frame.bmp"
        capture.parent.mkdir()
        capture.write_bytes(b"old capture")
        numbered = Path(str(capture) + ".12.bmp")
        numbered.write_bytes(b"old numbered capture")
        scenario = {
            "schema": 1,
            "name": "fresh capture required",
            "capture": {"path": "captures/frame.bmp", "state": 8, "every": 12},
            "expect": {"capture": True},
        }

        completed, artifact = self.run_scenario(scenario, mode="no_capture")

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("capture was not written", completed.stdout)
        self.assertEqual(capture.read_bytes(), b"old capture")
        self.assertEqual(numbered.read_bytes(), b"old numbered capture")
        self.assertFalse((artifact / "capture" / "captures" / "frame.bmp").exists())

    def test_truncated_bmp_cannot_satisfy_capture_oracle(self) -> None:
        scenario = {
            "schema": 1,
            "name": "complete capture required",
            "capture": {"path": "frame.bmp", "state": 8},
            "expect": {"capture": True},
        }

        completed, artifact = self.run_scenario(scenario, mode="corrupt_capture")

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("complete renderer BMP", completed.stdout)
        self.assertTrue((artifact / "capture" / "frame.bmp").is_file())

    def test_native_bootstrap_and_replay_claims_are_verified(self) -> None:
        replay = self.scenario_directory / "lap.jsonl"
        replay.write_text('{"schema":1}\n', encoding="utf-8")
        cases = (
            (
                "warp_not_applied",
                {"schema": 1, "name": "warp claim", "warp": 1,
                 "input": {"replay": "lap.jsonl"}},
                "native warp was requested but never applied",
            ),
            (
                "warp_wrong_track",
                {"schema": 1, "name": "track claim", "warp": 1,
                 "input": {"replay": "lap.jsonl"}},
                "loaded_circuit 6 != requested 1",
            ),
            (
                "replay_not_started",
                {"schema": 1, "name": "start claim", "warp": 1,
                 "input": {"replay": "lap.jsonl"}},
                "native replay never became active",
            ),
            (
                "guest_unverified",
                {"schema": 1, "name": "guest claim", "warp": 1,
                 "input": {"replay": "lap.jsonl"}},
                "guest input verification covered 179/180 consumed frames",
            ),
        )
        for index, (mode, scenario, expected) in enumerate(cases):
            with self.subTest(mode=mode):
                self.artifacts = self.root / f"artifacts-claim-{index}"
                completed, _ = self.run_scenario(scenario, mode=mode)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn(expected, completed.stdout)

    def test_failed_state_load_is_not_mistaken_for_race_state(self) -> None:
        state = self.scenario_directory / "grid.lstate"
        replay = self.scenario_directory / "lap.jsonl"
        state.write_bytes(b"state")
        replay.write_text('{"schema":1}\n', encoding="utf-8")
        scenario = {
            "schema": 1,
            "name": "state claim",
            "state_load": "grid.lstate",
            "input": {"replay": "lap.jsonl"},
        }

        completed, _ = self.run_scenario(scenario, mode="state_load_failed")

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("native state load reported failure", completed.stdout)
        self.assertIn("native state load was requested but never applied", completed.stdout)

    def test_warp_and_state_load_are_rejected_as_ambiguous(self) -> None:
        state = self.scenario_directory / "grid.lstate"
        state.write_bytes(b"state")
        scenario_path = self.write_scenario(
            {"schema": 1, "name": "ambiguous", "warp": 1, "state_load": "grid.lstate"}
        )
        completed = subprocess.run(
            [sys.executable, str(RUNNER), str(scenario_path), "--exe", str(self.fake_game)],
            cwd=self.root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("alternative bootstraps", completed.stderr)

    def test_unknown_scenario_field_is_rejected(self) -> None:
        scenario_path = self.write_scenario(
            {"schema": 1, "name": "typo", "expect": {"min_swap": 1}}
        )
        completed = subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                str(scenario_path),
                "--exe",
                str(self.fake_game),
                "--artifacts-dir",
                str(self.artifacts),
            ],
            cwd=self.root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("scenario.expect has unknown field(s): min_swap", completed.stderr)
        self.assertFalse(self.artifacts.exists())

    def test_unverified_warp_mode_is_rejected(self) -> None:
        scenario_path = self.write_scenario(
            {"schema": 1, "name": "bad mode", "warp": 1, "warp_mode": 99}
        )
        completed = subprocess.run(
            [sys.executable, str(RUNNER), str(scenario_path), "--exe", str(self.fake_game)],
            cwd=self.root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("0 (time trial) or 2 (single race)", completed.stderr)

    def test_unverified_warp_car_is_rejected(self) -> None:
        scenario_path = self.write_scenario(
            {"schema": 1, "name": "bad car", "warp": "1:3:1:1"}
        )
        completed = subprocess.run(
            [sys.executable, str(RUNNER), str(scenario_path), "--exe", str(self.fake_game)],
            cwd=self.root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("runtime-verified car 0", completed.stderr)


if __name__ == "__main__":
    unittest.main()
