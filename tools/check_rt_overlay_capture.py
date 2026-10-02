#!/usr/bin/env python3
"""Compare real D3D12 frames with only the authenticated overlay omitted."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from check_rt_render_capture import area, inspect
from inspect_rt_task import CaptureError


def inside(x: float, y: float, poly: list) -> bool:
    if len(poly) < 3 or area(poly) <= 1e-8:
        return False
    products = [(b[0]-a[0])*(y-a[1]) - (b[1]-a[1])*(x-a[0]) for a, b in zip(poly, poly[1:]+poly[:1])]
    return all(p >= -1e-6 for p in products) or all(p <= 1e-6 for p in products)


def filter_taps(x: int, y: int, info: dict) -> list:
    """Pinned VideoInterfacePS UV warp/clamp and nonzero sampler taps."""
    vx, vy, vw, vh = info["vi_viewport"]
    rw, rh = info["video_resolution"]
    px, py = (x+.5-vx)*rw/vw, (y+.5-vy)*rh/vh
    filtering = info["filtering"]
    if filtering == 2:
        # PixelAntialiasing uses fwidth of affine video-space UVs.
        def warp(p: float, derivative: float) -> float:
            seam = math.floor(p + .5)
            return min(max((p-seam)/derivative + seam, seam-.5), seam+.5)
        px, py = warp(px, rw/vw), warp(py, rh/vh)
    elif filtering not in (0, 1):
        raise CaptureError("unsupported VI filter for pixel-coverage comparison")
    px, py = min(max(px, .5), rw-.5), min(max(py, .5), rh-.5)
    if filtering == 0:
        return [(math.floor(px)+.5, math.floor(py)+.5)]
    cx, cy = math.floor(px-.5)+.5, math.floor(py-.5)+.5
    fx, fy = px-cx, py-cy
    return [(cx+dx, cy+dy) for dx in (0, 1) for dy in (0, 1)
            if (fx if dx else 1-fx) * (fy if dy else 1-fy) > 1e-9]


def compare(baseline: Path, repeat: Path, omitted: Path, sequence: int) -> dict:
    proof = inspect(baseline, sequence, include_geometry=True)
    control = inspect(repeat, sequence, include_geometry=True)
    changed = inspect(omitted, sequence, include_geometry=True)
    info = proof["present_info"]
    for other in (control, changed):
        if any(other["present_info"][key] != info[key] for key in
               ("sequence", "frames", "frame", "width", "height", "row_bytes", "format", "color_address",
                "filtering", "vi_viewport", "vi_scissor", "video_resolution", "texture_extent")):
            raise CaptureError("presentation input/settings differ")
        for key in ("indices", "world_indices", "world_groups", "calls", "local_positions"):
            if other["render_info"][key] != proof["render_info"][key]:
                raise CaptureError(f"native/presented draw inputs differ: {key}")
    for suffix in ("-world.bin", "-screen.bin", "-shade.bin"):
        blobs = [(d/"rt-render"/f"task-{sequence}{suffix}").read_bytes() for d in (baseline, repeat, omitted)]
        if blobs[0] != blobs[1] or blobs[0] != blobs[2]:
            raise CaptureError("GPU geometry/projection changed across differential runs")
    if proof["render_info"]["drop_overlay"] or control["render_info"]["drop_overlay"] or not changed["render_info"]["drop_overlay"]:
        raise CaptureError("overlay experiment modes are incorrect")
    expected = [dict(draw, omitted=draw["overlay"]) for draw in proof["render_info"]["raster"]]
    if changed["render_info"]["raster"] != expected or control["render_info"]["raster"] != proof["render_info"]["raster"]:
        raise CaptureError("a draw other than the exact overlay changed")
    images = [(d/"rt-render"/f"task-{sequence}-swap.bgra").read_bytes() for d in (baseline, repeat, omitted)]
    if images[0] != images[1]:
        raise CaptureError("native repeat is not pixel-identical")
    pixels = outside = outside_overlay = 0
    min_x, min_y, max_x, max_y = info["width"], info["height"], -1, -1
    for y in range(info["height"]):
        for x in range(info["width"]):
            offset = y * info["row_bytes"] + x * 4
            if images[0][offset:offset+3] == images[2][offset:offset+3]:
                continue
            pixels += 1
            min_x, min_y, max_x, max_y = min(min_x, x), min(min_y, y), max(max_x, x), max(max_y, y)
            taps = filter_taps(x, y, info)
            contributing = [(tx, ty) for tx, ty in taps if any(inside(tx, ty, face) for face in proof["overlay_source"])]
            outside += any(not any(inside(tx, ty, face) for face in proof["road_source"]) for tx, ty in contributing)
            outside_overlay += not contributing
    if pixels == 0 or outside_overlay:
        raise CaptureError(f"empty/unexplained overlay differential: pixels={pixels}, outside_overlay={outside_overlay}")
    return {"sequence": sequence, "native_repeat_identical": True, "overlay_changed_pixels": pixels,
            "bounds": [min_x, min_y, max_x, max_y], "outside_opaque_road_projection": outside,
            "overlay_projection_uncovered_area": proof["overlay_projection_uncovered_area"],
            "replacement_coverage": "unproved; projected road coverage does not establish RT replacement pixels"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("repeat", type=Path)
    parser.add_argument("omitted", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = [compare(args.baseline, args.repeat, args.omitted, n) for n in (60, 300, 420, 540)]
        text = json.dumps(result, indent=2) + "\n"
        if args.output:
            args.output.write_text(text, encoding="utf-8")
        print(text)
    except (CaptureError, OSError, ValueError, KeyError, TypeError, IndexError) as error:
        parser.exit(1, f"overlay capture rejected: {error}\n")


if __name__ == "__main__":
    main()
