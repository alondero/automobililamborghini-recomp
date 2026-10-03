#!/usr/bin/env python3
"""Audit isolated native cutout comparisons; this never admits game shadows."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import struct

from inspect_rt_task import CaptureError
from check_rt_render_capture import inspect


def validate_alpha_results(render: dict, evidence: dict, raw: bytes) -> dict:
    extent = evidence.get("target_extent")
    if (not isinstance(extent, list) or len(extent) != 2
            or any(type(value) is not int or not 0 < value <= 4096 for value in extent)
            or type(evidence.get("color_address")) is not int):
        raise CaptureError("invalid native alpha framebuffer identity or extent")
    width, height = extent
    if (evidence.get("measured") is not True or evidence.get("error") != ""
            or evidence.get("stride") != 112 or type(evidence.get("faces")) is not int
            or not 0 < evidence["faces"] <= 16384 or len(raw) != evidence["faces"] * 112
            or any(evidence.get(key) is not False for key in ("ray_query", "edge_pixels", "alpha_admitted"))):
        raise CaptureError("incomplete or overstated native alpha evidence")
    calls = {call["call"]: call for call in render["calls"]}
    draws = {draw["draw_index"]: draw for draw in render["raster"]}
    cutouts = {item["draw"] for item in render["sun_shadow_rejected_ranges"] if item["reason"] == 64}
    expected = set()
    for draw in draws.values():
        call = calls[draw["call"]]
        if (call["color_address"] == evidence["color_address"] and draw["call"] in cutouts
                and draw["indexed"] and not draw["test_z"] and not draw["omitted"]):
            expected.update((draw["draw_index"], face) for face in range(draw["first"], draw["first"] + draw["count"], 3))
    observed = set()
    totals = dict(tested_pixels=0, native_covered=0, evaluated_covered=0, disagreements=0,
                  unsupported_faces=0, no_interior_faces=0)
    mismatches = []
    for offset in range(0, len(raw), 112):
        values = struct.unpack_from("<8I20f", raw, offset)
        draw_index, first, tested, native, evaluated, disagreement, unsupported, pixel = values[:8]
        identity = (draw_index, first)
        if (identity not in expected or identity in observed or native > tested or evaluated > tested
                or disagreement > tested or abs(native - evaluated) > disagreement
                or any(not math.isfinite(value) for value in values[8:])):
            raise CaptureError("invalid native alpha face result or ownership")
        observed.add(identity)
        totals["tested_pixels"] += tested
        totals["native_covered"] += native
        totals["evaluated_covered"] += evaluated
        totals["disagreements"] += disagreement
        totals["unsupported_faces"] += unsupported != 0
        totals["no_interior_faces"] += tested == 0
        if disagreement:
            if pixel >= width * height:
                raise CaptureError("native alpha mismatch coordinate is unauthenticated")
            mismatches.append({"draw_index": draw_index, "call": draws[draw_index]["call"],
                               "first": first, "pixels": disagreement,
                               "first_pixel": [pixel % width, pixel // width],
                               "uv_position": list(values[8:12]), "dx_dy": list(values[12:16]),
                               "alpha_depth_shade": list(values[16:20]),
                               "native_uv_shade_alpha": list(values[20:24]),
                               "native_dx_dy": list(values[24:28])})
        elif pixel != 0xFFFFFFFF:
            raise CaptureError("native alpha result has a spurious mismatch coordinate")
    if observed != expected or any(type(evidence.get(key)) is not int or evidence[key] != value
                                   for key, value in totals.items()):
        raise CaptureError("native alpha face coverage or summary does not match GPU results")
    return {"faces": len(observed), **totals, "mismatches": mismatches,
            "interior_parity": totals["tested_pixels"] > 0 and totals["disagreements"] == 0,
            "complete_face_evidence": totals["unsupported_faces"] == 0 and totals["no_interior_faces"] == 0,
            "alpha_admitted": False, "ray_query": False, "edge_pixels": False}


def inspect_alpha(directory: Path, sequence: int) -> dict:
    inspect(directory, sequence)
    render_path = directory / "rt-render" / f"task-{sequence}-render.json"
    render = json.loads(render_path.read_text(encoding="utf-8"))
    evidence = render.get("native_alpha_evidence")
    if not isinstance(evidence, list) or not evidence:
        raise CaptureError("native alpha diagnostic did not run")
    seen = set()
    results = []
    for item in evidence:
        name = item.get("file", "")
        if (not isinstance(name, str) or not re.fullmatch(rf"task-{sequence}-alpha-evidence-(0|[1-9][0-9]*)\.bin", name)
                or name in seen):
            raise CaptureError("invalid native alpha result file identity")
        seen.add(name)
        raw = (render_path.parent / name).read_bytes()
        result = validate_alpha_results(render, item, raw)
        if "native_clip_file" in item:
            suffix = name.removeprefix(f"task-{sequence}-alpha-evidence-").removesuffix(".bin")
            if (item["native_clip_file"] != f"task-{sequence}-native-clip-{suffix}.bin"
                    or type(item.get("native_clip_bytes")) is not int
                    or item["native_clip_bytes"] != item["faces"] * 48):
                raise CaptureError("invalid native alpha clip evidence identity or extent")
            result["native_clip_faces"] = validate_native_clip(
                render, raw, (render_path.parent / item["native_clip_file"]).read_bytes(),
                (render_path.parent / f"task-{sequence}-screen.bin").read_bytes())
        results.append(result)
    shader_records = render.get("native_alpha_vertex_shaders")
    if shader_records is not None:
        if not isinstance(shader_records, list):
            raise CaptureError("invalid native alpha vertex shader evidence")
        calls = {call["call"]: call for call in render["calls"]}
        colors = {item["color_address"] for item in evidence}
        cutouts = {item["draw"] for item in render["sun_shadow_rejected_ranges"] if item["reason"] == 64}
        expected_draws = {draw["draw_index"] for draw in render["raster"]
                          if calls[draw["call"]]["color_address"] in colors and draw["call"] in cutouts
                          and draw["indexed"] and not draw["test_z"] and not draw["omitted"]}
        recorded_draws = set()
        for record in shader_records:
            draw = record.get("draw_index")
            if (type(draw) is not int or draw not in expected_draws or draw in recorded_draws
                    or record.get("file") != f"task-{sequence}-native-vs-{draw}.dxil"
                    or type(record.get("bytes")) is not int or not 0 < record["bytes"] <= 16 * 1024 * 1024):
                raise CaptureError("invalid native alpha vertex shader identity or extent")
            data = (render_path.parent / record["file"]).read_bytes()
            if len(data) != record["bytes"] or data[:4] != b"DXBC":
                raise CaptureError("invalid native alpha vertex shader bytecode")
            recorded_draws.add(draw)
        if recorded_draws != expected_draws:
            raise CaptureError("incomplete native alpha vertex shader evidence")
    return {"sequence": sequence, "framebuffers": results,
            "interior_parity": all(item["interior_parity"] for item in results),
            "alpha_admitted": False}


def validate_native_clip(render: dict, raw_results: bytes, raw_clip: bytes, raw_screen: bytes) -> int:
    if not raw_results or len(raw_results) % 112 or len(raw_clip) != len(raw_results) // 112 * 48:
        raise CaptureError("incomplete native alpha clip vertex evidence")
    executed = 0
    for face in range(len(raw_results) // 112):
        _, first, tested = struct.unpack_from("<3I", raw_results, face * 112)
        clip = struct.unpack_from("<12f", raw_clip, face * 48)
        if any(not math.isfinite(value) for value in clip):
            raise CaptureError("nonfinite native alpha clip vertex evidence")
        # Empty clipped/scissored draws may leave the initialized packet empty.
        # Such a face has no pixel evidence and cannot authorize admission.
        if not any(clip):
            if tested:
                raise CaptureError("native alpha pixels lack executed vertex evidence")
            continue
        executed += 1
        for corner in range(3):
            vertex = render["indices"][first + corner]
            if type(vertex) is not int or vertex < 0 or (vertex + 1) * 16 > len(raw_screen):
                raise CaptureError("native alpha clip vertex identity is out of range")
            w = struct.unpack_from("<f", raw_screen, vertex * 16 + 12)[0]
            if clip[corner * 4 + 3] != w or (tested and w <= 1e-6):
                raise CaptureError("native alpha clip does not match the presented face")
    return executed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--sequence", type=int, action="append")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-interior-parity", action="store_true")
    args = parser.parse_args()
    try:
        result = [inspect_alpha(args.directory, sequence) for sequence in (args.sequence or [60, 300, 420, 540])]
        text = json.dumps(result, indent=2) + "\n"
        if args.output:
            args.output.write_text(text, encoding="utf-8")
        print(text)
        if args.require_interior_parity and any(not item["interior_parity"] for item in result):
            parser.exit(1, "native alpha interior parity failed; admission must remain closed\n")
    except (CaptureError, OSError, ValueError, KeyError, TypeError, IndexError) as error:
        parser.exit(1, f"native alpha capture rejected: {error}\n")


if __name__ == "__main__":
    main()
