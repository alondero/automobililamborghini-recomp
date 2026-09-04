#!/usr/bin/env python3
"""Run one deterministic Lamborghini scenario and evaluate its result.

The game process owns simulation and replay.  This wrapper owns isolation,
timeouts, artifact collection, and the small set of assertions that make a run
useful in automation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
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

# These are the variables this runner may set and records in its manifest.  All
# inherited LAMBO_* knobs are cleared except asset discovery: rendering tweaks,
# one-off probes, and old input drivers would make a scenario non-reproducible.
MANAGED_ENVIRONMENT = {
    "LAMBO_ANALOG_BRAKE",
    "LAMBO_ANALOG_BRAKE_PROBE",
    "LAMBO_ANALOG_THROTTLE",
    "LAMBO_ANALOG_THROTTLE_PROBE",
    "LAMBO_CONTROLLER_PAK",
    "LAMBO_CONTROLLER_PAK_FILE",
    "LAMBO_CRASH_TEST",
    "LAMBO_DL_RENDER_EVERY",
    "LAMBO_DL_RENDER_OUT",
    "LAMBO_DL_RENDER_STATE",
    "LAMBO_GRAPHICS_CONFIG",
    "LAMBO_HARNESS_RESULT",
    "LAMBO_HEADLESS",
    "LAMBO_INPUT_EXIT_ON_END",
    "LAMBO_INPUT_PULSE",
    "LAMBO_INPUT_RECORD",
    "LAMBO_INPUT_REPLAY",
    "LAMBO_INPUT_START_DELAY",
    "LAMBO_INPUT_START_STATE",
    "LAMBO_LAUNCHER",
    "LAMBO_LIGHTING_SELFTEST",
    "LAMBO_MODERN_INPUT",
    "LAMBO_MODERN_MAX_VIS",
    "LAMBO_STATE_FILE",
    "LAMBO_STATE_LOAD",
    "LAMBO_STATE_LOAD_DELAY",
    "LAMBO_STATE_LOAD_STATE",
    "LAMBO_STATE_SAVE",
    "LAMBO_STATE_SAVE_DELAY",
    "LAMBO_STATE_SAVE_STATE",
    "LAMBO_WARP",
    "LAMBO_WARP_MODE",
}
PRESERVED_LAMBO_ENVIRONMENT = {"LAMBO_ASSET_DIR"}

TOP_LEVEL_KEYS = {
    "schema",
    "name",
    "headless",
    "warp",
    "warp_mode",
    "state_load",
    "input",
    "max_vis",
    "timeout_seconds",
    "capture",
    "expect",
}
INPUT_KEYS = {"replay", "start_state", "start_delay", "exit_on_end"}
CAPTURE_KEYS = {"path", "state", "every"}
EXPECTATION_KEYS = {
    "max_state_at_least",
    "replay_complete",
    "min_swaps",
    "min_abs_player_speed",
    "capture",
}


class ScenarioError(ValueError):
    """A user-facing scenario or result validation error."""


def _integer(value: Any, label: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ScenarioError(f"{label} must be an integer >= {minimum}")
    return value


def _signed_integer(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ScenarioError(f"{label} must be an integer")
    return value


def _boolean(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise ScenarioError(f"{label} must be a boolean")
    return value


def _mapping(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ScenarioError(f"{label} must be an object")
    return value


def _reject_unknown_keys(value: dict[str, Any], allowed: set[str], label: str) -> None:
    unknown = sorted(set(value) - allowed)
    if unknown:
        raise ScenarioError(f"{label} has unknown field(s): {', '.join(unknown)}")


def _path(value: Any, label: str, scenario_directory: Path) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise ScenarioError(f"{label} must be a non-empty path string")
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = scenario_directory / path
    return path.resolve()


def _capture_path(value: Any, label: str) -> Path:
    if not isinstance(value, str) or not value.strip():
        raise ScenarioError(f"{label} must be a non-empty relative path string")
    path = Path(value)
    if path.is_absolute() or path.anchor or ".." in path.parts:
        raise ScenarioError(f"{label} must stay inside the run artifact directory")
    return path


def _parse_warp(value: str | int) -> tuple[str, dict[str, int]]:
    text = str(value)
    parts = text.split(":")
    if not 1 <= len(parts) <= 4 or any(not re.fullmatch(r"[0-9]+", part) for part in parts):
        raise ScenarioError(
            "scenario.warp must use circuit[:laps[:car[:players]]] with decimal integers"
        )
    values = [int(part) for part in parts]
    values.extend((3, 0, 1)[len(values) - 1 :])
    circuit, laps, car, players = values
    if not 1 <= circuit <= 6:
        raise ScenarioError("scenario.warp circuit must be in [1, 6]")
    if not 1 <= laps <= 30:
        raise ScenarioError("scenario.warp laps must be in [1, 30]")
    if car != 0:
        raise ScenarioError(
            "scenario.warp currently supports only runtime-verified car 0"
        )
    if not 1 <= players <= 4:
        raise ScenarioError("scenario.warp players must be in [1, 4]")
    if players >= 3 and circuit > 3:
        raise ScenarioError("scenario.warp circuits 4-6 are unavailable with 3-4 players")
    return text, {
        "circuit": circuit,
        "laps": laps,
        "car": car,
        "players": players,
    }


def load_scenario(path: Path) -> dict[str, Any]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ScenarioError(f"cannot read scenario: {error}") from error
    except json.JSONDecodeError as error:
        raise ScenarioError(
            f"invalid scenario JSON at line {error.lineno}, column {error.colno}: {error.msg}"
        ) from error

    scenario = _mapping(raw, "scenario")
    _reject_unknown_keys(scenario, TOP_LEVEL_KEYS, "scenario")
    schema = scenario.get("schema")
    if isinstance(schema, bool) or not isinstance(schema, int) or schema != SCHEMA_VERSION:
        raise ScenarioError(f"scenario.schema must be {SCHEMA_VERSION}")

    name = scenario.get("name", path.stem)
    if not isinstance(name, str) or not name.strip():
        raise ScenarioError("scenario.name must be a non-empty string")

    if "headless" in scenario:
        _boolean(scenario["headless"], "scenario.headless")
    if "warp" in scenario and (
        isinstance(scenario["warp"], bool)
        or not isinstance(scenario["warp"], (str, int))
        or not str(scenario["warp"]).strip()
    ):
        raise ScenarioError("scenario.warp must be a non-empty string or integer")
    if "warp_mode" in scenario:
        warp_mode = _integer(scenario["warp_mode"], "scenario.warp_mode")
        if "warp" not in scenario:
            raise ScenarioError("scenario.warp_mode requires scenario.warp")
        if warp_mode not in {0, 2}:
            raise ScenarioError("scenario.warp_mode must be 0 (time trial) or 2 (single race)")
    if "warp" in scenario and "state_load" in scenario:
        raise ScenarioError(
            "scenario.warp and scenario.state_load are alternative bootstraps; choose one"
        )

    if "max_vis" in scenario:
        _integer(scenario["max_vis"], "scenario.max_vis", minimum=1)
    if "timeout_seconds" in scenario:
        timeout = scenario["timeout_seconds"]
        if (
            isinstance(timeout, bool)
            or not isinstance(timeout, (int, float))
            or not math.isfinite(timeout)
            or timeout <= 0
        ):
            raise ScenarioError("scenario.timeout_seconds must be a number > 0")

    base = path.parent
    resolved = dict(scenario)
    resolved["name"] = name.strip()
    if "warp" in scenario:
        resolved["warp"], resolved["_warp_expected"] = _parse_warp(scenario["warp"])
    if "state_load" in scenario:
        resolved["state_load"] = _path(scenario["state_load"], "scenario.state_load", base)

    input_config = _mapping(scenario.get("input", {}), "scenario.input")
    _reject_unknown_keys(input_config, INPUT_KEYS, "scenario.input")
    resolved_input = dict(input_config)
    if "replay" in input_config:
        resolved_input["replay"] = _path(
            input_config["replay"], "scenario.input.replay", base
        )
    if "start_state" in input_config:
        _integer(input_config["start_state"], "scenario.input.start_state")
    if "start_delay" in input_config:
        _integer(input_config["start_delay"], "scenario.input.start_delay")
    if "exit_on_end" in input_config:
        _boolean(input_config["exit_on_end"], "scenario.input.exit_on_end")
    if "replay" not in input_config and set(input_config) & {
        "start_state",
        "start_delay",
        "exit_on_end",
    }:
        raise ScenarioError("scenario.input start/exit fields require scenario.input.replay")
    resolved["input"] = resolved_input

    if "capture" in scenario:
        capture = _mapping(scenario["capture"], "scenario.capture")
        _reject_unknown_keys(capture, CAPTURE_KEYS, "scenario.capture")
        if "path" not in capture:
            raise ScenarioError("scenario.capture.path is required")
        resolved_capture = dict(capture)
        resolved_capture["path"] = _capture_path(
            capture["path"], "scenario.capture.path"
        )
        if "state" in capture:
            _integer(capture["state"], "scenario.capture.state")
        if "every" in capture:
            _integer(capture["every"], "scenario.capture.every", minimum=1)
        resolved["capture"] = resolved_capture

    expectations = _mapping(scenario.get("expect", {}), "scenario.expect")
    _reject_unknown_keys(expectations, EXPECTATION_KEYS, "scenario.expect")
    if "max_state_at_least" in expectations:
        _integer(expectations["max_state_at_least"], "scenario.expect.max_state_at_least")
    if "replay_complete" in expectations:
        _boolean(expectations["replay_complete"], "scenario.expect.replay_complete")
    if "min_swaps" in expectations:
        _integer(expectations["min_swaps"], "scenario.expect.min_swaps")
    if "min_abs_player_speed" in expectations:
        _integer(
            expectations["min_abs_player_speed"], "scenario.expect.min_abs_player_speed"
        )
    if "capture" in expectations:
        _boolean(expectations["capture"], "scenario.expect.capture")
        if expectations["capture"] and "capture" not in resolved:
            raise ScenarioError("scenario.expect.capture requires scenario.capture")
    resolved["expect"] = dict(expectations)

    for source, label in (
        (resolved.get("state_load"), "state-load"),
        (resolved_input.get("replay"), "replay"),
    ):
        if source is not None and not source.is_file():
            raise ScenarioError(f"{label} file does not exist: {source}")
    return resolved


def find_executable(override: str | None, repository: Path) -> Path:
    if override:
        candidate = Path(override).expanduser()
        if not candidate.is_absolute():
            candidate = Path.cwd() / candidate
        candidate = candidate.resolve()
        if not candidate.is_file():
            raise ScenarioError(f"executable does not exist: {candidate}")
        return candidate

    executable_names = (
        ("lamborghini_modern.exe", "lamborghini_modern")
        if os.name == "nt"
        else ("lamborghini_modern", "lamborghini_modern.exe")
    )
    build_roots = [repository / "build"]
    build_roots.extend(
        path for path in sorted(repository.glob("build-*")) if path.is_dir()
    )
    configurations = (Path(), Path("Release"), Path("Debug"), Path("RelWithDebInfo"))
    for build_root in build_roots:
        for configuration in configurations:
            for name in executable_names:
                candidate = build_root / configuration / name
                if candidate.is_file():
                    return candidate.resolve()

    for name in executable_names:
        found = shutil.which(name)
        if found:
            return Path(found).resolve()
    raise ScenarioError("could not find lamborghini_modern; pass --exe PATH")


def create_artifact_directory(root: Path, scenario_name: str) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    slug = re.sub(r"[^A-Za-z0-9_.-]+", "-", scenario_name).strip("-.") or "scenario"
    return Path(tempfile.mkdtemp(prefix=f"{slug}-", dir=root)).resolve()


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def stage_fixtures(
    scenario: dict[str, Any], scenario_path: Path, artifact_directory: Path
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Freeze replay/save inputs in the run directory and return a rerunnable scenario."""

    resolved_document = json.loads(scenario_path.read_text(encoding="utf-8"))
    fixtures: list[dict[str, Any]] = []
    fixture_root = artifact_directory / "fixtures"

    def stage(source: Path, role: str) -> tuple[Path, str]:
        fixture_root.mkdir(parents=True, exist_ok=True)
        suffix = "".join(source.suffixes)
        destination = fixture_root / f"{role}{suffix}"
        shutil.copy2(source, destination)
        relative = destination.relative_to(artifact_directory).as_posix()
        fixtures.append(
            {
                "role": role,
                "source": str(source),
                "artifact": relative,
                "bytes": destination.stat().st_size,
                "sha256": _sha256(destination),
            }
        )
        return destination.resolve(), relative

    if "state_load" in scenario:
        staged, relative = stage(scenario["state_load"], "state-load")
        scenario["state_load"] = staged
        resolved_document["state_load"] = relative

    input_config = scenario["input"]
    if "replay" in input_config:
        staged, relative = stage(input_config["replay"], "input-replay")
        input_config["replay"] = staged
        resolved_input = resolved_document.setdefault("input", {})
        resolved_input["replay"] = relative

    return resolved_document, fixtures


def build_environment(scenario: dict[str, Any], artifact_directory: Path) -> dict[str, str]:
    environment = os.environ.copy()
    for name in tuple(environment):
        if name.startswith("LAMBO_") and name not in PRESERVED_LAMBO_ENVIRONMENT:
            environment.pop(name, None)

    harness_result = artifact_directory / "harness-result.json"
    environment.update(
        {
            "LAMBO_HEADLESS": "1" if scenario.get("headless", True) else "0",
            "LAMBO_GRAPHICS_CONFIG": str(artifact_directory / "graphics.json"),
            "LAMBO_CONTROLLER_PAK_FILE": str(artifact_directory / "controller.mpk"),
            "LAMBO_HARNESS_RESULT": str(harness_result),
            "LAMBO_MODERN_MAX_VIS": str(scenario.get("max_vis", DEFAULT_MAX_VIS)),
        }
    )

    if "warp" in scenario:
        environment["LAMBO_WARP"] = str(scenario["warp"])
    if "warp_mode" in scenario:
        environment["LAMBO_WARP_MODE"] = str(scenario["warp_mode"])
    if "state_load" in scenario:
        environment["LAMBO_STATE_LOAD"] = str(scenario["state_load"])

    input_config = scenario["input"]
    if "replay" in input_config:
        environment["LAMBO_INPUT_REPLAY"] = str(input_config["replay"])
        # A replay is normally a complete autonomous run.  Scenarios may opt out
        # when they intentionally want the VI cap to be the stopping condition.
        environment["LAMBO_INPUT_EXIT_ON_END"] = (
            "1" if input_config.get("exit_on_end", True) else "0"
        )
    elif "exit_on_end" in input_config:
        environment["LAMBO_INPUT_EXIT_ON_END"] = (
            "1" if input_config["exit_on_end"] else "0"
        )
    if "start_state" in input_config:
        environment["LAMBO_INPUT_START_STATE"] = str(input_config["start_state"])
    if "start_delay" in input_config:
        environment["LAMBO_INPUT_START_DELAY"] = str(input_config["start_delay"])

    capture = scenario.get("capture")
    if capture is not None:
        capture_root = artifact_directory / "capture"
        capture["run_path"] = (capture_root / capture["path"]).resolve()
        capture["run_path"].parent.mkdir(parents=True, exist_ok=True)
        environment["LAMBO_DL_RENDER_OUT"] = str(capture["run_path"])
        if "state" in capture:
            environment["LAMBO_DL_RENDER_STATE"] = str(capture["state"])
        if "every" in capture:
            environment["LAMBO_DL_RENDER_EVERY"] = str(capture["every"])
    return environment


def executable_command(executable: Path) -> list[str]:
    # A Python script is accepted as an executable override so the runner itself
    # can be tested on Windows without a ROM or a compiled game.
    if executable.suffix.lower() in {".py", ".pyw"}:
        return [sys.executable, str(executable), "--console", "--verbose"]
    return [str(executable), "--console", "--verbose"]


def run_process(
    command: list[str], environment: dict[str, str], cwd: Path, timeout: float
) -> tuple[int | None, str, str, bool]:
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        start_new_session=os.name != "nt",
    )
    try:
        stdout, stderr = process.communicate(timeout=timeout)
        return process.returncode, stdout, stderr, False
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            try:
                subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=5,
                    check=False,
                )
            except (OSError, subprocess.TimeoutExpired):
                process.kill()
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            except OSError:
                process.kill()
        if process.poll() is None:
            process.kill()
        try:
            stdout, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired as drain_error:
            # A descendant outside the tree can retain a copied pipe handle. Do
            # not let that defeat the scenario's wall-clock timeout.
            stdout_value = drain_error.output or b""
            stderr_value = drain_error.stderr or b""
            stdout = (
                stdout_value.decode("utf-8", errors="replace")
                if isinstance(stdout_value, bytes)
                else stdout_value
            )
            stderr = (
                stderr_value.decode("utf-8", errors="replace")
                if isinstance(stderr_value, bytes)
                else stderr_value
            )
            if process.stdout is not None:
                process.stdout.close()
            if process.stderr is not None:
                process.stderr.close()
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                pass
        return process.returncode, stdout, stderr, True


def load_native_result(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise ScenarioError("native result file is missing")
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ScenarioError(f"native result JSON is invalid: {error}") from error
    result = _mapping(result, "native result")

    schema = result.get("schema")
    if isinstance(schema, bool) or not isinstance(schema, int) or schema != SCHEMA_VERSION:
        raise ScenarioError(f"native result.schema must be {SCHEMA_VERSION}")
    for key in ("outcome", "reason"):
        if not isinstance(result.get(key), str):
            raise ScenarioError(f"native result.{key} must be a string")
    for key in (
        "exit_code",
        "vis",
        "swaps",
        "max_state",
        "final_state",
        "loaded_circuit",
    ):
        _integer(result.get(key), f"native result.{key}")
    _signed_integer(result.get("menu_screen"), "native result.menu_screen")
    _integer(result.get("player_vehicle"), "native result.player_vehicle", minimum=-1)
    _signed_integer(result.get("player_speed"), "native result.player_speed")
    _integer(result.get("max_abs_player_speed"), "native result.max_abs_player_speed")

    warp = _mapping(result.get("warp"), "native result.warp")
    for key in ("requested", "applied", "failed"):
        _boolean(warp.get(key), f"native result.warp.{key}")
    for key in ("circuit", "laps", "car", "players"):
        _integer(warp.get(key), f"native result.warp.{key}")
    _signed_integer(warp.get("mode"), "native result.warp.mode")

    state_load = _mapping(result.get("state_load"), "native result.state_load")
    for key in ("requested", "applied", "failed"):
        _boolean(state_load.get(key), f"native result.state_load.{key}")

    replay = _mapping(result.get("replay"), "native result.replay")
    for key in ("configured", "recording", "active", "complete", "failed"):
        _boolean(replay.get(key), f"native result.replay.{key}")
    for key in (
        "total_frames",
        "frames_consumed",
        "guest_frames_verified",
        "dispatcher_ticks",
    ):
        _integer(replay.get(key), f"native result.replay.{key}")
    return result


def evaluate(
    scenario: dict[str, Any], result: dict[str, Any], process_returncode: int
) -> list[str]:
    failures: list[str] = []
    if process_returncode != 0:
        failures.append(f"process exited {process_returncode}")
    if result["exit_code"] != process_returncode:
        failures.append(
            f"native exit_code {result['exit_code']} != process exit {process_returncode}"
        )
    if result["outcome"].lower() not in {"ok", "pass", "passed", "success", "completed"}:
        failures.append(f"native outcome {result['outcome']!r}: {result['reason']}")
    if result["replay"]["failed"]:
        failures.append("native replay reported failure")

    expected = scenario["expect"]
    replay_scenario = "replay" in scenario["input"]
    if result["replay"]["configured"] != replay_scenario:
        failures.append(
            f"native replay configured={result['replay']['configured']}, "
            f"expected {replay_scenario}"
        )
    if replay_scenario:
        if result["replay"]["recording"]:
            failures.append("native replay unexpectedly entered recording mode")
        if not result["replay"]["active"]:
            failures.append("native replay never became active")
        if result["replay"]["total_frames"] == 0:
            failures.append("native replay reported an empty trace")
        if result["replay"]["frames_consumed"] == 0:
            failures.append("native replay did not consume any frames")
        if (
            result["replay"]["guest_frames_verified"]
            != result["replay"]["frames_consumed"]
        ):
            failures.append(
                "guest input verification covered "
                f"{result['replay']['guest_frames_verified']}/"
                f"{result['replay']['frames_consumed']} consumed frames"
            )

    warp_scenario = "warp" in scenario
    warp_result = result["warp"]
    if warp_result["requested"] != warp_scenario:
        failures.append(
            f"native warp requested={warp_result['requested']}, expected {warp_scenario}"
        )
    if warp_scenario:
        if warp_result["failed"]:
            failures.append("native warp reported failure")
        if not warp_result["applied"]:
            failures.append("native warp was requested but never applied")
        requested_warp = dict(scenario["_warp_expected"])
        requested_warp["mode"] = scenario.get("warp_mode", 2)
        for field, requested in requested_warp.items():
            if warp_result[field] != requested:
                failures.append(
                    f"warp {field} {warp_result[field]} != requested {requested}"
                )
        if result["loaded_circuit"] != requested_warp["circuit"]:
            failures.append(
                f"loaded_circuit {result['loaded_circuit']} != requested "
                f"{requested_warp['circuit']}"
            )

    state_load_scenario = "state_load" in scenario
    load_result = result["state_load"]
    if load_result["requested"] != state_load_scenario:
        failures.append(
            "native state_load "
            f"requested={load_result['requested']}, expected {state_load_scenario}"
        )
    if state_load_scenario:
        if load_result["failed"]:
            failures.append("native state load reported failure")
        if not load_result["applied"]:
            failures.append("native state load was requested but never applied")
    minimum_state = expected.get("max_state_at_least")
    if minimum_state is not None and result["max_state"] < minimum_state:
        failures.append(f"max_state {result['max_state']} < {minimum_state}")
    require_replay_complete = expected.get(
        "replay_complete",
        replay_scenario,
    )
    if require_replay_complete:
        actual = result["replay"]["complete"]
        if not actual:
            failures.append("replay_complete was false, expected true")
        elif result["replay"]["frames_consumed"] != result["replay"]["total_frames"]:
            failures.append(
                "replay completed with "
                f"{result['replay']['frames_consumed']}/{result['replay']['total_frames']} frames"
            )
    elif expected.get("replay_complete") is False and result["replay"]["complete"]:
        failures.append("replay_complete was true, expected false")
    minimum_swaps = expected.get("min_swaps")
    if minimum_swaps is not None and result["swaps"] < minimum_swaps:
        failures.append(f"swaps {result['swaps']} < {minimum_swaps}")
    minimum_speed = expected.get("min_abs_player_speed")
    if minimum_speed is not None and result["max_abs_player_speed"] < minimum_speed:
        failures.append(
            f"max_abs_player_speed {result['max_abs_player_speed']} < {minimum_speed}"
        )
    if expected.get("capture"):
        capture_outputs = [
            path for path in _capture_outputs(scenario["capture"])
            if _complete_renderer_bmp(path)
        ]
        if not capture_outputs:
            failures.append(
                "capture was not written as a complete renderer BMP: "
                f"{scenario['capture']['run_path']}"
            )
    return failures


def _write_artifacts(
    artifact_directory: Path,
    scenario_path: Path,
    resolved_scenario: dict[str, Any],
    fixtures: list[dict[str, Any]],
    environment: dict[str, str],
    stdout: str,
    stderr: str,
) -> None:
    (artifact_directory / "stdout.log").write_text(stdout, encoding="utf-8")
    (artifact_directory / "stderr.log").write_text(stderr, encoding="utf-8")
    shutil.copy2(scenario_path, artifact_directory / "scenario.json")
    (artifact_directory / "scenario-resolved.json").write_text(
        json.dumps(resolved_scenario, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (artifact_directory / "fixtures-manifest.json").write_text(
        json.dumps({"schema": SCHEMA_VERSION, "fixtures": fixtures}, indent=2, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )
    harness_environment = {
        key: environment[key]
        for key in sorted(MANAGED_ENVIRONMENT | PRESERVED_LAMBO_ENVIRONMENT)
        if key in environment
    }
    (artifact_directory / "harness-environment.json").write_text(
        json.dumps(harness_environment, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def _write_runner_result(
    artifact_directory: Path,
    scenario_name: str,
    executable: Path,
    process_returncode: int | None,
    timed_out: bool,
    failures: list[str],
    native_result: dict[str, Any] | None,
) -> None:
    result = {
        "schema": SCHEMA_VERSION,
        "outcome": "failed" if failures else "passed",
        "scenario": scenario_name,
        "executable": str(executable),
        "process_exit_code": process_returncode,
        "timed_out": timed_out,
        "failures": failures,
        "native_result": native_result,
    }
    (artifact_directory / "runner-result.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def _capture_outputs(capture: dict[str, Any]) -> list[Path]:
    capture_path = capture["run_path"]
    captures = [capture_path] if capture_path.is_file() else []
    if capture_path.parent.is_dir():
        numbered_prefix = capture_path.name + "."
        for candidate in capture_path.parent.iterdir():
            name = candidate.name
            if not name.startswith(numbered_prefix) or not name.endswith(".bmp"):
                continue
            sequence = name[len(numbered_prefix) : -len(".bmp")]
            if candidate.is_file() and sequence.isdigit():
                captures.append(candidate)
    return captures


def _complete_renderer_bmp(path: Path) -> bool:
    """Accept only the complete 24-bit BMP shape emitted by the headless renderer."""

    try:
        size = path.stat().st_size
        with path.open("rb") as stream:
            header = stream.read(54)
    except OSError:
        return False
    if len(header) != 54 or header[:2] != b"BM":
        return False

    unsigned = lambda offset, width: int.from_bytes(
        header[offset : offset + width], "little", signed=False
    )
    signed = lambda offset: int.from_bytes(
        header[offset : offset + 4], "little", signed=True
    )
    declared_size = unsigned(2, 4)
    pixel_offset = unsigned(10, 4)
    dib_size = unsigned(14, 4)
    width = signed(18)
    height = signed(22)
    planes = unsigned(26, 2)
    bits_per_pixel = unsigned(28, 2)
    compression = unsigned(30, 4)
    image_size = unsigned(34, 4)
    if (
        width <= 0
        or height <= 0
        or planes != 1
        or bits_per_pixel != 24
        or compression != 0
        or dib_size != 40
        or pixel_offset != 54
    ):
        return False
    expected_image_size = ((width * 3 + 3) // 4) * 4 * height
    return (
        image_size == expected_image_size
        and declared_size == pixel_offset + expected_image_size
        and size == declared_size
    )


def positive_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a number") from error
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", type=Path, help="schema-1 scenario JSON")
    parser.add_argument("--exe", help="path to lamborghini_modern (or a .py test double)")
    parser.add_argument(
        "--artifacts-dir",
        type=Path,
        help="artifact parent directory (default: <repo>/artifacts/game-scenarios)",
    )
    parser.add_argument(
        "--timeout",
        type=positive_float,
        help=f"wall-clock timeout in seconds (default: scenario or {DEFAULT_TIMEOUT_SECONDS:g})",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    repository = Path(__file__).resolve().parents[1]
    scenario_path = args.scenario.expanduser().resolve()
    try:
        scenario = load_scenario(scenario_path)
        executable = find_executable(args.exe, repository)
    except ScenarioError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    artifact_root = (
        args.artifacts_dir.expanduser().resolve()
        if args.artifacts_dir is not None
        else repository / "artifacts" / "game-scenarios"
    )
    try:
        artifact_directory = create_artifact_directory(artifact_root, scenario["name"])
        resolved_scenario, fixtures = stage_fixtures(
            scenario, scenario_path, artifact_directory
        )
        environment = build_environment(scenario, artifact_directory)
    except OSError as error:
        artifact_note = (
            f" (artifacts={artifact_directory})" if "artifact_directory" in locals() else ""
        )
        print(f"ERROR: cannot prepare scenario artifacts: {error}{artifact_note}", file=sys.stderr)
        return 2

    timeout = args.timeout or float(scenario.get("timeout_seconds", DEFAULT_TIMEOUT_SECONDS))
    try:
        returncode, stdout, stderr, timed_out = run_process(
            executable_command(executable), environment, repository, timeout
        )
    except OSError as error:
        returncode, stdout, stderr, timed_out = None, "", str(error), False

    try:
        _write_artifacts(
            artifact_directory,
            scenario_path,
            resolved_scenario,
            fixtures,
            environment,
            stdout,
            stderr,
        )
    except OSError as error:
        print(
            f"FAIL {scenario['name']}: could not preserve artifacts: {error} "
            f"(artifacts={artifact_directory})"
        )
        return 1

    failures: list[str] = []
    if timed_out:
        failures.append(f"timed out after {timeout:g}s")
    if returncode is None:
        failures.append(f"could not start executable: {stderr}")

    result: dict[str, Any] | None = None
    try:
        result = load_native_result(artifact_directory / "harness-result.json")
    except ScenarioError as error:
        failures.append(str(error))

    if result is not None and returncode is not None:
        failures.extend(evaluate(scenario, result, returncode))

    try:
        _write_runner_result(
            artifact_directory,
            scenario["name"],
            executable,
            returncode,
            timed_out,
            failures,
            result,
        )
    except OSError as error:
        print(
            f"FAIL {scenario['name']}: could not write runner result: {error} "
            f"(artifacts={artifact_directory})"
        )
        return 1

    if failures:
        print(
            f"FAIL {scenario['name']}: {'; '.join(failures)} "
            f"(artifacts={artifact_directory})"
        )
        return 1

    assert result is not None
    replay = result["replay"]
    print(
        f"PASS {scenario['name']}: vis={result['vis']} swaps={result['swaps']} "
        f"max_state={result['max_state']} "
        f"replay={replay['frames_consumed']}/{replay['total_frames']} "
        f"(artifacts={artifact_directory})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
