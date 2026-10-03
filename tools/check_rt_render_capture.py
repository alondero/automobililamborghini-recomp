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
            and not call["shader_flags"] & ((1 << 29) | (3 << 30) | 1)
            and len(m["fog_rgba"]) == 4
            and all(math.isfinite(v) and 0 <= v <= 1 for v in m["fog_rgba"]))


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
    for item in unclassified:
        if not isinstance(item, dict) or type(item.get("reason")) is not int or not 1 <= item["reason"] <= 6:
            raise CaptureError("unknown shadow unclassified reason")
        if type(item.get("count")) is not int or item["count"] < 0:
            raise CaptureError("invalid shadow unclassified face count")
        if item["reason"] == 5 and item["count"] != 0:
            raise CaptureError("overflowed shadow face count is not represented as zero")
    if shadow.get("complete") is True:
        if (shadow.get("authenticated") is not True or shadow.get("params_abi_valid") is not True
                or shadow.get("geometry", 0) <= 0 or shadow.get("overlays", 0) <= 0
                or rejected or unclassified):
            raise CaptureError("shadow workload claims completeness with missing or rejected admission")
    elif shadow.get("complete") is not False:
        raise CaptureError("shadow workload completeness must be boolean")


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
    if (render.get("schema") != 1 or present.get("schema") != 1
            or render["workload"] != present["workload"]
            or render["native"]["sequence"] != present["sequence"]
            or render["weight"] != 1 or render.get("gpu_indices_equal") is not True
            or present.get("successful_present") is not True or present.get("native_color_image") is not True
            or present["frames"] != 1 or present["frame"] != 0
            or present["format"] != "BGRA8"):
        raise CaptureError("unmatched/non-native/unsuccessful presentation")
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
    if any(render["native"][key] != meta[key] for key in ("epoch", "sequence", "root", "phase", "circuit", "players")):
        raise CaptureError("native task/workload mismatch")
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
            physical = role in ("car", "road", "walls", "scenery")
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
