#!/usr/bin/env python3
"""Compare real D3D12 frames with only the authenticated overlay omitted."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct

from check_rt_render_capture import area, inspect, receiver_material
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


def owner_image(run: Path, render: dict, present: dict, require_complete: bool = True) -> tuple[dict, bytes]:
    matches = [item for item in render.get("owner_buffers", [])
               if item.get("color_address") == present["color_address"]]
    if len(matches) != 1:
        raise CaptureError(f"presented color address has {len(matches)} owner targets")
    owner = matches[0]
    if require_complete and owner.get("complete") is not True:
        raise CaptureError("presented owner target is incomplete (unsupported draw or scene path)")
    width, height = owner.get("width"), owner.get("height")
    if (owner.get("layout") != "draw_index_plus_one,primitive_id"
            or type(width) is not int or type(height) is not int
            or [width, height] != present["texture_extent"]
            or owner.get("row_bytes") != width * 8):
        raise CaptureError("owner target extent/layout differs from the displayed VI source")
    raw = (run / "rt-render" / owner["file"]).read_bytes()
    if len(raw) != width * height * 8:
        raise CaptureError("owner target byte extent is invalid")
    return owner, raw


def owner_at_tap(tx: float, ty: float, owner: dict, raw: bytes) -> tuple[int, int]:
    width, height = owner["width"], owner["height"]
    # VI tap positions are already source-texture texel coordinates. The VI
    # shader divides them by texture resolution; allocated padding does not
    # stretch those coordinates across the owner target.
    x = min(max(math.floor(tx), 0), width - 1)
    y = min(max(math.floor(ty), 0), height - 1)
    draw_id, primitive_id = struct.unpack_from("<II", raw, (y * width + x) * 8)
    if draw_id == 0:
        raise CaptureError(f"unowned VI tap at ({x},{y})")
    return draw_id - 1, primitive_id


def verify_owner_tap(tx: float, ty: float, owner: dict, raw: bytes,
                     render: dict, receiver_faces: set, calls: dict, draws: dict) -> tuple[int, int]:
    draw_index, primitive_id = owner_at_tap(tx, ty, owner, raw)
    draw = draws.get(draw_index)
    if draw is None or not draw["indexed"] or draw["test_z"] or draw["overlay"]:
        raise CaptureError(f"owner {draw_index} at tap ({tx:.2f},{ty:.2f}) is unknown, raw, test-Z or overlay")
    if primitive_id >= draw["count"] // 3:
        raise CaptureError(f"owner primitive {primitive_id} is outside draw {draw_index}")
    first = draw["first"] + primitive_id * 3
    call = calls.get(draw["call"])
    if (call is None or call["color_address"] != owner["color_address"]
            or not receiver_material(call) or (draw["call"], first) not in receiver_faces):
        raise CaptureError(f"owner draw {draw_index} primitive {primitive_id} lacks admitted fog-safe receiver evidence")
    if first > len(render["indices"]) - 3:
        raise CaptureError(f"owner draw {draw_index} primitive {primitive_id} exceeds presented indices")
    return draw["call"], first


def compare(baseline: Path, repeat: Path, omitted: Path, sequence: int,
            inspect_incomplete: bool = False) -> dict:
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
        raise CaptureError("owner-buffer diagnostic changed native swapchain pixels")
    if proof["render_info"].get("owner_buffers"):
        raise CaptureError("baseline unexpectedly enabled the owner diagnostic")
    control_owner, control_owner_bytes = owner_image(repeat, control["render_info"], control["present_info"])
    owner, owner_bytes = owner_image(omitted, changed["render_info"], changed["present_info"],
                                    require_complete=not inspect_incomplete)
    receiver_faces = {(face["call"], face["first"]) for face in changed["receiver_ranges"]}
    calls = {call["call"]: call for call in changed["render_info"]["calls"]}
    draws = {}
    for draw in changed["render_info"]["raster"]:
        index = draw.get("draw_index")
        if type(index) is not int or index in draws:
            raise CaptureError("duplicate or missing owner draw identity")
        draws[index] = draw
    pixels = outside = outside_projected_overlay = unattributed_overlay = 0
    taps_checked = 0
    visible_receivers = set()
    min_x, min_y, max_x, max_y = info["width"], info["height"], -1, -1
    for y in range(info["height"]):
        for x in range(info["width"]):
            offset = y * info["row_bytes"] + x * 4
            if images[0][offset:offset+3] == images[2][offset:offset+3]:
                continue
            pixels += 1
            min_x, min_y, max_x, max_y = min(min_x, x), min(min_y, y), max(max_x, x), max(max_y, y)
            taps = filter_taps(x, y, info)
            native_overlay_tap = False
            for tx, ty in taps:
                native_draw_index, _ = owner_at_tap(tx, ty, control_owner, control_owner_bytes)
                native_draw = draws.get(native_draw_index)
                if native_draw is None:
                    raise CaptureError(f"native owner {native_draw_index} has no authenticated draw range")
                native_overlay_tap = native_overlay_tap or native_draw["overlay"]
                visible_receivers.add(verify_owner_tap(tx, ty, owner, owner_bytes,
                    changed["render_info"], receiver_faces, calls, draws))
                taps_checked += 1
            unattributed_overlay += not native_overlay_tap
            # Geometry projections are reported to explain edge cases only.
            # Pixel ownership from the real raster pass is the attribution gate.
            contributing = [(tx, ty) for tx, ty in taps if any(inside(tx, ty, face) for face in proof["overlay_source"])]
            outside += any(not any(inside(tx, ty, face) for face in proof["road_source"]) for tx, ty in contributing)
            outside_projected_overlay += not contributing
    if pixels == 0 or unattributed_overlay:
        raise CaptureError(f"empty/unexplained overlay differential: pixels={pixels}, owner-unattributed={unattributed_overlay}")
    return {"sequence": sequence, "native_repeat_identical": True, "overlay_changed_pixels": pixels,
            "bounds": [min_x, min_y, max_x, max_y], "outside_opaque_road_projection": outside,
            "outside_projected_overlay": outside_projected_overlay,
            "owner_unattributed_overlay_pixels": unattributed_overlay,
            "owner_taps_checked": taps_checked, "unique_admitted_receiver_faces": len(visible_receivers),
            "owner_target_complete": owner["complete"],
            "visible_receiver_gate_passed": owner["complete"] and unattributed_overlay == 0,
            "gate_passed": owner["complete"] and unattributed_overlay == 0,
            "overlay_projection_uncovered_area": proof["overlay_projection_uncovered_area"],
            "owner_incomplete_reasons": changed["render_info"].get("owner_incomplete_reasons", []),
            "replacement_coverage": ("unproved: incomplete full-view ownership; shadow AS, receiver path and failure restoration remain unimplemented"
                if not owner["complete"] else
                "unproved: visible receiver ownership is measured for this D3D12 view, but shadow AS, receiver path and failure restoration remain unimplemented")}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("repeat", type=Path)
    parser.add_argument("omitted", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--inspect-incomplete", action="store_true",
                        help="scan an incomplete owner map for diagnostic failures; this never passes the suppression gate")
    args = parser.parse_args()
    try:
        result = [compare(args.baseline, args.repeat, args.omitted, n, args.inspect_incomplete)
                  for n in (60, 300, 420, 540)]
        text = json.dumps(result, indent=2) + "\n"
        if args.output:
            args.output.write_text(text, encoding="utf-8")
        print(text)
    except (CaptureError, OSError, ValueError, KeyError, TypeError, IndexError) as error:
        parser.exit(1, f"overlay capture rejected: {error}\n")


if __name__ == "__main__":
    main()
