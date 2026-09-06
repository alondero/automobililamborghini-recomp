"""Measure model cursors with difficulty held fixed.

Each invocation writes a schema shared with the retained evidence: campaign
metadata lives at the top level and the selected scenario contains a
configuration plus successful ``runs``. Failed attempts are retained in a
separate list and make the process exit non-zero; they are never converted
into measurements.
"""
import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ROM = ROOT / "Automobili Lamborghini (USA).z64"
CATEGORIES = (0, 4, 1, 2, 3, 5, 6, 7)
# Maximum gear values from the engine rows' +8 halfword at ROM 0xAFAE0.
# The neutral gear is zero, so these are maxima rather than counts.
MAX_GEAR_BY_CATEGORY = (5, 6, 6, 6, 5, 6, 5, 5)
FRAME = re.compile(r"F (\d+) st (\d+) sel (\d+) cur (\d+) slot (-?\d+)")
RECORD = re.compile(r"R (\d+) ch (\d+) cat (\d+) sp (-?\d+).* rec ([0-9A-F]+) ")
GEAR_STATE_OFFSET = 0xA8
STEERING_ACCUM_OFFSET = 0xB4
POSITION_OFFSET = 0x14
SCRATCH_OFFSET = 0xD0


def read_text(path):
    return path.read_text(encoding="utf-8", errors="replace")


def summarize(path, model, difficulty):
    samples = []
    frame = None
    for line in read_text(path).splitlines():
        if match := FRAME.fullmatch(line):
            frame, state, actual_difficulty, _, _ = map(int, match.groups())
            continue
        match = RECORD.match(line)
        if not match or frame is None or state != 8:
            continue
        slot, channel, category, speed = map(int, match.groups()[:4])
        data = bytes.fromhex(match[5])
        if channel != 1 or category != CATEGORIES[model // 3]:
            continue
        if actual_difficulty != difficulty:
            raise ValueError(f"{path}: difficulty changed to {actual_difficulty}")
        samples.append((frame, speed, slot, data))
    if not samples or max(s[1] for s in samples) <= 0:
        raise ValueError(f"{path}: no moving player with expected category")
    if len({s[2] for s in samples}) != 1:
        raise ValueError(f"{path}: ambiguous player body")

    gear_states = [struct.unpack_from(">h", s[3], GEAR_STATE_OFFSET)[0] for s in samples]
    max_gear = MAX_GEAR_BY_CATEGORY[CATEGORIES[model // 3]]
    if gear_states[0] != 0 or any(gear < 0 or gear > max_gear for gear in gear_states):
        raise ValueError(f"{path}: invalid automatic gear sequence: {sorted(set(gear_states))}")
    if max(s[1] for s in samples) >= 50 and max(gear_states) == 0:
        raise ValueError(f"{path}: automatic gearbox never left neutral")

    launch = next(s[0] for s in samples if s[1] > 0)
    thresholds = {str(t): next((s[0] - launch for s in samples if s[1] >= t), None)
                  for t in (50, 100, 150)}
    commands = [struct.unpack_from(">h", s[3], STEERING_ACCUM_OFFSET)[0]
                for s in samples if s[0] <= launch + 80]
    end100 = next((i for i, s in enumerate(samples) if s[1] >= 100), None)
    launch_window = {}
    if end100 is not None:
        prefix = [s for s in samples[:end100 + 1] if s[0] >= launch]
        positions = [struct.unpack_from(">3f", s[3], POSITION_OFFSET) for s in prefix]
        dx, dz = positions[-1][0] - positions[0][0], positions[-1][2] - positions[0][2]
        distance = math.hypot(dx, dz)
        deviation = max(abs((p[0] - positions[0][0]) * dz - (p[2] - positions[0][2]) * dx)
                        / distance for p in positions) if distance else None
        launch_window = {
            "minimum_speed_step_to_100": min((b[1] - a[1] for a, b in zip(prefix, prefix[1:])), default=0),
            "max_lateral_deviation_to_100": deviation,
        }
    return {
        "model_cursor": model, "category": CATEGORIES[model // 3],
        "difficulty": difficulty, "samples": len(samples), "launch_frame": launch,
        "updates_to_speed": thresholds, "peak_observed_speed": max(s[1] for s in samples),
        "scratch_at_launch": list(struct.unpack_from(">4f", next(s[3] for s in samples if s[1] > 0), SCRATCH_OFFSET)),
        "trace_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "launch_steering_slew": max((abs(b - a) for a, b in zip(commands, commands[1:])), default=0),
        "launch_window": launch_window,
        "automatic_gearbox_verified": True,
    }


def git_metadata():
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
        encoding="utf-8", errors="replace").strip()
    dirty = bool(subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=all"], cwd=ROOT,
        text=True, encoding="utf-8", errors="replace"))
    return revision, dirty


def failure_record(model, rep, difficulty, vis, error, trace, log, exit_code):
    record = {
        "model_cursor": model, "difficulty": difficulty, "rep": rep,
        "requested_vis": vis, "status": "failed", "error": str(error),
        "exit_code": exit_code,
    }
    if trace.exists():
        record["trace_sha256"] = hashlib.sha256(trace.read_bytes()).hexdigest()
        frames = re.findall(r"^F (\d+) ", read_text(trace), re.MULTILINE)
        if frames:
            record["last_trace_frame"] = int(frames[-1])
    if log.exists():
        log_text = read_text(log)
        record["controlled_exit_observed"] = f"reached {vis} VIs; quitting" in log_text
        record["native_crash_banner"] = "NATIVE CRASH" in log_text
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/lamborghini_modern.exe")
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--models", default="0,3,6,9,12,15,18,21")
    parser.add_argument("--difficulty", type=int, choices=(0, 1), default=0)
    parser.add_argument("--vis", type=int, default=1000)
    parser.add_argument("--reps", type=int, default=2)
    parser.add_argument("--scenario", choices=("launch", "steering"), default="launch")
    args = parser.parse_args()
    models = [int(v) for v in args.models.split(",")]
    if any(v < 0 or v >= 24 for v in models) or args.reps < 1 or args.vis < 1:
        parser.error("models must be 0-23; reps and vis must be positive")
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    executable = args.exe.resolve()
    if not executable.is_file():
        parser.error(f"missing executable: {executable}")
    source_revision, source_dirty = git_metadata()
    executable_sha256 = hashlib.sha256(executable.read_bytes()).hexdigest()
    rom_sha256 = hashlib.sha256(args.rom.resolve().read_bytes()).hexdigest() if args.rom.is_file() else None

    input_value = "8000" if args.scenario == "launch" else "8000:-80:0"
    scenario_config = {
        "headless": True, "warp": "1:2:<model_cursor>:1", "difficulty": args.difficulty,
        "mode": 0, "input": input_value, "max_vis": args.vis,
    }
    summary_path = args.out / "summary.json"
    if summary_path.exists():
        loaded_summary = json.loads(read_text(summary_path))
        summary = loaded_summary if isinstance(loaded_summary, dict) else {}
    else:
        summary = {}
    metadata = {"rom_sha256": rom_sha256, "executable_sha256": executable_sha256,
                "source_base_revision": source_revision, "source_dirty": source_dirty}
    if summary:
        mismatches = [key for key, value in metadata.items()
                      if key in summary and summary[key] != value]
        if mismatches:
            raise SystemExit("existing summary provenance differs for "
                             + ", ".join(mismatches) + "; choose a new --out directory")
        existing_scenario = summary.get(args.scenario)
        if isinstance(existing_scenario, dict):
            old_config = existing_scenario.get("configuration")
            if old_config is not None and old_config != scenario_config:
                raise SystemExit("existing summary configuration differs for "
                                 + args.scenario + "; choose a new --out directory")
    summary.update({"date": datetime.date.today().isoformat(), **metadata})
    summary[args.scenario] = {"configuration": scenario_config, "runs": []}
    summary.setdefault("excluded_failed_attempts", [])
    failures = 0
    for model in models:
        for rep in range(1, args.reps + 1):
            stem = f"{args.scenario}_model{model}_difficulty{args.difficulty}_rep{rep}"
            trace = args.out / (stem + ".txt")
            log = args.out / (stem + ".log")
            env = {k: v for k, v in os.environ.items() if not k.startswith("LAMBO_")}
            env.update(LAMBO_HEADLESS="1", LAMBO_WARP=f"1:2:{model}:1",
                       LAMBO_WARP_DIFFICULTY=str(args.difficulty), LAMBO_WARP_MODE="0",
                       LAMBO_CAR_TRACE="1", LAMBO_CAR_TRACE_PATH=str(trace),
                       LAMBO_MODERN_INPUT=input_value, LAMBO_MODERN_MAX_VIS=str(args.vis))
            print(stem, flush=True)
            result = None
            try:
                trace.unlink(missing_ok=True)
                with log.open("wb") as log_file:
                    subprocess.run([str(executable), "--lambo-debug"], cwd=ROOT, env=env,
                                   stdout=log_file, stderr=subprocess.STDOUT,
                                   timeout=180, check=True)
                log_text = read_text(log)
                if f"reached {args.vis} VIs; quitting" not in log_text:
                    raise RuntimeError("process exited without the controlled VI-cap message")
                result = summarize(trace, model, args.difficulty)
                result["rep"] = rep
                summary[args.scenario]["runs"].append(result)
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError,
                    ValueError, RuntimeError) as error:
                failures += 1
                exit_code = getattr(error, "returncode", None)
                summary["excluded_failed_attempts"].append(
                    failure_record(model, rep, args.difficulty, args.vis, error,
                                   trace, log, exit_code))
            summary_path.write_text(json.dumps(summary, indent=2) + "\n",
                                   encoding="utf-8", newline="\n")
            print(json.dumps(result if result is not None else summary["excluded_failed_attempts"][-1]), flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
