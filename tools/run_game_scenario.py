#!/usr/bin/env python3
"""Run one game scenario without a human or a focused window."""

from __future__ import annotations

import argparse
import copy
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 1
DEFAULT_TIMEOUT_SECONDS = 90.0
DEFAULT_MAX_VIS = 3600


class ScenarioError(ValueError):
    pass


def _dict(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ScenarioError(f"{name} must be an object")
    return value


def _path(value: Any, name: str, base: Path) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise ScenarioError(f"{name} must be a path string")
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = base / path
    return path.resolve()


def _warp(value: Any) -> str:
    if isinstance(value, bool) or not isinstance(value, (str, int)):
        raise ScenarioError("scenario.warp must be circuit[:laps[:car[:players]]]")
    text = str(value).strip()
    parts = text.split(":")
    if not 1 <= len(parts) <= 4 or any(not part.isdigit() for part in parts):
        raise ScenarioError("scenario.warp must be circuit[:laps[:car[:players]]]")
    values = [int(part) for part in parts]
    values += [3, 0, 1][: 4 - len(values)]
    circuit, laps, car, players = values
    if not 1 <= circuit <= 6 or not 1 <= laps <= 30:
        raise ScenarioError("scenario.warp circuit must be 1-6 and laps must be 1-30")
    if car != 0:
        raise ScenarioError("scenario.warp currently supports runtime-verified car 0")
    if not 1 <= players <= 4 or (players >= 3 and circuit > 3):
        raise ScenarioError("scenario.warp has an unsupported player/track combination")
    return text


def load_scenario(path: Path) -> dict[str, Any]:
    try:
        scenario = _dict(json.loads(path.read_text(encoding="utf-8")), "scenario")
    except OSError as error:
        raise ScenarioError(f"cannot read scenario: {error}") from error
    except json.JSONDecodeError as error:
        raise ScenarioError(f"invalid scenario JSON: {error}") from error

    if scenario.get("schema") != SCHEMA_VERSION:
        raise ScenarioError(f"scenario.schema must be {SCHEMA_VERSION}")
    name = scenario.get("name", path.stem)
    if not isinstance(name, str) or not name.strip():
        raise ScenarioError("scenario.name must be a non-empty string")
    scenario["name"] = name.strip()
    if "warp" in scenario:
        scenario["warp"] = _warp(scenario["warp"])
    if "warp_mode" in scenario and scenario["warp_mode"] not in (0, 2):
        raise ScenarioError("scenario.warp_mode must be 0 (time trial) or 2 (single race)")
    if "warp" in scenario and "state_load" in scenario:
        raise ScenarioError("scenario.warp and scenario.state_load are alternative bootstraps")

    if "state_load" in scenario:
        scenario["state_load"] = _path(scenario["state_load"], "scenario.state_load", path.parent)
        if not scenario["state_load"].is_file():
            raise ScenarioError(f"state-load file does not exist: {scenario['state_load']}")

    inputs = _dict(scenario.setdefault("input", {}), "scenario.input")
    if "replay" in inputs:
        inputs["replay"] = _path(inputs["replay"], "scenario.input.replay", path.parent)
        if not inputs["replay"].is_file():
            raise ScenarioError(f"replay file does not exist: {inputs['replay']}")
    if any(key in inputs for key in ("start_state", "start_delay", "exit_on_end")) and "replay" not in inputs:
        raise ScenarioError("scenario.input start/exit fields require scenario.input.replay")

    capture = scenario.get("capture")
    if capture is not None:
        capture = _dict(capture, "scenario.capture")
        value = capture.get("path")
        if not isinstance(value, str) or not value.strip():
            raise ScenarioError("scenario.capture.path must be a relative path")
        capture_path = Path(value)
        if capture_path.is_absolute() or ".." in capture_path.parts:
            raise ScenarioError("scenario.capture.path must stay inside the run artifact")
        scenario["capture"] = capture

    scenario["expect"] = _dict(scenario.get("expect", {}), "scenario.expect")
    return scenario


def find_executable(override: str | None, repository: Path) -> Path:
    if override:
        candidate = Path(override).expanduser()
        if not candidate.is_absolute():
            candidate = Path.cwd() / candidate
        candidate = candidate.resolve()
        if candidate.is_file():
            return candidate
        raise ScenarioError(f"executable does not exist: {candidate}")
    names = ("lamborghini_modern.exe", "lamborghini_modern") if os.name == "nt" else (
        "lamborghini_modern", "lamborghini_modern.exe"
    )
    for root in (repository / "build", *sorted(repository.glob("build-*"))):
        for name in names:
            candidate = root / name
            if candidate.is_file():
                return candidate.resolve()
    for name in names:
        found = shutil.which(name)
        if found:
            return Path(found).resolve()
    raise ScenarioError("could not find lamborghini_modern; pass --exe PATH")


def create_artifacts(root: Path, name: str) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    slug = "".join(ch if ch.isalnum() or ch in "._-" else "-" for ch in name).strip("-.")
    return Path(tempfile.mkdtemp(prefix=(slug or "scenario") + "-", dir=root)).resolve()


def stage_fixtures(scenario: dict[str, Any], artifact: Path) -> dict[str, Any]:
    runtime = copy.deepcopy(scenario)
    fixture_root = artifact / "fixtures"
    fixture_root.mkdir()
    fixtures: list[dict[str, Any]] = []

    def stage(source: Path, role: str) -> str:
        destination = fixture_root / (role + "".join(source.suffixes))
        shutil.copy2(source, destination)
        fixtures.append({"role": role, "file": destination.name, "bytes": destination.stat().st_size})
        return str(destination.resolve())

    if "state_load" in scenario:
        runtime["state_load"] = stage(scenario["state_load"], "state-load")
    if "replay" in scenario["input"]:
        runtime["input"]["replay"] = stage(scenario["input"]["replay"], "input-replay")
    (artifact / "fixtures.json").write_text(
        json.dumps({"schema": SCHEMA_VERSION, "fixtures": fixtures}, indent=2) + "\n",
        encoding="utf-8",
    )
    return runtime


def build_environment(scenario: dict[str, Any], runtime: dict[str, Any], artifact: Path) -> dict[str, str]:
    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith("LAMBO_") or key == "LAMBO_ASSET_DIR"
    }
    environment.update({
        "LAMBO_HEADLESS": "1" if scenario.get("headless", True) else "0",
        "LAMBO_GRAPHICS_CONFIG": str(artifact / "graphics.json"),
        "LAMBO_CONTROLLER_PAK_FILE": str(artifact / "controller.mpk"),
        "LAMBO_HARNESS_RESULT": str(artifact / "harness-result.json"),
        "LAMBO_MODERN_MAX_VIS": str(scenario.get("max_vis", DEFAULT_MAX_VIS)),
    })
    if "warp" in runtime:
        environment["LAMBO_WARP"] = runtime["warp"]
    if "warp_mode" in runtime:
        environment["LAMBO_WARP_MODE"] = str(runtime["warp_mode"])
    if "state_load" in runtime:
        environment["LAMBO_STATE_LOAD"] = runtime["state_load"]

    inputs = runtime["input"]
    if "replay" in inputs:
        environment["LAMBO_INPUT_REPLAY"] = inputs["replay"]
        environment["LAMBO_INPUT_EXIT_ON_END"] = "1" if inputs.get("exit_on_end", True) else "0"
        if "start_state" in inputs:
            environment["LAMBO_INPUT_START_STATE"] = str(inputs["start_state"])
        if "start_delay" in inputs:
            environment["LAMBO_INPUT_START_DELAY"] = str(inputs["start_delay"])

    capture = runtime.get("capture")
    if capture:
        capture_path = Path(capture["path"])
        run_path = (artifact / "capture" / capture_path).resolve()
        run_path.parent.mkdir(parents=True, exist_ok=True)
        capture["run_path"] = str(run_path)
        environment["LAMBO_DL_RENDER_OUT"] = str(run_path)
        if "state" in capture:
            environment["LAMBO_DL_RENDER_STATE"] = str(capture["state"])
        if "every" in capture:
            environment["LAMBO_DL_RENDER_EVERY"] = str(capture["every"])
    return environment


def command_for(executable: Path) -> list[str]:
    return ([sys.executable, str(executable), "--console", "--verbose"]
            if executable.suffix.lower() in {".py", ".pyw"}
            else [str(executable), "--console", "--verbose"])


def run_process(command: list[str], environment: dict[str, str], cwd: Path,
                timeout: float) -> tuple[int | None, str, str, bool]:
    process = subprocess.Popen(
        command, cwd=cwd, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", errors="replace", start_new_session=os.name != "nt"
    )
    try:
        stdout, stderr = process.communicate(timeout=timeout)
        return process.returncode, stdout, stderr, False
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        stdout, stderr = process.communicate()
        return process.returncode, stdout, stderr, True


def load_native_result(path: Path) -> dict[str, Any]:
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ScenarioError(f"native result is missing or invalid: {error}") from error
    result = _dict(result, "native result")
    if result.get("schema") != SCHEMA_VERSION:
        raise ScenarioError(f"native result.schema must be {SCHEMA_VERSION}")
    return result


def evaluate(scenario: dict[str, Any], result: dict[str, Any], returncode: int | None) -> list[str]:
    failures: list[str] = []
    if returncode != 0:
        failures.append(f"process exited {returncode}")
    if result.get("exit_code") != returncode:
        failures.append(f"native exit_code {result.get('exit_code')} != process exit {returncode}")
    if result.get("outcome") not in {"ok", "pass", "passed", "success", "completed"}:
        failures.append(f"native outcome {result.get('outcome')!r}: {result.get('reason')}")

    replay = _dict(result.get("replay", {}), "native result.replay")
    input_config = scenario["input"]
    replay_requested = "replay" in input_config
    if bool(replay.get("configured")) != replay_requested:
        failures.append("native replay configuration does not match scenario")
    if replay_requested:
        if replay.get("failed"):
            failures.append("native replay reported failure")
        if not replay.get("active"):
            failures.append("native replay never became active")
        if replay.get("frames_consumed", 0) == 0:
            failures.append("native replay did not consume any frames")
        if replay.get("guest_frames_verified") != replay.get("frames_consumed"):
            failures.append("guest input verification did not cover every consumed frame")
        expected_complete = scenario["expect"].get("replay_complete", True)
        if bool(replay.get("complete")) != expected_complete:
            failures.append(f"replay_complete was {replay.get('complete')}, expected {expected_complete}")
        if expected_complete and replay.get("frames_consumed") != replay.get("total_frames"):
            failures.append("replay completed before consuming the full trace")

    warp = _dict(result.get("warp", {}), "native result.warp")
    if bool(warp.get("requested")) != ("warp" in scenario):
        failures.append("native warp configuration does not match scenario")
    if "warp" in scenario:
        expected = [int(part) for part in scenario["warp"].split(":")]
        expected += [3, 0, 1][: 4 - len(expected)]
        for key, value in zip(("circuit", "laps", "car", "players"), expected):
            if warp.get(key) != value:
                failures.append(f"warp {key} {warp.get(key)} != requested {value}")
        if not warp.get("applied") or warp.get("failed"):
            failures.append("native warp was not applied cleanly")
        if result.get("loaded_circuit") != expected[0]:
            failures.append(f"loaded_circuit {result.get('loaded_circuit')} != requested {expected[0]}")

    if "state_load" in scenario:
        state_load = _dict(result.get("state_load", {}), "native result.state_load")
        if not state_load.get("applied") or state_load.get("failed"):
            failures.append("native state load was not applied cleanly")

    expected = scenario["expect"]
    if "max_state_at_least" in expected and result.get("max_state", 0) < expected["max_state_at_least"]:
        failures.append(f"max_state {result.get('max_state')} < {expected['max_state_at_least']}")
    if "min_swaps" in expected and result.get("swaps", 0) < expected["min_swaps"]:
        failures.append(f"swaps {result.get('swaps')} < {expected['min_swaps']}")
    if "min_abs_player_speed" in expected and result.get("max_abs_player_speed", 0) < expected["min_abs_player_speed"]:
        failures.append(
            f"max_abs_player_speed {result.get('max_abs_player_speed')} < {expected['min_abs_player_speed']}"
        )
    if expected.get("capture"):
        capture = scenario.get("capture", {})
        path = Path(capture.get("run_path", ""))
        if not path.is_file() or path.stat().st_size == 0:
            failures.append(f"capture was not written: {path}")
    return failures


def write_artifacts(artifact: Path, original: Path, runtime: dict[str, Any],
                    environment: dict[str, str], stdout: str, stderr: str) -> None:
    (artifact / "stdout.log").write_text(stdout, encoding="utf-8")
    (artifact / "stderr.log").write_text(stderr, encoding="utf-8")
    shutil.copy2(original, artifact / "scenario.json")
    (artifact / "scenario-resolved.json").write_text(
        json.dumps(runtime, indent=2, default=str) + "\n", encoding="utf-8"
    )
    selected = {key: environment[key] for key in sorted(environment) if key.startswith("LAMBO_")}
    (artifact / "harness-environment.json").write_text(
        json.dumps(selected, indent=2) + "\n", encoding="utf-8"
    )


def write_runner_result(artifact: Path, scenario: str, executable: Path,
                        returncode: int | None, timed_out: bool,
                        failures: list[str], native: dict[str, Any] | None) -> None:
    (artifact / "runner-result.json").write_text(json.dumps({
        "schema": SCHEMA_VERSION, "outcome": "failed" if failures else "passed",
        "scenario": scenario, "executable": str(executable),
        "process_exit_code": returncode, "timed_out": timed_out,
        "failures": failures, "native_result": native,
    }, indent=2) + "\n", encoding="utf-8")


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", type=Path)
    parser.add_argument("--exe", help="path to lamborghini_modern (or a .py test double)")
    parser.add_argument("--artifacts-dir", type=Path)
    parser.add_argument("--timeout", type=float)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    repository = Path(__file__).resolve().parents[1]
    scenario_path = args.scenario.expanduser().resolve()
    try:
        scenario = load_scenario(scenario_path)
        executable = find_executable(args.exe, repository)
        root = (args.artifacts_dir.expanduser().resolve()
                if args.artifacts_dir else repository / "artifacts" / "game-scenarios")
        artifact = create_artifacts(root, scenario["name"])
        runtime = stage_fixtures(scenario, artifact)
        environment = build_environment(scenario, runtime, artifact)
    except (OSError, ScenarioError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    timeout = args.timeout or float(scenario.get("timeout_seconds", DEFAULT_TIMEOUT_SECONDS))
    try:
        returncode, stdout, stderr, timed_out = run_process(
            command_for(executable), environment, repository, timeout
        )
    except OSError as error:
        returncode, stdout, stderr, timed_out = None, "", str(error), False
    try:
        write_artifacts(artifact, scenario_path, runtime, environment, stdout, stderr)
    except OSError as error:
        print(f"FAIL {scenario['name']}: could not preserve artifacts: {error}")
        return 1

    failures = [f"timed out after {timeout:g}s"] if timed_out else []
    native: dict[str, Any] | None = None
    try:
        native = load_native_result(artifact / "harness-result.json")
    except ScenarioError as error:
        failures.append(str(error))
    if native is not None:
        failures.extend(evaluate(runtime, native, returncode))
    try:
        write_runner_result(artifact, scenario["name"], executable, returncode,
                            timed_out, failures, native)
    except OSError as error:
        print(f"FAIL {scenario['name']}: could not write runner result: {error}")
        return 1

    if failures:
        print(f"FAIL {scenario['name']}: {'; '.join(failures)} (artifacts={artifact})")
        return 1
    replay = native["replay"]
    print(f"PASS {scenario['name']}: vis={native['vis']} swaps={native['swaps']} "
          f"max_state={native['max_state']} "
          f"replay={replay['frames_consumed']}/{replay['total_frames']} "
          f"(artifacts={artifact})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
