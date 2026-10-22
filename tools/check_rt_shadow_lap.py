#!/usr/bin/env python3
"""Drive full time-trial laps with ray-traced shadows and report fallbacks.

The stationary validation matrix samples four tasks at the start line. This
check replays each circuit's recorded lap in a real D3D12 window, so tunnels,
bridges, camera turns and every scenery material the lap passes are covered.
It parses the production `[rt-shadow]` log and fails when race tasks fall back
to the native blob shadow more often than `--min-ready` allows.

Generated scenarios, logs and optional window screenshots stay under ignored
artifacts/. Requires a USA ROM build and a DXR 1.1 capable GPU.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import threading
import time
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ARTIFACT = re.compile(r"\(artifacts=(.+)\)\s*$")
SUMMARY = re.compile(r"\[rt-shadow\] lap_summary tasks=(\d+) ready=(\d+)(.*)$")
FALLBACK = re.compile(r"\[rt-shadow\] task=(\d+) native: (.+?)(?: circuit=| rejected=|$)")
WORKLOAD = re.compile(r"\[rt-shadow\] workload=\d+ task=(\d+) epoch=\d+ ready=(\d) reason=\"([^\"]*)\"")


def scenario(circuit: int, softness: float, rays: int, developer: dict | None = None) -> dict:
    base = json.loads((ROOT / "scenarios" / f"circuit-{circuit}-time-trial-car-0.json").read_text(encoding="utf-8"))
    base["name"] = f"rt-shadow-lap-c{circuit}"
    base["headless"] = False
    base["input"]["replay"] = str((ROOT / "scenarios" / base["input"]["replay"]).resolve())
    base["graphics"] = {"rt_shadows": True, "rt_shadow_rays": rays, "rt_shadow_softness": softness}
    base.pop("capture", None)
    base["expect"].pop("capture", None)
    if developer:
        base["developer_env"] = dict(developer)
    return base


def developer_env(pairs: list[str] | None) -> dict:
    """`KEY=VALUE` pairs for debug views and tuning, e.g. LAMBO_RT_SHADOW_DEBUG=2."""
    result = {}
    for pair in pairs or []:
        key, separator, value = pair.partition("=")
        if not separator or not key:
            raise ValueError(f"expected KEY=VALUE, got {pair!r}")
        result[key] = value
    return result


def summarize(stderr: str) -> dict:
    """Readiness from the production log.

    Current builds print a `lap_summary` line per summary window. Older builds
    only log fallbacks and ready-state changes, so those are reconstructed.
    """
    tasks = ready = 0
    reasons: Counter[str] = Counter()
    summaries = 0
    for line in stderr.splitlines():
        match = SUMMARY.search(line)
        if match:
            summaries += 1
            tasks += int(match.group(1))
            ready += int(match.group(2))
            for item in re.findall(r"(\w+)=(\d+)", match.group(3)):
                if item[0] != "tasks":
                    reasons[item[0]] += int(item[1])
    if summaries:
        return {"source": "lap_summary", "tasks": tasks, "ready": ready, "reasons": dict(reasons)}

    fallback_tasks = set()
    seen_tasks = set()
    for line in stderr.splitlines():
        match = FALLBACK.search(line)
        if match:
            fallback_tasks.add(int(match.group(1)))
            reasons[match.group(2).strip()] += 1
            continue
        match = WORKLOAD.search(line)
        if match:
            seen_tasks.add(int(match.group(1)))
            if match.group(2) == "0":
                reasons["renderer: " + (match.group(3) or "unknown")] += 1
    span = (max(seen_tasks | fallback_tasks) - min(seen_tasks | fallback_tasks) + 1) if seen_tasks | fallback_tasks else 0
    return {"source": "reconstructed", "tasks": span, "ready": max(0, span - len(fallback_tasks)),
            "reasons": dict(reasons)}


def capture_window(stop: threading.Event, every: float, directory: Path) -> None:
    sys.path.insert(0, str(ROOT / "tools"))
    import drive_input  # Windows only; imported lazily so parsing works anywhere.

    drive_input.initialize_windows()
    directory.mkdir(parents=True, exist_ok=True)
    index = 0
    while not stop.wait(every):
        try:
            window, _ = drive_input.find_window()
            if window:
                drive_input.shot(window, str(directory / f"shot-{index:03d}.png"))
                index += 1
        except Exception as error:  # A window that closes mid-capture ends the run.
            print(f"capture skipped: {error}", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--circuits", type=int, nargs="+", default=[1, 2, 3, 4, 5, 6])
    parser.add_argument("--softness", type=float, default=0.5)
    parser.add_argument("--rays", type=int, default=8, choices=(4, 8, 16))
    parser.add_argument("--min-ready", type=float, default=0.99,
                        help="fraction of race tasks that must use the ray-traced path")
    parser.add_argument("--shots-every", type=float, default=0.0,
                        help="seconds between window screenshots; 0 disables them")
    parser.add_argument("--env", action="append", metavar="KEY=VALUE",
                        help="developer variable for every lap, such as LAMBO_RT_SHADOW_DEBUG=2")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "rt-production" / "laps")
    args = parser.parse_args()
    try:
        developer = developer_env(args.env)
    except ValueError as error:
        parser.error(str(error))
    args.output.mkdir(parents=True, exist_ok=True)
    failed = False
    results = {}
    for circuit in args.circuits:
        path = args.output / f"lap-c{circuit}.json"
        path.write_text(json.dumps(scenario(circuit, args.softness, args.rays, developer), indent=2) + "\n",
                        encoding="utf-8")
        stop = threading.Event()
        shooter = None
        if args.shots_every > 0:
            shooter = threading.Thread(target=capture_window,
                                       args=(stop, args.shots_every, args.output / f"shots-c{circuit}"), daemon=True)
            shooter.start()
        started = time.monotonic()
        completed = subprocess.run([sys.executable, str(ROOT / "tools" / "run_game_scenario.py"), str(path),
                                    "--timeout", str(args.timeout)], cwd=ROOT, capture_output=True, text=True)
        stop.set()
        if shooter:
            shooter.join(timeout=10)
        last = (completed.stdout.strip().splitlines() or [""])[-1]
        match = ARTIFACT.search(last)
        if not match:
            print(f"circuit {circuit}: no artifact ({last[:200]})")
            failed = True
            continue
        stderr = (Path(match.group(1)) / "stderr.log").read_text(encoding="utf-8", errors="replace")
        summary = summarize(stderr)
        summary["scenario_passed"] = completed.returncode == 0
        summary["artifact"] = match.group(1)
        summary["seconds"] = round(time.monotonic() - started, 1)
        fraction = summary["ready"] / summary["tasks"] if summary["tasks"] else 0.0
        summary["ready_fraction"] = round(fraction, 4)
        results[circuit] = summary
        ok = completed.returncode == 0 and fraction >= args.min_ready
        failed |= not ok
        top = sorted(summary["reasons"].items(), key=lambda item: -item[1])[:5]
        print(f"circuit {circuit}: {'PASS' if ok else 'FAIL'} ready {summary['ready']}/{summary['tasks']} "
              f"({fraction:.1%}) via {summary['source']}; fallbacks {top}", flush=True)
    (args.output / "laps.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
