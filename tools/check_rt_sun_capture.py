#!/usr/bin/env python3
"""Check a stationary circuit's native light; this is not an RT eligibility gate."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from inspect_rt_task import CaptureError, TaskInspector


def compare_stationary(reports):
    if len(reports) != 4 or {r["task"]["sequence"] for r in reports} != {60, 300, 420, 540}:
        raise CaptureError("expected the four sampled tasks: 60, 300, 420, 540")
    players = reports[0]["task"]["players"]
    circuit = reports[0]["task"]["circuit"]
    # Older local schema-2 captures lack these additive scene fields. They can
    # still establish camera independence, but cannot prove mode/model identity.
    scene = {key: reports[0]["task"].get(key) for key in ("race_mode", "model_cursors")}
    if type(circuit) is not int or not 0 <= circuit < 6:
        raise CaptureError("unknown circuit in stationary light observations")
    if players not in (1, 2):
        raise CaptureError("stationary fixture supports one or two players")
    reference_model = reference_lights = None
    views = {slot: [] for slot in range(1, players + 1)}
    headings = {slot: set() for slot in range(1, players + 1)}
    for report in reports:
        task = report["task"]
        if task["phase"] != 8 or task["circuit"] != circuit or task["players"] != players:
            raise CaptureError("tasks do not share the race phase/circuit/player count")
        if any(task.get(key) != value for key, value in scene.items()):
            raise CaptureError("tasks do not share race mode/model selection")
        for slot in headings:
            camera = next((c for c in task["cameras"] if c["slot"] == slot), None)
            if not camera:
                raise CaptureError("missing camera slot")
            headings[slot].add(camera["heading"])
            # Existing producer interpolation identifies object 1 separately in
            # each view. Confirm the native record's physical-car flag as well.
            car = [d for d in report["draws"] if d["state"]["transform_group"] ==
                   (0x10000000 | slot << 16 | 1) and d.get("native_object", {}).get("flags", 0) & 8]
            lit_car = [draw for draw in car if draw["state"]["vertex_lit"]]
            if not lit_car:
                raise CaptureError("missing lit physical car body")
            if reference_model is None:
                reference_model = lit_car[0]["model"]
            if lit_car[0]["model"] != reference_model:
                raise CaptureError("physical car body moved or rotated during stationary test")
            view = lit_car[0]["view"]
            if len(view) != 4 or any(len(row) != 4 or not all(math.isfinite(v) for v in row) for row in view):
                raise CaptureError("invalid camera view matrix")
            views[slot].append(view)
            for draw in car:
                state = draw["state"]
                if not state["vertex_lit"]:
                    continue
                lights = state["lights"][:state["light_count"]]
                if not lights or any(light is None or sum(v * v for v in light["direction"]) == 0 for light in lights):
                    raise CaptureError("missing/zero directional light")
                directions = [light["direction"] for light in lights]
                if reference_lights is None:
                    reference_lights = directions
                if directions != reference_lights:
                    raise CaptureError("directional lights changed with camera or view")
    if len(headings[1]) < 2 or not any(abs((a - b + 180) % 360 - 180) >= 150
                                    for a in headings[1] for b in headings[1]):
        raise CaptureError("player one did not turn the camera far enough")
    # Check rotation rather than translation: moving an otherwise unchanged
    # camera would not falsify a camera-oriented directional key.
    rotations = [[row[:3] for row in view[:3]] for view in views[1]]
    if all(rotation == rotations[0] for rotation in rotations):
        raise CaptureError("captured camera rotation did not change")
    key = reference_lights[0]
    length = math.sqrt(sum(v * v for v in key))
    return {"task_sequences": sorted(r["task"]["sequence"] for r in reports), "players": players,
            "circuit": circuit,
            **scene,
            "headings": {slot: sorted(values) for slot, values in headings.items()},
            "body_model": reference_model, "native_directional_vectors": reference_lights,
            "unit_key_direction": [v / length for v in key],
            "eligibility": "unproved; native observations only"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="local scenario rt-tasks directory")
    args = parser.parse_args()
    try:
        reports = []
        for sequence in (60, 300, 420, 540):
            path = args.directory / f"task-{sequence}.json"
            reports.append(TaskInspector(path.with_suffix(".bin").read_bytes(),
                                         json.loads(path.read_text(encoding="utf-8"))).inspect())
        print(json.dumps(compare_stationary(reports), indent=2))
        print("PASS: stationary camera/light observations; RT eligibility remains unproved")
    except (CaptureError, OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"capture rejected: {error}\n")


if __name__ == "__main__":
    main()
