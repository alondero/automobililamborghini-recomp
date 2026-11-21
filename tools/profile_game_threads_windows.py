"""Run the issue 426 Windows experiment with isolated settings and a RIP sampler."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import time

import run_game_scenario as scenario_runner


ROOT = Path(__file__).resolve().parents[1]
GRAPHICS = {
    "api_option": "D3D12", "res_option": "Original2x", "msaa_option": "None",
    "rr_option": "Display", "rt_shadows": False, "texture_pack": "",
    "wm_option": "Windowed", "window_width": 1280, "window_height": 720,
    "no_lod": True, "draw_distance": 1.5,
}


def profile(executable: Path, sampler: Path, artifacts: Path, workload: str) -> bool:
    scenario_path = ROOT / "scenarios/circuit-1-time-trial-car-0.json"
    scenario = scenario_runner.load_scenario(scenario_path)
    scenario["headless"] = False
    # The source scenario's BMP assertion belongs to its software renderer.
    # This experiment measures the windowed RT64 path without image capture.
    scenario.pop("capture", None)
    scenario["expect"].pop("capture", None)
    if workload == "4p":
        scenario.update(warp="1:3:0:4", warp_mode=2)
    artifact = scenario_runner.create_artifacts(artifacts, workload)
    runtime = scenario_runner.stage_fixtures(scenario, artifact)
    environment = scenario_runner.build_environment(scenario, runtime, artifact)
    environment.update(RT64_RENDER_COST_REPORT="1", LAMBO_LOG_DIR=str(artifact / "logs"))
    (artifact / "graphics.json").write_text(json.dumps(GRAPHICS, indent=2) + "\n", encoding="utf-8")
    error_path = artifact / "stderr.log"
    sampler_status = None
    with (artifact / "stdout.log").open("w", encoding="utf-8") as output, \
            error_path.open("w", encoding="utf-8") as errors:
        process = subprocess.Popen([str(executable), "--console", "--verbose"],
                                   cwd=ROOT, env=environment, stdout=output, stderr=errors)
        try:
            print(f"{workload}: pid={process.pid} artifacts={artifact}", flush=True)
            deadline = time.monotonic() + 40
            while process.poll() is None and time.monotonic() < deadline:
                if "prepare commands=" in error_path.read_text(encoding="utf-8", errors="replace"):
                    break
                time.sleep(0.2)
            else:
                raise RuntimeError("No prepared graphics task within 40 seconds; inspect stderr.log")
            time.sleep(10)  # Exclude startup before the bounded 15-second sample.
            with (artifact / "samples.csv").open("w") as samples, \
                    (artifact / "threads.log").open("w") as counters:
                command = [str(sampler), str(process.pid), "15"]
                (artifact / "sampler-command.json").write_text(json.dumps(command) + "\n")
                sampler_status = subprocess.run(command, stdout=samples, stderr=counters,
                                                timeout=25, check=False).returncode
            process.wait(timeout=100)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    result = scenario_runner.load_native_result(artifact / "harness-result.json")
    failures = scenario_runner.evaluate(runtime, result, process.returncode,
                                       error_path.read_text(encoding="utf-8", errors="replace"))
    if sampler_status != 0:
        failures.append(f"sampler exited {sampler_status}")
    (artifact / "profile-result.json").write_text(json.dumps({
        "workload": workload, "sampler_status": sampler_status, "failures": failures,
        "harness": result,
    }, indent=2) + "\n", encoding="utf-8")
    print(f"{'FAIL' if failures else 'PASS'} {workload}: {failures}", flush=True)
    return not failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--sampler", required=True, type=Path)
    parser.add_argument("--artifacts-dir", type=Path, default=ROOT / "artifacts/game-thread-profile")
    parser.add_argument("--workload", choices=("1p", "4p", "both"), default="both")
    args = parser.parse_args()
    executable, sampler = args.exe.resolve(), args.sampler.resolve()
    if not executable.is_file() or not sampler.is_file():
        parser.error("--exe and --sampler must be existing executables")
    workloads = ("1p", "4p") if args.workload == "both" else (args.workload,)
    try:
        passed = [profile(executable, sampler, args.artifacts_dir.resolve(), case) for case in workloads]
        return 0 if all(passed) else 1
    except (OSError, RuntimeError, subprocess.TimeoutExpired, scenario_runner.ScenarioError) as error:
        print(f"FAIL: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
