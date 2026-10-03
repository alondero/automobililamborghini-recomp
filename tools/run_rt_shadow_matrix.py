#!/usr/bin/env python3
"""Run the ray-traced shadow validation matrix and compare it with native controls.

For each circuit, race mode and player-one car this runs four windowed D3D12
captures from the same stationary replay: native baseline, native exact-overlay
omission, ray-traced hard shadow (0 degrees) and ray-traced soft shadow.
Generated scenarios and results stay under ignored artifacts/. Requires a USA
ROM build.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASES = {
    "native": "rt-presented-materials.json",
    "omitted": "rt-overlay-differential.json",
    "hard": "rt-shadows-hard.json",
    "soft": "rt-shadows-hard.json",
}
ARTIFACT = re.compile(r"\(artifacts=(.+)\)\s*$")


def variant(kind: str, circuit: int, mode: int, softness: float, rays: int, car: int = 0,
            any_model: bool = False) -> dict:
    scenario = json.loads((ROOT / "scenarios" / BASES[kind]).read_text(encoding="utf-8"))
    scenario["name"] = f"rt-shadow-matrix-c{circuit}-m{mode}-car{car}-{kind}"
    scenario["warp"] = f"{circuit}:1:{car}:1"
    scenario["warp_mode"] = mode
    scenario["input"]["replay"] = str((ROOT / "scenarios" / scenario["input"]["replay"]).resolve())
    if kind == "soft":
        scenario["graphics"] = {"rt_shadows": True, "rt_shadow_rays": rays, "rt_shadow_softness": softness}
    if any_model and kind in ("hard", "soft"):
        scenario["developer_env"] = {"LAMBO_RT_SHADOW_ANY_MODEL": "1"}
    return scenario


def run(path: Path, timeout: int) -> tuple[bool, str | None, str]:
    completed = subprocess.run([sys.executable, str(ROOT / "tools" / "run_game_scenario.py"), str(path),
                                "--timeout", str(timeout)], cwd=ROOT, capture_output=True, text=True)
    last = (completed.stdout.strip().splitlines() or [""])[-1]
    match = ARTIFACT.search(last)
    return completed.returncode == 0, match.group(1) if match else None, last[:300]


def check(runs: dict, kind: str, report: Path, png_dir: Path) -> bool:
    command = [sys.executable, str(ROOT / "tools" / "check_rt_shadow_capture.py"),
               runs["native"]["artifact"], runs["omitted"]["artifact"], runs[kind]["artifact"],
               "--output", str(report), "--png-dir", str(png_dir)]
    if kind == "soft":
        command.append("--soft")
    return subprocess.run(command, cwd=ROOT, capture_output=True, text=True).returncode == 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--circuits", type=int, nargs="+", default=[1, 2, 3, 4, 5, 6])
    parser.add_argument("--modes", type=int, nargs="+", default=[0, 2])
    parser.add_argument("--cars", type=int, nargs="+", default=[0], help="player-one model selectors")
    parser.add_argument("--kinds", nargs="+", default=list(BASES), choices=list(BASES))
    parser.add_argument("--softness", type=float, default=0.5)
    parser.add_argument("--rays", type=int, default=8, choices=(4, 8, 16))
    parser.add_argument("--any-model", action="store_true",
                        help="developer sweep: let unvalidated models reach the production path")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "rt-production" / "matrix.json")
    args = parser.parse_args()
    scenario_dir = args.output.parent / "matrix-scenarios"
    scenario_dir.mkdir(parents=True, exist_ok=True)
    summary = []
    failed = False
    for circuit in args.circuits:
        for mode in args.modes:
            for car in args.cars:
                label = f"c{circuit}-m{mode}-car{car}"
                runs = {}
                for kind in args.kinds:
                    path = scenario_dir / f"{label}-{kind}.json"
                    document = variant(kind, circuit, mode, args.softness, args.rays, car, args.any_model)
                    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
                    passed, artifact, line = run(path, args.timeout)
                    runs[kind] = {"passed": passed, "artifact": artifact, "result": line}
                    failed |= not passed
                    print(f"{label} {kind}: {line}", flush=True)
                checks = {}
                if all(runs.get(k, {}).get("artifact") for k in ("native", "omitted")):
                    for kind in ("hard", "soft"):
                        if not runs.get(kind, {}).get("artifact"):
                            continue
                        report = args.output.parent / f"matrix-{label}-{kind}.json"
                        passed = check(runs, kind, report, args.output.parent / "png" / f"{label}-{kind}")
                        checks[kind] = {"passed": passed, "report": str(report)}
                        failed |= not passed
                        print(f"{label} {kind} check: {'PASS' if passed else 'FAIL'}", flush=True)
                summary.append({"circuit": circuit, "mode": mode, "car": car, "runs": runs, "checks": checks})
                args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
