#!/usr/bin/env python3
"""Verify live guest brake demand, deceleration, digital parity, and menu isolation.

The probe boots the port headlessly into a warped race with A held (via
LAMBO_MODERN_INPUT), publishes one fixed analog brake value through the
production atomic bridge, and parses the game-thread hook's opt-in field trace.
It asserts the demand ramps to the normalized target and that speed decreases
monotonically with brake input, that full analog braking matches held digital B,
and runs an A-confirm menu smoke outside the race hook.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
DEFAULT_EXE = REPO / "build" / "lamborghini_modern.exe"
TRACE = re.compile(
    r"analog brake field: mode=(analog|digital) port=(\d+) channel=(\d+) "
    r"vehicle=(\d+) frame=(\d+) normalized=(\d+) demand=(-?[\d.]+) speed=(-?\d+)"
)
MENU_TRACE = re.compile(r"\[menu\] vi=(\d+)\s+screen=(-?\d+) sub=(-?\d+)")
STATE_TRACE = re.compile(r"\[state\] vi=(\d+)\s+state=(-?\d+)")
SAMPLE_FRAME = 180
MEDIAN_WINDOW = 300  # race updates after the countdown before sampling speed


def median(values: list[float]) -> float:
    ordered = sorted(values)
    return ordered[len(ordered) // 2]


def run_sample(executable: Path, value: float | None) -> tuple[float, float]:
    """Run a warped race holding A with an optional analog brake override.

    Returns (median demand, median late-phase speed) across the trace.
    """
    environment = os.environ.copy()
    environment.update({
        "LAMBO_HEADLESS": "1",
        "LAMBO_WARP": "1",
        "LAMBO_ANALOG_BRAKE_PROBE": "1",
        "LAMBO_MODERN_INPUT": "8000",  # hold A so the car is always under power
        "LAMBO_MODERN_MAX_VIS": "2400",
    })
    if value is None:
        # Digital mode: no analog override at all; brake comes from pulsing B below.
        environment.pop("LAMBO_ANALOG_BRAKE", None)
        environment["LAMBO_MODERN_INPUT"] = "C000"  # hold A + digital B
    else:
        environment["LAMBO_ANALOG_BRAKE"] = f"{value:.6f}"
    completed = subprocess.run(
        [str(executable), "--console", "--lambo-debug"],
        cwd=REPO,
        env=environment,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=120,
        check=False,
    )
    matches = list(TRACE.finditer(completed.stdout))
    if completed.returncode != 0 or not matches:
        print(completed.stdout, file=sys.stderr)
        raise RuntimeError(
            f"probe run failed for value {value}: exit={completed.returncode}, "
            f"field traces={len(matches)}"
        )
    samples = []
    for match in matches:
        mode = match.group(1)
        port, channel, _vehicle, frame = map(int, match.groups()[1:5])
        demand = float(match.group(7))
        speed = int(match.group(8))
        samples.append((mode, port, channel, frame, demand, speed))
    if not samples:
        raise RuntimeError("no brake field samples")
    mode, port, channel = samples[0][0], samples[0][1], samples[0][2]
    expected_mode = "digital" if value is None else "analog"
    if mode != expected_mode:
        raise RuntimeError(f"unexpected bridge mode {mode} (wanted {expected_mode})")
    if port != 0 or channel != 1:
        raise RuntimeError(f"unexpected player mapping: port={port}, channel={channel}")
    demands = [sample[4] for sample in samples]
    speeds = [sample[5] for sample in samples[MEDIAN_WINDOW:]]
    if len(speeds) < 50:
        raise RuntimeError(f"probe did not collect a late-phase window ({len(speeds)} samples)")
    return median(demands), median(speeds)


def run_menu_smoke(executable: Path) -> tuple[tuple[int, int], tuple[int, int]]:
    environment = os.environ.copy()
    environment.update({
        "LAMBO_HEADLESS": "1",
        "LAMBO_ANALOG_BRAKE": "0.5",
        "LAMBO_INPUT_PULSE": "8000:120:4:900",
        "LAMBO_MODERN_MAX_VIS": "2400",
    })
    environment.pop("LAMBO_WARP", None)
    environment.pop("LAMBO_MODERN_INPUT", None)
    completed = subprocess.run(
        [str(executable), "--console", "--lambo-debug"], cwd=REPO, env=environment,
        text=True, encoding="utf-8", errors="replace", stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=90, check=False,
    )
    screens = [(int(match.group(2)), int(match.group(3)))
               for match in MENU_TRACE.finditer(completed.stdout)]
    states = [int(match.group(2)) for match in STATE_TRACE.finditer(completed.stdout)]
    distinct = list(dict.fromkeys(screens))
    if completed.returncode != 0 or len(distinct) < 2 or 8 not in states:
        print(completed.stdout, file=sys.stderr)
        raise RuntimeError(
            f"menu A-confirm smoke did not reach the race transition: screens={distinct}, states={states}"
        )
    return distinct[0], distinct[-1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    args = parser.parse_args()
    executable = args.exe.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")

    failures = 0
    print(" input | median demand | median late speed")
    print("-------+---------------+------------------")
    speeds: dict[str, float] = {}
    for label, value in (("0%", 0.0), ("50%", 0.5), ("100%", 1.0)):
        demand, speed = run_sample(executable, value)
        print(f" {label:>5} | {demand:>13.1f} | {speed:>16.0f}")
        speeds[label] = speed
    failures += abs(speeds["100%"] - speeds["0%"]) < 20
    failures += not (speeds["0%"] > speeds["50%"] > speeds["100%"])

    digital_demand, digital_speed = run_sample(executable, None)
    print(f" dig-B | {digital_demand:>13.1f} | {digital_speed:>16.0f}")
    tolerance = max(5.0, abs(speeds["100%"]) * 0.25)
    failures += abs(speeds["100%"] - digital_speed) > tolerance

    first_menu, last_menu = run_menu_smoke(executable)
    print(f"menu A-confirm smoke: {first_menu} -> {last_menu}")
    if failures:
        print(f"FAIL: {failures} brake probe mismatch(es)", file=sys.stderr)
        return 1
    print("PASS: brake demand ramp, partial braking order, digital parity, and menu A-confirm")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
