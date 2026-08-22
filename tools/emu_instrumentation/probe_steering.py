#!/usr/bin/env python3
"""Capture the live human-steering chain from a deterministic driven race."""

from __future__ import annotations

import argparse
import csv
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
DEFAULT_EXE = REPO / "build" / "lamborghini_modern.exe"
TRACE = re.compile(
    r"steering chain: frame=(\d+) vehicle=(-?\d+) raw=(-?\d+) "
    r"trimmed=(-?\d+) curved=(-?\d+) demand=(-?\d+) speed=(-?\d+) "
    r"selector=(-?\d+) curve=([AB])"
)


@dataclass(frozen=True)
class Sample:
    frame: int
    vehicle: int
    raw: int
    trimmed: int
    curved: int
    demand: int
    speed: int
    selector: int
    curve: str


def expected_trim(raw: int) -> int:
    magnitude = min(max(abs(raw) - 4, 0), 61)
    return -magnitude if raw < 0 else magnitude


def run_probe(executable: Path, scenario: str, car: int, max_vis: int) -> list[Sample]:
    environment = os.environ.copy()
    environment.update({
        "LAMBO_HEADLESS": "1",
        "LAMBO_WARP": f"1:3:{car}:1",
        "LAMBO_STEERING_PROBE": "1",
        "LAMBO_MODERN_INPUT": "8000:0:0",
        "LAMBO_MODERN_MAX_VIS": str(max_vis),
    })
    if scenario == "hairpin":
        environment["LAMBO_STEERING_SEQUENCE"] = "0:0,1400:80,1440:-80,1480:0"
    else:
        environment.pop("LAMBO_STEERING_SEQUENCE", None)

    completed = subprocess.run(
        [str(executable), "--lambo-debug"],
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
    samples = [Sample(*match.groups()[:-1], match.group(9)) for match in TRACE.finditer(completed.stdout)]
    samples = [Sample(
        int(sample.frame), int(sample.vehicle), int(sample.raw), int(sample.trimmed),
        int(sample.curved), int(sample.demand), int(sample.speed), int(sample.selector),
        sample.curve,
    ) for sample in samples]
    if completed.returncode != 0 or not samples:
        print(completed.stdout, file=sys.stderr)
        raise RuntimeError(
            f"steering probe failed: exit={completed.returncode}, samples={len(samples)}"
        )
    return samples


def validate(samples: list[Sample], scenario: str) -> None:
    for previous, sample in zip(samples, samples[1:]):
        if sample.frame != previous.frame + 1:
            raise RuntimeError(f"non-consecutive trace frames: {previous.frame}, {sample.frame}")
    for sample in samples:
        if sample.trimmed != expected_trim(sample.raw):
            raise RuntimeError(
                f"trim mismatch at frame {sample.frame}: raw={sample.raw}, "
                f"trimmed={sample.trimmed}"
            )
        expected_curve = "B" if sample.selector == 0 else "A"
        if sample.curve != expected_curve:
            raise RuntimeError(
                f"selector mismatch at frame {sample.frame}: "
                f"selector={sample.selector}, curve={sample.curve}"
            )
    if scenario == "hairpin":
        transitions = [
            (previous, sample)
            for previous, sample in zip(samples, samples[1:])
            if previous.raw != sample.raw
        ]
        if len(transitions) < 3:
            raise RuntimeError(f"expected three scripted stick transitions, got {len(transitions)}")
        maximum_curve_delta = max(abs(sample.curved - previous.curved)
                                  for previous, sample in transitions)
        response_frames = [sample for sample in samples if sample.raw != 0]
        maximum_demand_step = max(
            abs(sample.demand - previous.demand)
            for previous, sample in zip(response_frames, response_frames[1:])
        )
        if maximum_curve_delta <= 10:
            raise RuntimeError(
                f"scripted full-lock input changed the curve result by only "
                f"{maximum_curve_delta}"
            )
        if maximum_demand_step <= 1:
            print(
                "live correction: curve response is immediate, but final demand "
                f"is rate-limited to {maximum_demand_step} unit/frame"
            )
        else:
            print(f"largest one-frame final-demand change: {maximum_demand_step}")


def write_csv(output: Path, samples: list[Sample]) -> None:
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(Sample.__dataclass_fields__.keys())
        for sample in samples:
            writer.writerow(sample.__dict__.values())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenario", choices=("straight", "hairpin"), default="straight")
    parser.add_argument("--car", type=int, default=0)
    parser.add_argument("--max-vis", type=int, default=None)
    args = parser.parse_args()

    executable = args.exe.resolve()
    if not executable.is_file():
        parser.error(f"executable not found: {executable}")
    if not 0 <= args.car <= 5:
        parser.error("--car must be in the ROM's 0-5 cursor range")
    max_vis = args.max_vis or (1800 if args.scenario == "straight" else 1650)

    samples = run_probe(executable, args.scenario, args.car, max_vis)
    validate(samples, args.scenario)
    write_csv(args.output, samples)
    print(
        f"wrote {len(samples)} frames to {args.output}; "
        f"curve={samples[-1].curve}, max_speed={max(sample.speed for sample in samples)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
