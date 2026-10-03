#!/usr/bin/env python3
"""Authenticate native triangles against fenced RT64 output; keep dumps local."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import struct

from inspect_rt_task import CaptureError, TaskInspector


def opaque_coverage(material: dict) -> bool:
    """Texture-independent interior coverage in pinned RasterPS, not solidity.

    No alpha compare, coverage modulation or framebuffer alpha blend means
    sampled alpha cannot punch a hole. Depth and primitive-source restrictions
    deliberately exclude decals, interpenetrating and translucent passes.
    """
    return (material["alpha_compare"] == 0 and not material["coverage_times_alpha"]
            and not material["alpha_blend"] and not material["force_blend"]
            and material["z_compare"] and material["z_update"]
            and material["z_mode"] == 0 and material["z_source"] == 0)


def face_key(group: int, combine: int, geometry: int, points: list) -> tuple:
    return group, combine, geometry, tuple(sorted(tuple(p) for p in points))


def belongs_to_physical_car(obj: dict, objects: list) -> bool:
    """Resolve captured child parts through their bounded native parent chain."""
    index = obj.get("index")
    visited = set()
    while type(index) is int and 0 <= index < len(objects) and index not in visited:
        visited.add(index)
        current = objects[index]
        if current["flags"] & 8:
            return True
        parent = current.get("parent")
        if type(parent) is not int or parent < 0:
            return False
        index = parent
    return False


def procedural_world_object(obj: dict) -> bool:
    """Measured native tiled-world role; coverage still needs material proof."""
    return (obj.get("flags") == 0xC01 and obj.get("list") == 0
            and obj.get("parent") == -1 and obj.get("kind") == 13)


def topology(faces: list) -> dict:
    """Diagnostic edge incidence, never a convexity/solidity admission rule.

    Ray queries admit authenticated opaque physical triangles. A failed weld
    can mean a seam/T-junction/detail overlap; it does not prove a real hole.
    """
    edges = Counter()
    direction = Counter()
    degenerate = 0
    vertices_all = set()
    for face in faces:
        vertices = [tuple(round(x, 4) for x in p) for p in face]
        if len(set(vertices)) != 3:
            degenerate += 1
            continue
        vertices_all.update(vertices)
        for a, b in zip(vertices, vertices[1:] + vertices[:1]):
            edge = tuple(sorted((a, b)))
            edges[edge] += 1
            direction[edge] += 1 if (a, b) == edge else -1
    boundary = sum(n == 1 for n in edges.values())
    nonmanifold = sum(n > 2 for n in edges.values())
    consistent = bool(edges) and all(n == 2 for n in edges.values()) and all(n == 0 for n in direction.values())
    return {"faces": len(faces), "boundary_edges": boundary,
            "nonmanifold_edges": nonmanifold, "degenerate_faces": degenerate,
            "consistent_orientation": consistent, "weld_world_units": 1e-4}


# Phase 1 attenuates native output without relighting, so vertex-lit surfaces
# would double-darken their light-averted faces. They cast but never receive.
F3DEX_LIGHTING = 0x00020000


def presented_object(render: dict, call: dict) -> int | None:
    """Native object of an indexed draw's first presented vertex, if any."""
    try:
        group = render["world_groups"][render["world_indices"][render["indices"][call["first"]]]]
    except (IndexError, KeyError, TypeError):
        return None
    return group & 0xFFFF if type(group) is int and group & 0xFFF00000 == 0x10000000 else None


def world_cutout_reason(call: dict, render: dict) -> int:
    """Measured world-builder cutouts are explicit non-casters (reason 4)."""
    m = call["material"]
    measured = (call.get("indexed") is True and call.get("projection_type") in (1, 2)
                and call.get("extended_type", 0) == 0 and m["other_lo"] == 0xCB023038
                and call["shader_other_lo"] == m["other_lo"]
                and call["shader_other_hi"] & ~63 == m["other_hi"] & ~63
                and m["coverage_times_alpha"] is True and m["alpha_compare"] == 0
                and not m["alpha_blend"] and not m["force_blend"] and m["z_compare"] and m["z_update"]
                and m["z_mode"] == 0 and m["z_source"] == 0)
    objects = render.get("native", {}).get("objects", [])
    index = presented_object(render, call)
    if not measured or index is None or index >= len(objects):
        return 0
    obj = objects[index]
    world = (index == 0 and obj.get("flags") == 0x601 and obj.get("list", 0) != 0) or procedural_world_object(obj)
    return 4 if world else 0


def artistic_car_glass(call: dict) -> bool:
    """Maintainer policy: the measured blended car glass casts as opaque glass."""
    m = call["material"]
    return (m["other_lo"] == 0x504A50 and (m["combine_w0"], m["combine_w1"]) == (0xFC121824, 0xFF33FFFF)
            and call["shader_other_lo"] == m["other_lo"]
            and call["shader_other_hi"] & ~63 == m["other_hi"] & ~63
            and call.get("extended_type", 0) == 0 and m["alpha_compare"] == 0 and m["alpha_blend"]
            and m["z_compare"] and not m["z_update"] and m["z_mode"] == 0x800 and m["z_source"] == 0)


def receiver_material(call: dict) -> bool:
    """Bounded measured two-cycle standard-fog paths, with finite native fog."""
    m = call["material"]
    cc = (m["combine_w0"], m["combine_w1"])
    return (opaque_coverage(m) and m["standard_fog"] is True
            and m["other_lo"] in (0xC8112078, 0xC8112230)
            and (m["other_hi"] >> 20) & 3 == 1
            and cc in {(0xFC127FFF, 0xFFFFF238), (0xFC26A004, 0x1FFC93F8),
                       (0xFC327FFF, 0xFFFFF838), (0xFCFFFFFF, 0xFFFE7838)}
            and call["shader_other_lo"] == m["other_lo"]
            and call["shader_other_hi"] == m["other_hi"]
            and not call["shader_flags"] & ((1 << 29) | 1)
            and not m.get("geometry", 0) & F3DEX_LIGHTING
            and len(m["fog_rgba"]) == 4
            and all(math.isfinite(v) and 0 <= v <= 1 for v in m["fog_rgba"]))


def non_caster_reason(call: dict) -> int:
    """Authenticate screen/backdrop exclusions using HLE-copied projection identity."""
    m = call["material"]
    if (m["z_compare"] or m["z_update"] or call.get("extended_type", 0) != 0
            or call["shader_other_lo"] != m["other_lo"]
            or call["shader_other_hi"] & ~63 != m["other_hi"] & ~63):
        return 0
    if call["projection_type"] == 3 and call["shader_flags"] & 1:
        return 1
    if call["projection_type"] == 1 and call.get("projection_aspect") == 3:
        return 2
    if (call["projection_type"] == 2 and call.get("projection_address") == 0xA2C40
            and m["geometry"] & ~0x800000 == 0):
        return 3
    return 0


def validate_overlay_count(overlay_faces: int, require_overlay: bool = True) -> None:
    # Time trial has one authenticated player shadow. Single race can submit
    # the same 16-triangle native child for several visible car parents.
    if require_overlay and (overlay_faces == 0 or overlay_faces % 16 != 0):
        raise CaptureError(f"native shadow children did not form complete 16-triangle groups: {overlay_faces} triangles")


def validate_shadow_admission(render: dict) -> None:
    """Keep incomplete native metadata from being mistaken for suppressible work."""
    shadow = render.get("sun_shadow")
    if shadow is None:
        return
    if not isinstance(shadow, dict):
        raise CaptureError("invalid shadow workload metadata")
    rejected = render.get("sun_shadow_rejected_ranges")
    unclassified = render.get("sun_shadow_unclassified_ranges")
    if not isinstance(rejected, list) or not isinstance(unclassified, list):
        raise CaptureError("shadow workload omitted rejected or unclassified ranges")
    for field, ranges in (("rejected", rejected), ("unclassified", unclassified)):
        if type(shadow.get(field)) is not int or shadow[field] != len(ranges):
            raise CaptureError(f"shadow workload {field} count does not match its ranges")
    for item in rejected:
        if (not isinstance(item, dict) or type(item.get("first")) is not int
                or type(item.get("count")) is not int or item["first"] < 0 or item["count"] < 0
                or type(item.get("projection_type")) is not int or item["projection_type"] not in (1, 2)):
            raise CaptureError("invalid indexed shadow rejected range")
    for item in unclassified:
        if not isinstance(item, dict) or type(item.get("reason")) is not int or not 1 <= item["reason"] <= 6:
            raise CaptureError("unknown shadow unclassified reason")
        if (type(item.get("first")) is not int or type(item.get("count")) is not int
                or item["first"] < 0 or item["count"] < 0
                or type(item.get("projection_type")) is not int
                or item["projection_type"] not in (0, 1, 2, 3, 4)):
            raise CaptureError("invalid shadow unclassified metadata")
        if item["reason"] == 1:
            if item["projection_type"] not in (0, 3, 4) or item["first"] != 0 or item["count"] != 0:
                raise CaptureError("non-indexed projection invented an indexed face range")
        elif item["projection_type"] not in (1, 2):
            raise CaptureError("indexed shadow rejection has a non-indexed projection type")
        if item["reason"] == 5 and item["count"] != 0:
            raise CaptureError("overflowed shadow face count is not represented as zero")
    if shadow.get("complete") is True:
        if (shadow.get("authenticated") is not True or shadow.get("params_abi_valid") is not True
                or shadow.get("geometry", 0) <= 0 or shadow.get("overlays", 0) <= 0
                or rejected or unclassified):
            raise CaptureError("shadow workload claims completeness with missing or rejected admission")
    elif shadow.get("complete") is not False:
        raise CaptureError("shadow workload completeness must be boolean")
    if "policy_schema" in shadow and (type(shadow["policy_schema"]) is not int or shadow["policy_schema"] != 2):
        raise CaptureError("unknown shadow material policy schema")
    if shadow.get("policy_schema") == 2:
        calls = {call["call"]: call for call in render["calls"]}
        seen_noncasters = set()
        for count_key, records_key in (("non_casters", "sun_shadow_non_caster_ranges"),
                                       ("receivers", "sun_shadow_receiver_ranges"),
                                       ("receiver_rejected", "sun_shadow_receiver_rejected_ranges")):
            records = render.get(records_key)
            if (not isinstance(records, list) or type(shadow.get(count_key)) is not int
                    or shadow[count_key] != len(records)):
                raise CaptureError("shadow workload omitted or miscounted a material policy list")
            for item in records:
                call = calls.get(item.get("draw"))
                if (call is None or any(item.get(key) != call[key] for key in ("first", "count", "projection_type"))):
                    raise CaptureError("shadow policy range differs from its presented draw")
                if count_key == "non_casters":
                    expected = 4 if item.get("reason") == 4 else non_caster_reason(call)
                    if expected == 4:
                        expected = world_cutout_reason(call, render)
                    if (type(item.get("reason")) is not int or item["reason"] == 0
                            or expected != item["reason"] or item["draw"] in seen_noncasters):
                        raise CaptureError("shadow draw excluded without supported screen/backdrop/cutout identity")
                    seen_noncasters.add(item["draw"])
                elif count_key == "receivers":
                    if item.get("reason") != 0 or not call["indexed"] or not receiver_material(call):
                        raise CaptureError("shadow receiver lacks supported native material proof")
                elif type(item.get("reason")) is not int or item["reason"] <= 0:
                    raise CaptureError("native-only receiver omitted its rejection reason")
        if shadow["complete"] and shadow["receivers"] == 0:
            raise CaptureError("complete shadow workload has no admitted receiver")


def validate_draw_metadata(calls: list) -> None:
    """Keep index-buffer ranges distinct from raw/non-indexed projection calls."""
    if not isinstance(calls, list):
        raise CaptureError("invalid workload call metadata")
    for call in calls:
        if not isinstance(call, dict) or type(call.get("call")) is not int:
            raise CaptureError("invalid workload draw identity")
        if (type(call.get("indexed")) is not bool or type(call.get("first")) is not int
                or type(call.get("count")) is not int or type(call.get("triangle_count")) is not int
                or type(call.get("raw_vertex_count")) is not int
                or type(call.get("projection_type")) is not int):
            raise CaptureError("incomplete workload draw metadata")
        if (call["first"] < 0 or call["count"] < 0 or call["triangle_count"] < 0
                or call["raw_vertex_count"] < 0):
            raise CaptureError("negative workload draw range")
        if call["indexed"]:
            if call["projection_type"] not in (1, 2) or call["count"] % 3 != 0:
                raise CaptureError("invalid indexed projection range")
            if call["count"] != call["triangle_count"] * 3:
                raise CaptureError("indexed range disagrees with triangle count")
            if call.get("raw_vertex_start") is not None or call["raw_vertex_count"] != 0:
                raise CaptureError("indexed draw unexpectedly carries a raw vertex range")
        else:
            if (call["projection_type"] not in (0, 3, 4) or call["first"] != 0 or call["count"] != 0):
                raise CaptureError("non-indexed projection invented an indexed face range")
            raw_start = call.get("raw_vertex_start")
            if call["projection_type"] == 4:
                if (type(raw_start) is not int or raw_start < 0
                        or call["raw_vertex_count"] != call["triangle_count"] * 3):
                    raise CaptureError("raw triangle projection omitted or corrupted its vertex range")
            elif raw_start is not None or call["raw_vertex_count"] != 0:
                raise CaptureError("non-triangle projection carries a raw vertex range")


def area(poly: list) -> float:
    return abs(sum(a[0]*b[1] - b[0]*a[1] for a, b in zip(poly, poly[1:] + poly[:1]))) / 2


def clip_scissor(poly: list, rect: list) -> list:
    left, top, right, bottom = rect
    clip = [[left, top], [right, top], [right, bottom], [left, bottom]]
    for a, b in zip(clip, clip[1:]+clip[:1]):
        poly = half_plane(poly, a, b, True)
    return poly


def half_plane(poly: list, a: list, b: list, inside: bool) -> list:
    """Clip against one oriented edge. The cutter is counterclockwise."""
    result = []
    for p, q in zip(poly, poly[1:] + poly[:1]):
        dp = (b[0]-a[0])*(p[1]-a[1]) - (b[1]-a[1])*(p[0]-a[0])
        dq = (b[0]-a[0])*(q[1]-a[1]) - (b[1]-a[1])*(q[0]-a[0])
        keep_p, keep_q = (dp >= 0, dq >= 0) if inside else (dp <= 0, dq <= 0)
        if keep_p:
            result.append(p)
        if keep_p != keep_q:
            t = dp / (dp - dq)
            result.append([p[0] + t*(q[0]-p[0]), p[1] + t*(q[1]-p[1])])
    return result


def uncovered_area(face: list, covers: list) -> float:
    remaining = [face]
    for cover in covers:
        if area(cover) <= 1e-8:
            continue
        if sum(a[0]*b[1]-b[0]*a[1] for a, b in zip(cover, cover[1:]+cover[:1])) < 0:
            cover = list(reversed(cover))
        next_remaining = []
        for poly in remaining:
            inside = poly
            for a, b in zip(cover, cover[1:]+cover[:1]):
                outside = half_plane(inside, a, b, False)
                if area(outside) > 1e-8:
                    next_remaining.append(outside)
                inside = half_plane(inside, a, b, True)
                if len(inside) < 3:
                    break
        remaining = next_remaining
        if not remaining:
            return 0.0
        if len(remaining) > 10000:
            raise CaptureError("coverage polygon budget exceeded")
    return sum(area(poly) for poly in remaining)


def screen_face(submitted: dict, indices: list, screen: list, flags: int = 0) -> list:
    """RasterVS homogeneous clipping, including RasterPS's F3D far bound.

    A triangle crossing a clip plane can still cover most of the foreground.
    Rejecting the entire primitive would invent receiver-coverage gaps.
    """
    points = [screen[i] for i in indices]
    width, height = submitted["resolution"]
    x, y, vw, vh = submitted["viewport"]
    sx, sy = submitted["screen_scale"]
    ox, oy = submitted["screen_offset"]
    poly = [[((p[0]*2/width - 1)*sx + ox)*p[3],
             ((1-p[1]*2/height)*sy + oy)*p[3], p[2]*p[3], p[3]] for p in points]
    planes = [lambda p: p[3]-1e-8, lambda p: p[3]+p[0], lambda p: p[3]-p[0],
              lambda p: p[3]+p[1], lambda p: p[3]-p[1], lambda p: 1022/1024*p[3]-p[2]]
    if not flags & 2:  # NoN disables the near bound; RasterPS clamps that depth.
        planes.append(lambda p: p[2])
    for distance in planes:
        clipped = []
        for p, q in zip(poly, poly[1:]+poly[:1]):
            dp, dq = distance(p), distance(q)
            if dp >= 0:
                clipped.append(p)
            if (dp >= 0) != (dq >= 0):
                t = dp/(dp-dq)
                clipped.append([p[k]+t*(q[k]-p[k]) for k in range(4)])
        poly = clipped
    return [[x+(p[0]/p[3]+1)*vw/2, y+(1-p[1]/p[3])*vh/2] for p in poly]


def validate_pair(render: dict, present: dict, world: bytes, swap: bytes) -> list:
    if (render.get("schema") != 2 or present.get("schema") != 1
            or render["workload"] != present["workload"]
            or render["native"]["sequence"] != present["sequence"]
            or render["weight"] != 1 or render.get("gpu_indices_equal") is not True
            or present.get("successful_present") is not True or present.get("native_color_image") is not True
            or present["frames"] != 1 or present["frame"] != 0
            or present["format"] != "BGRA8"):
        raise CaptureError("unmatched/non-native/unsuccessful presentation")
    validate_draw_metadata(render.get("calls"))
    for call in render["calls"]:
        if call["indexed"] and (call["first"] > len(render["indices"])
                or call["count"] > len(render["indices"]) - call["first"]):
            raise CaptureError("indexed draw range outside workload indices")
    width, height, row = present["width"], present["height"], present["row_bytes"]
    if not (type(width) is int and type(height) is int and 0 < width <= 4096 and 0 < height <= 4096
            and row == (width * 4 + 255) // 256 * 256 and len(swap) == row * height):
        raise CaptureError("invalid swapchain extent")
    vertices = len(render["world_indices"])
    if not 0 < vertices <= 262144 or len(world) != vertices * 16 or render["world_bytes"] != len(world):
        raise CaptureError("invalid GPU world extent")
    points = list(struct.iter_unpack("<4f", world))
    if any(not all(math.isfinite(x) for x in p) or abs(p[3] - 1) > 1e-4 for p in points):
        raise CaptureError("invalid GPU world position")
    if len(render["local_positions"]) != vertices * 3:
        raise CaptureError("invalid native vertex extent")
    if any(not math.isfinite(v) for v in render["local_positions"]):
        raise CaptureError("nonfinite local position")
    if len(render["indices"]) > 786432 or len(render["indices"]) % 3 or any(type(i) is not int or not 0 <= i < vertices for i in render["indices"]):
        raise CaptureError("index outside presented vertices")
    groups = render["world_groups"]
    if any(type(i) is not int or not 0 <= i < len(groups) for i in render["world_indices"]):
        raise CaptureError("world transform outside group table")
    if len({c["call"] for c in render["calls"]}) != len(render["calls"]):
        raise CaptureError("duplicate workload call identity")
    identities = [(r["call"], r["first"], r["count"], r["indexed"], r["test_z"]) for r in render["raster"]]
    if len(set(identities)) != len(identities):
        raise CaptureError("duplicate raster submission")
    if any(not math.isfinite(v) or v <= 0 for v in present["video_resolution"]):
        raise CaptureError("invalid VI resolution")
    if any(not math.isfinite(v) for v in present["vi_viewport"]) or any(v <= 0 for v in present["vi_viewport"][2:]):
        raise CaptureError("invalid VI viewport")
    for r in render["raster"]:
        if any(not math.isfinite(v) or v <= 0 for v in r["resolution"]):
            raise CaptureError("invalid framebuffer resolution")
        if any(not math.isfinite(v) for key in ("screen_scale", "screen_offset", "viewport", "scissor") for v in r[key]):
            raise CaptureError("invalid raster mapping")
    return points


def validate_generated_material_inputs(render: dict, uv_bytes: bytes,
                                       buffers: dict[str, bytes]) -> dict:
    """Validate captured native bindings without admitting alpha casters.

    The raster render index selects RenderIndices; its instance index selects
    RenderParams. Those identities need not have the same numeric value.
    """
    vertices = len(render["world_indices"])
    if (render.get("generated_uv_bytes") != vertices * 8 or len(uv_bytes) != vertices * 8
            or render.get("generated_uv_layout") != "float2,RSPProcessCS,presented"):
        raise CaptureError("invalid generated UV extent or provenance")
    uv = list(struct.iter_unpack("<2f", uv_bytes))
    if any(not all(math.isfinite(x) for x in point) for point in uv):
        raise CaptureError("non-finite generated UV")
    layouts = {"render-params": 20, "rdp-tiles": 64, "gpu-tiles": 52}
    records = render.get("native_material_buffers")
    if (not isinstance(records, list) or len(records) != len(layouts)
            or any(not isinstance(record, dict) for record in records)):
        raise CaptureError("incomplete native material buffers")
    extents = {}
    sequence = render["native"]["sequence"]
    for record in records:
        name = record.get("name")
        count = record.get("count")
        if (name not in layouts or name in extents or type(count) is not int
                or not 0 < count <= 128 * 1024 * 1024 // layouts[name]
                or record.get("stride") != layouts[name]
                or record.get("bytes") != count * layouts[name]
                or record.get("file") != f"task-{sequence}-{name}.bin"
                or len(buffers.get(name, b"")) != record["bytes"]):
            raise CaptureError("invalid native material buffer extent or identity")
        extents[name] = count
    calls = {call["call"]: call for call in render["calls"]}
    for draw in render["raster"]:
        identity = draw.get("native_indices")
        call = calls.get(draw["call"])
        if call is None or not isinstance(identity, dict):
            raise CaptureError("raster draw lacks native material binding identity")
        if any(type(identity.get(key)) is not int or not 0 <= identity[key] <= 0xFFFFFFFF
               for key in ("instance_index", "face_start", "tile_start", "tile_count", "highlight_color")):
            raise CaptureError("invalid native RenderIndices value")
        instance = identity["instance_index"]
        material = call["material"]
        if (instance != draw["call"] or instance >= extents["render-params"]
                or identity["tile_start"] != material["tile_start"]
                or identity["tile_count"] != material["tile_count"]
                or identity["face_start"] != (call["first"] if draw["indexed"] else 0)):
            raise CaptureError("native material binding does not match game call")
        for name in ("rdp-tiles", "gpu-tiles"):
            if (identity["tile_start"] > extents[name]
                    or identity["tile_count"] > extents[name] - identity["tile_start"]):
                raise CaptureError("native tile binding exceeds uploaded buffer")
        cc_l, cc_h, om_l, om_h, flags = struct.unpack_from("<5I", buffers["render-params"], instance * 20)
        if (cc_l & 0xFFFFFF != material["combine_w0"] & 0xFFFFFF
                or cc_h != material["combine_w1"] or om_l != call["shader_other_lo"]
                or om_h != call["shader_other_hi"] or flags != call["shader_flags"]):
            raise CaptureError("uploaded native render parameters differ from selected shader")
    return {"uv_vertices": len(uv), "buffer_counts": extents,
            "raster_bindings": len(render["raster"]), "alpha_admitted": False}


def inspect(directory: Path, sequence: int, include_geometry: bool = False,
            require_overlay: bool = True) -> dict:
    native_path = directory / "rt-tasks" / f"task-{sequence}.json"
    meta = json.loads(native_path.read_text(encoding="utf-8"))
    native = TaskInspector(native_path.with_suffix(".bin").read_bytes(), meta).inspect()
    prefix = directory / "rt-render" / f"task-{sequence}"
    render = json.loads(prefix.with_name(prefix.name + "-render.json").read_text(encoding="utf-8"))
    present = json.loads(prefix.with_name(prefix.name + "-present.json").read_text(encoding="utf-8"))
    hle = json.loads(prefix.with_name(prefix.name + "-hle.json").read_text(encoding="utf-8"))
    validate_shadow_admission(render)
    if hle["first"] != hle["last"] or hle["first"] != render["workload"] or any(
            hle[key] != meta[key] for key in ("epoch", "sequence", "root")):
        raise CaptureError("task did not publish exactly one authenticated workload")
    points = validate_pair(render, present, prefix.with_name(prefix.name + "-world.bin").read_bytes(),
                           prefix.with_name(prefix.name + "-swap.bgra").read_bytes())
    screen_bytes = prefix.with_name(prefix.name + "-screen.bin").read_bytes()
    if len(screen_bytes) != len(points) * 16:
        raise CaptureError("invalid GPU screen extent")
    screen = list(struct.iter_unpack("<4f", screen_bytes))
    if any(not all(math.isfinite(x) for x in p) for p in screen):
        raise CaptureError("invalid GPU screen position")
    shade_bytes = prefix.with_name(prefix.name + "-shade.bin").read_bytes()
    if len(shade_bytes) != len(points) * 16:
        raise CaptureError("invalid GPU shade extent")
    shade = list(struct.iter_unpack("<4f", shade_bytes))
    generated_inputs = None
    if "generated_uv_bytes" in render or "native_material_buffers" in render:
        generated_inputs = validate_generated_material_inputs(render,
            prefix.with_name(prefix.name + "-uv.bin").read_bytes(),
            {name: prefix.with_name(prefix.name + f"-{name}.bin").read_bytes()
             for name in ("render-params", "rdp-tiles", "gpu-tiles")})
    if any(render["native"][key] != meta[key] for key in ("epoch", "sequence", "root", "phase", "circuit", "players")):
        raise CaptureError("native task/workload mismatch")
    for key in ("race_mode", "model_cursors"):
        if render["native"].get(key) != meta.get(key):
            raise CaptureError("native scene selectors differ from matching Workload")
    native_faces = defaultdict(list)
    for draw in native["draws"]:
        state = draw["state"]
        for local, world in zip(draw["local_faces"], draw["faces"]):
            native_faces[face_key(state["transform_group"], state["combine"], state["geometry"], local)].append((draw, local, world))
    calls = {c["call"]: c for c in render["calls"]}
    counts = Counter()
    car_faces = defaultdict(list)
    car_parts = defaultdict(list)
    matched_error = 0.0
    admitted = []
    casters, receivers = Counter(), Counter()
    overlay_faces = 0
    road_screen, overlay_screen = [], []
    road_source, overlay_source = [], []
    unsupported_overlay_projection = 0
    for submitted in render["raster"]:
        if not submitted["indexed"] or submitted["test_z"]:
            counts["unsupported raw/test-Z triangles"] += submitted["count"] // 3
            continue
        call = calls[submitted["call"]]
        first, count = submitted["first"], submitted["count"]
        if first != call["first"] or count != call["count"] or count % 3 or not (0 <= first <= len(render["indices"]) - count):
            raise CaptureError("raster range differs from authenticated call")
        m = call["material"]
        if call["color_address"] != present["color_address"]:
            counts["not the presented color image"] += count // 3
            continue
        combine = (m["combine_w0"] & 0xFFFFFF) << 32 | m["combine_w1"]
        for start in range(first, first + count, 3):
            indices = render["indices"][start:start+3]
            groups = [render["world_groups"][render["world_indices"][i]] for i in indices]
            if len(set(groups)) != 1:
                counts["mixed native groups"] += 1
                continue
            local = [render["local_positions"][i*3:i*3+3] for i in indices]
            gpu_world = [list(points[i][:3]) for i in indices]
            key = face_key(groups[0], combine, m["geometry"] & ~0x800000, local)
            candidates = []
            for draw, source_local, world in native_faces.get(key, []):
                state = draw["state"]
                # RSP initializes the unused low six OtherModeH bits to ones.
                if state["other_lo"] != m["other_lo"] or state["other_hi"] & ~63 != m["other_hi"] & ~63:
                    continue
                # Compare matched local corners, not triangle order/winding.
                mapping = {tuple(native_local): p for native_local, p in zip(source_local, world)}
                error = max(abs(mapping[tuple(native_local)][k] - p[k]) for native_local, p in zip(local, gpu_world) for k in range(3))
                if error < 0.005:
                    candidates.append((draw, error))
            if not candidates:
                counts["unmatched native identity"] += 1
                continue
            roles = set()
            for draw, error in candidates:
                matched_error = max(matched_error, error)
                obj = draw.get("native_object")
                if submitted["overlay"]:
                    roles.add("overlay")
                elif obj and belongs_to_physical_car(obj, render["native"]["objects"]):
                    roles.add("car")
                elif obj and procedural_world_object(obj):
                    roles.add("procedural world")
                else:
                    roles.update(r["record_slot"] for r in draw["segment_records"])
            if len(roles) != 1:
                counts["ambiguous/unsupported native role"] += 1
                continue
            role = roles.pop()
            projected = screen_face(submitted, indices, screen, call["shader_flags"])
            viewport_matches = submitted["viewport"] == [0, 0, *present["texture_extent"]]
            if projected:
                signed_area = sum(a[0]*b[1]-b[0]*a[1] for a, b in zip(projected, projected[1:]+projected[:1]))
                if call["shader_flags"] & 4 and signed_area >= 0:
                    projected = []  # Pinned D3D12 culls clockwise front faces.
                else:
                    projected = clip_scissor(projected, submitted["scissor"])
            source_projected = projected
            if viewport_matches:
                vx, vy, vw, vh = present["vi_viewport"]
                rw, rh = present["video_resolution"]
                projected = clip_scissor([[vx + p[0]*vw/rw, vy + p[1]*vh/rh] for p in projected], present["vi_scissor"])
            if role == "overlay":
                overlay_faces += 1
                counts["overlay omitted" if submitted["omitted"] else "overlay native"] += 1
                if viewport_matches:
                    overlay_screen.append(projected)
                    overlay_source.append(source_projected)
                else:
                    unsupported_overlay_projection += 1
                continue
            opaque = opaque_coverage(m)
            counts[f"{role}: {'opaque interior' if opaque else 'excluded coverage'}"] += 1
            if opaque and role == "car":
                car_faces[groups[0]].append(gpu_world)
                transforms = [render["world_indices"][i] for i in indices]
                if len(set(transforms)) == 1:
                    car_parts[transforms[0]].append(gpu_world)
            physical = role in ("car", "road", "walls", "scenery", "procedural world")
            if opaque and physical:
                casters[role] += 1
            fog_valid = all(type(render["fog_indices"][i]) is int and 0 <= render["fog_indices"][i] <= len(render["fog_params"])
                            and (render["fog_indices"][i] == 0 or all(math.isfinite(v) for v in render["fog_params"][render["fog_indices"][i]-1])) for i in indices)
            receiver = physical and receiver_material(call) and fog_valid and all(all(math.isfinite(v) for v in shade[i]) and 0 <= shade[i][3] <= 1 for i in indices)
            if receiver:
                admitted.append({"call": submitted["call"], "first": start, "count": 3, "role": role})
                receivers[role] += 1
            if receiver and role == "road":
                if viewport_matches:
                    road_screen.append(projected)
                    road_source.append(source_projected)
    validate_overlay_count(overlay_faces, require_overlay)
    result = {"sequence": sequence, "workload": render["workload"], "present": present["present"],
            "counts": dict(counts), "native_gpu_max_world_error": matched_error,
            "caster_candidate_triangles": dict(casters), "receiver_candidate_triangles": dict(receivers),
            "car_opaque_topology": {hex(g): topology(faces) for g, faces in car_faces.items()},
            "car_parts_topology": {str(g): topology(faces) for g, faces in car_parts.items()},
            "overlay_projection_uncovered_area": sum(uncovered_area(f, road_screen) for f in overlay_screen),
            "unsupported_overlay_projection_faces": unsupported_overlay_projection,
            "replacement_coverage": "not established by this geometry report; use the raster-owner differential for visible receiver ownership; production replacement remains unimplemented",
            "caster_policy": "authenticated submitted opaque physical triangles; no offscreen completion or solid proxy"}
    if generated_inputs is not None:
        result["generated_material_inputs"] = generated_inputs
    if include_geometry:
        result["road_screen"] = road_screen
        result["overlay_screen"] = overlay_screen
        result["road_source"] = road_source
        result["overlay_source"] = overlay_source
        result["present_info"] = present
        result["render_info"] = render
        result["receiver_ranges"] = admitted
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--allow-missing-overlay", action="store_true",
                        help="analyze presented materials without claiming the C1 native overlay identity")
    args = parser.parse_args()
    try:
        result = [inspect(args.run, n, require_overlay=not args.allow_missing_overlay) for n in (60, 300, 420, 540)]
        text = json.dumps(result, indent=2) + "\n"
        if args.output:
            args.output.write_text(text, encoding="utf-8")
        print(text)
    except (CaptureError, OSError, ValueError, KeyError, TypeError, IndexError) as error:
        parser.exit(1, f"render capture rejected: {error}\n")


if __name__ == "__main__":
    main()
