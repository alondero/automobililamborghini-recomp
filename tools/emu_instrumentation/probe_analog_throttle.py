#!/usr/bin/env python3
"""Verify live guest throttle, acceleration, digital parity, and menu isolation.

The probe boots the port headlessly, uses the first-party race warp, publishes
one fixed analog value through the production atomic bridge, and parses the
game-thread hook's opt-in field trace. It compares vehicle speed at a fixed
race-update frame and runs an A-confirm smoke outside the race hook.
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
    r"analog throttle field: mode=(analog|digital) port=(\d+) channel=(\d+) "
    r"vehicle=(\d+) frame=(\d+) normalized=(\d+) limit=(-?\d+) "
    r"demand=(-?\d+) speed=(-?\d+)"
)
MENU_TRACE = re.compile(r"\[menu\] vi=(\d+)\s+screen=(-?\d+) sub=(-?\d+)")
STATE_TRACE = re.compile(r"\[state\] vi=(\d+)\s+state=(-?\d+)")
SAMPLE_FRAME = 180


def run_sample(executable: Path, value: float | None, digital_a: bool = False) -> tuple[int, int, int]:
    environment = os.environ.copy()
    environment.update({
        "LAMBO_HEADLESS": "1",
        "LAMBO_WARP": "1",
        "LAMBO_ANALOG_THROTTLE_PROBE": "1",
        "LAMBO_MODERN_MAX_VIS": "1800",
    })
    if value is None:
        environment.pop("LAMBO_ANALOG_THROTTLE", None)
    else:
        environment["LAMBO_ANALOG_THROTTLE"] = f"{value:.6f}"
    if digital_a:
        environment["LAMBO_MODERN_INPUT"] = "8000"
    else:
        environment.pop("LAMBO_MODERN_INPUT", None)
    completed = subprocess.run(
        [str(executable), "--console", "--lambo-debug"],
        cwd=REPO,
        env=environment,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=90,
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
        numbers = tuple(map(int, match.groups()[1:]))
        port, channel, _vehicle, frame, _normalized, limit, demand, speed = numbers
        samples.append((mode, port, channel, frame, limit, demand, speed))
    eligible = [sample for sample in samples if sample[3] <= SAMPLE_FRAME]
    if not eligible:
        raise RuntimeError(f"probe did not reach race frame {SAMPLE_FRAME}")
    mode, port, channel, frame, limit, demand, speed = max(eligible, key=lambda sample: sample[3])
    expected_mode = "digital" if value is None else "analog"
    if mode != expected_mode or frame != SAMPLE_FRAME:
        raise RuntimeError(f"unexpected sample: mode={mode}, frame={frame}")
    if port != 0 or channel != 1:
        raise RuntimeError(f"unexpected player mapping: port={port}, channel={channel}")
    return limit, demand, speed


def run_menu_smoke(executable: Path) -> tuple[tuple[int, int], tuple[int, int]]:
    environment = os.environ.copy()
    environment.update({
        "LAMBO_HEADLESS": "1",
        "LAMBO_ANALOG_THROTTLE": "0.5",
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
    print(f"fixed race-update frame: {SAMPLE_FRAME}")
    print(" input | limit | demand | expected | speed")
    print("-------+-------+--------+----------+------")
    speeds: dict[float, int] = {}
    for value in (0.0, 0.25, 0.5, 0.75, 1.0):
        limit, demand, speed = run_sample(executable, value)
        expected = round(value * max(0, limit))
        print(f" {value:>5.0%} | {limit:>5} | {demand:>6} | {expected:>8} | {speed:>5}")
        failures += demand != expected
        speeds[value] = speed

    limit, demand, fallback_speed = run_sample(executable, 0.0, digital_a=True)
    print(f" A max | {limit:>5} | {demand:>6} | {max(0, limit):>8} | {fallback_speed:>5}")
    failures += demand != max(0, limit)
    limit, digital_demand, digital_speed = run_sample(executable, None, digital_a=True)
    print(f"Digital| {limit:>5} | {digital_demand:>6} | {max(0, limit):>8} | {digital_speed:>5}")
    failures += digital_demand != max(0, limit)
    failures += not (speeds[0.5] < speeds[1.0])
    tolerance = max(2, abs(digital_speed) // 50)
    failures += abs(speeds[1.0] - digital_speed) > tolerance
    first_menu, last_menu = run_menu_smoke(executable)
    print(f"menu A-confirm smoke: {first_menu} -> {last_menu}")
    if failures:
        print(f"FAIL: {failures} throttle field mismatch(es)", file=sys.stderr)
        return 1
    print("PASS: throttle demand, partial acceleration, digital parity, and menu A-confirm")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
