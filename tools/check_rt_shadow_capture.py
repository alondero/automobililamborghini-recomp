#!/usr/bin/env python3
"""Compare a ray-traced shadow capture with native controls of the same scene.

Three render-capture runs share the replay and task sequences: the native
baseline (overlay drawn), the native exact-overlay omission (unshadowed
receivers) and the ray-traced run. Shadowing may only darken the unshadowed
image, so any brighter RT pixel fails. Transmission is RT / unshadowed; the
native overlay's own transmission is baseline / unshadowed. For a hard shadow
the darkest RT receiver pixels must transmit what the native core transmits
on screen, after the VI's gamma.
The production log must report a ready replacement for every task. Captures
are local, ignored evidence; this tool never reads guest RAM.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import numpy as np

READY = re.compile(r"\[rt-shadow\] workload=(\d+) task=(\d+) epoch=(\d+) ready=(\d) reason=\"([^\"]*)\"")


def load_swap(render: Path, sequence: int) -> np.ndarray:
    present = json.loads((render / f"task-{sequence}-present.json").read_text(encoding="utf-8"))
    width, height, row = present["width"], present["height"], present["row_bytes"]
    if present.get("format") != "BGRA8" or not present.get("successful_present"):
        raise ValueError(f"task {sequence}: unsupported or failed present")
    raw = np.frombuffer((render / f"task-{sequence}-swap.bgra").read_bytes(), np.uint8)
    if raw.size < row * height:
        raise ValueError(f"task {sequence}: short swap capture")
    return raw[:row * height].reshape(height, row)[:, :width * 4].reshape(height, width, 4)[:, :, 2::-1]


def write_change_map(path: Path, reference: np.ndarray, native: np.ndarray, traced: np.ndarray) -> None:
    """Grey = unchanged, red = RT only, blue = native overlay only, magenta = both."""
    from rt_swap_png import write_png
    out = (reference.astype(np.float64).mean(axis=2, keepdims=True).repeat(3, 2) * 0.5).astype(np.uint8)
    rt = np.any(traced != reference, axis=2)
    nat = np.any(native != reference, axis=2)
    out[nat] = (40, 80, 220)
    out[rt] = (230, 40, 40)
    out[rt & nat] = (230, 40, 230)
    write_png(path, out.shape[1], out.shape[0], out.tobytes())


def production_log(run: Path) -> dict[int, dict]:
    states: dict[int, dict] = {}
    for line in (run / "stderr.log").read_text(encoding="utf-8", errors="replace").splitlines():
        match = READY.search(line)
        if match:
            states[int(match.group(2))] = {"workload": int(match.group(1)), "epoch": int(match.group(3)),
                                           "ready": match.group(4) == "1", "reason": match.group(5)}
    return states


def darkening(reference: np.ndarray, image: np.ndarray) -> dict:
    """Pixels of `image` that differ from the unshadowed `reference`."""
    if reference.shape != image.shape:
        raise ValueError("captures differ in size")
    a = reference.astype(np.float64)
    b = image.astype(np.float64)
    changed = np.any(a != b, axis=2)
    lit = a.min(axis=2) >= 16
    darker = changed & lit & np.all(b <= a, axis=2)
    brighter = changed & np.any(b > a, axis=2)
    transmission = (b[darker] / a[darker]).mean(axis=1) if darker.any() else np.array([])
    ys, xs = np.nonzero(changed)
    return {
        "changed_pixels": int(changed.sum()),
        "darker_pixels": int(darker.sum()),
        "brighter_pixels": int(brighter.sum()),
        "bounds": [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1] if xs.size else None,
        "transmission_percentiles": {str(p): round(float(np.percentile(transmission, p)), 4)
                                     for p in (1, 5, 25, 50)} if transmission.size else None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path, help="native baseline run directory")
    parser.add_argument("unshadowed", type=Path, help="native exact-overlay omission run directory")
    parser.add_argument("traced", type=Path, help="ray-traced shadow run directory")
    parser.add_argument("--sequences", type=int, nargs="+", default=[60, 300, 420, 540])
    parser.add_argument("--core-tolerance", type=float, default=0.03)
    # Native shaders still compile asynchronously at the first sampled task;
    # independent runs can draw a few distant pixels with different pipelines.
    parser.add_argument("--warmup-sequences", type=int, nargs="*", default=[60],
                        help="tasks whose brightening is reported, not failed")
    parser.add_argument("--soft", action="store_true", help="soft shadows: skip the hard core check")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--png-dir", type=Path, help="write change maps and RT images here")
    args = parser.parse_args()
    states = production_log(args.traced)
    report = {"native": str(args.native), "unshadowed": str(args.unshadowed), "traced": str(args.traced),
              "tasks": []}
    failures = []
    for sequence in args.sequences:
        reference = load_swap(args.unshadowed / "rt-render", sequence)
        native_image = load_swap(args.native / "rt-render", sequence)
        traced_image = load_swap(args.traced / "rt-render", sequence)
        native = darkening(reference, native_image)
        traced = darkening(reference, traced_image)
        if args.png_dir:
            from rt_swap_png import write_png
            args.png_dir.mkdir(parents=True, exist_ok=True)
            write_change_map(args.png_dir / f"changes-{sequence}.png", reference, native_image, traced_image)
            write_png(args.png_dir / f"traced-{sequence}.png", traced_image.shape[1], traced_image.shape[0],
                      np.ascontiguousarray(traced_image).tobytes())
        result = {"sequence": sequence, "production": states.get(sequence), "native_overlay": native,
                  "ray_traced": traced}
        native_core = (native["transmission_percentiles"] or {}).get("1")
        traced_core = (traced["transmission_percentiles"] or {}).get("1")
        result["hard_core_difference"] = (None if native_core is None or traced_core is None
                                          else round(traced_core - native_core, 4))
        if not result["production"] or not result["production"]["ready"]:
            failures.append(f"task {sequence}: replacement not ready")
        result["warmup"] = sequence in args.warmup_sequences
        if traced["brighter_pixels"] and not result["warmup"]:
            failures.append(f"task {sequence}: {traced['brighter_pixels']} RT pixels brighter than unshadowed")
        # The native overlay can lighten very dark pixels at its soft edge, so
        # its own brighter pixels are reported, not treated as a mismatch.
        if not args.soft and (result["hard_core_difference"] is None or
                              abs(result["hard_core_difference"]) > args.core_tolerance):
            failures.append(f"task {sequence}: hard shadow core differs from native core")
        report["tasks"].append(result)
        print(json.dumps(result))
    report["failures"] = failures
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for failure in failures:
        print(f"FAIL {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
