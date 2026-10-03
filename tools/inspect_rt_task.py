#!/usr/bin/env python3
"""Inspect local, word-swapped USA task captures without exporting game assets.

This F3DEX diagnostic reports native command/material/transform provenance. It
does not implement RT64 interpolation, classify shadow eligibility, or validate
pixels. Captures contain ROM-derived data and must remain local and ignored.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path

# USA producer-owned signed halfwords, read through the word-swapped snapshot.
# Layout, timing and fail-closed behavior: docs/rt-shadows.md#guest-bridge-contract.
SCENE_SELECTOR_FIELDS = (("phase", 0x800CE6AC), ("circuit", 0x800CE794),
                         ("players", 0x800CE6A4), ("race_mode", 0x800CE6B4))
MODEL_CURSOR_ADDRESS = 0x800CE7E8
MODEL_CURSOR_STRIDE = 2


class CaptureError(ValueError):
    pass


class TaskInspector:
    def __init__(self, ram: bytes, metadata: dict):
        if not isinstance(metadata, dict) or len(ram) != 0x800000 or metadata.get("schema") != 2 or metadata.get("ram_layout") != "word-swapped":
            raise CaptureError("expected schema 2 and exactly 8 MiB of word-swapped RAM")
        if metadata.get("snapshot_point") != "producer-before-submit":
            raise CaptureError("snapshot must belong to the task producer before publication")
        if not metadata.get("emitters_complete"):
            raise CaptureError("emitter capture is incomplete")
        task = metadata.get("task_address")
        if task not in (0x800BF240, 0x800C6C90) or metadata.get("root") != task + 0x1C0:
            raise CaptureError("unknown task arena or non-root list")
        spans = metadata.get("emitters")
        if not isinstance(spans, list) or len(spans) > 64:
            raise CaptureError("invalid emitter span count")
        occupied = []
        for span in spans:
            if span["emitter"] not in (0x80009AC0, 0x8000F6D8, 0x800159FC, 0x8000E468):
                raise CaptureError("unknown emitter")
            begin, end = span["begin"], span["end"]
            if not (task + 0x1C0 <= begin <= end <= task + 0x79C0) or (begin | end) & 7:
                raise CaptureError("emitter span outside its task arena or unaligned")
            if span["camera_slot"] not in range(4):
                raise CaptureError("invalid emitter camera slot")
            if any(begin < old_end and old_begin < end for old_begin, old_end in occupied):
                raise CaptureError("overlapping emitter spans")
            if begin != end:
                occupied.append((begin, end))
        cameras = metadata.get("cameras", [])
        if len(cameras) > 4 or len({c["slot"] for c in cameras}) != len(cameras):
            raise CaptureError("duplicate or excess camera records")
        for camera in cameras:
            if camera["slot"] not in range(4) or not math.isfinite(camera["height_term"]):
                raise CaptureError("invalid camera record")
        self.ram = ram
        self.metadata = metadata
        self.segments = [0] * 16
        # Additive scene selectors in current schema 2 must agree with the
        # producer's RAM copy. Older captures retain their narrower evidence.
        if "race_mode" in metadata or "model_cursors" in metadata:
            selectors = metadata.get("model_cursors")
            if (any(type(metadata.get(name)) is not int for name, _ in SCENE_SELECTOR_FIELDS)
                    or not isinstance(selectors, list)
                    or len(selectors) != 4 or any(type(model) is not int for model in selectors)):
                raise CaptureError("incomplete race mode/model selector identity")
            if any(metadata.get(name) != self.read(address, "h") for name, address in SCENE_SELECTOR_FIELDS):
                raise CaptureError("scene selectors do not match producer RAM")
            if selectors != [self.read(MODEL_CURSOR_ADDRESS + player * MODEL_CURSOR_STRIDE, "h") for player in range(4)]:
                raise CaptureError("model selectors do not match producer RAM")
        self.geometry = self.other_hi = self.other_lo = self.combine = self.prim = self.fog = 0
        self.texture = self.light_count = 0
        self.lights = {}
        self.model = [self.identity()]
        self.model_address = [0]
        self.view = self.identity()
        self.view_address = 0
        self.groups = [0]
        self.vertices = {}
        self.draws = []
        self.commands = 0
        self.excluded_lists = []
        self.visibility_tests = []

    @staticmethod
    def identity():
        return [[float(i == j) for j in range(4)] for i in range(4)]

    @staticmethod
    def multiply(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

    def read(self, address, fmt):
        offset = self.resolve(address)
        size = struct.calcsize(fmt)
        offset ^= {1: 3, 2: 2}.get(size, 0)
        if offset + size > len(self.ram):
            raise CaptureError("RAM read exceeds capture")
        return struct.unpack_from("<" + fmt, self.ram, offset)[0]

    def resolve(self, address):
        high = address >> 24
        if 0x80 <= high <= 0xBF:
            offset = address & 0x1FFFFFFF
        elif high < 16:
            offset = (self.segments[high] & 0x1FFFFFFF) + (address & 0xFFFFFF)
        else:
            raise CaptureError(f"unsupported address {address:#x}")
        if offset >= len(self.ram):
            raise CaptureError(f"address outside RAM {address:#x}")
        return offset

    def matrix(self, address):
        # N64 Mtx has sixteen signed integer halfwords followed by sixteen
        # unsigned fractional halfwords, stored in row-vector order.
        return [[self.read(address + (i * 4 + j) * 2, "h") +
                 self.read(address + 32 + (i * 4 + j) * 2, "H") / 65536
                 for j in range(4)] for i in range(4)]

    def owner(self, address, inherited):
        physical = self.resolve(address)
        matches = [s for s in self.metadata["emitters"] if
                   (s["begin"] & 0x1FFFFFFF) <= physical < (s["end"] & 0x1FFFFFFF)]
        if len(matches) > 1:
            raise CaptureError("overlapping emitter spans")
        return matches[0] if matches else inherited

    def triangle(self, command, indices, owner, stack):
        try:
            vertices = [self.vertices[i] for i in indices]
        except KeyError as error:
            raise CaptureError("triangle references an unloaded vertex") from error
        vertex_state = vertices[0]["state"]
        if any(v["state"] != vertex_state or v["model"] != vertices[0]["model"] or
               v["view"] != vertices[0]["view"] for v in vertices):
            raise CaptureError("triangle mixes vertex transform/light contexts")
        state = {"emitter": owner["emitter"] if owner else 0,
                 "camera_slot": owner["camera_slot"] if owner else -1,
                 "list": stack[-1], "call_path": list(stack), "geometry": self.geometry, **vertex_state,
                 "other_hi": self.other_hi, "other_lo": self.other_lo,
                 "combine": self.combine, "prim": self.prim, "fog": self.fog,
                 "texture": self.texture}
        if (not self.draws or self.draws[-1]["state"] != state or
                self.draws[-1]["model"] != vertices[0]["model"] or self.draws[-1]["view"] != vertices[0]["view"]):
            self.draws.append({"state": state, "first_command": command, "triangles": 0,
                               "model": vertices[0]["model"], "view": vertices[0]["view"],
                               "min": [float("inf")] * 3, "max": [float("-inf")] * 3,
                               "vertex_addresses": set(), "faces": [], "local_faces": []})
        draw = self.draws[-1]
        draw["triangles"] += 1
        # Local, ignored evidence only. Needed to authenticate a presented GPU
        # triangle against native emitter/segment identity; never an asset bank.
        draw["faces"].append([v["pos"] for v in vertices])
        draw["local_faces"].append([v["local"] for v in vertices])
        for vertex in vertices:
            draw["vertex_addresses"].add(vertex["address"])
            for i in range(3):
                draw["min"][i] = min(draw["min"][i], vertex["pos"][i])
                draw["max"][i] = max(draw["max"][i], vertex["pos"][i])

    def walk(self, address, inherited=None, stack=()):
        if len(stack) >= 16:
            raise CaptureError("display-list depth budget exceeded")
        stack = (*stack, address)
        offset = self.resolve(address)
        if offset & 7:
            raise CaptureError("unaligned display list")
        while True:
            self.commands += 1
            if self.commands > 200000:
                raise CaptureError("display-list command budget exceeded")
            command = offset | 0x80000000
            owner = self.owner(command, inherited)
            w0, w1 = self.read(command, "I"), self.read(command + 4, "I")
            op = w0 >> 24
            if op == 0xB8:
                return
            if op == 0x64:
                extended = w0 & 0xFFFFFF
                if extended in (0x0C, 0x13):
                    flags = self.read(command + 8, "I")
                    if not flags & 2:
                        if flags & 1:
                            self.groups.append(w1)
                        else:
                            self.groups[-1] = w1
                    offset += 16
                elif extended in (0x05, 0x06, 0x07):
                    offset += 16
                elif extended == 0x0D:
                    if not w1 & 0x100:
                        count = w1 & 0xFF
                        if count >= len(self.groups):
                            raise CaptureError("transform group stack underflow")
                        if count:
                            del self.groups[-count:]
                    offset += 8
                elif extended in (0x2C, 0x33, 0x09, 0x15, 0x16, 0x17, 0x18):
                    offset += 8
                else:
                    raise CaptureError(f"unsupported extended command {extended:#x}")
                continue
            if op == 0x06:
                if owner and owner["emitter"] == 0x8000F6D8 and (w1 & 0x1FFFFFFF) >= len(self.ram):
                    # The port's panorama extension uses recomp::alloc beyond
                    # low RAM. Its list contains texture/vertex/triangle work,
                    # no matrix changes (lambo_sky_widescreen.cpp). Sky is
                    # excluded by emitter, never treated as physical geometry.
                    self.excluded_lists.append({"address": w1, "reason": "port panorama extension"})
                else:
                    self.walk(w1, owner, stack)
                if w0 & 0xFF0000:
                    return
            elif op == 0x01:
                flags = (w0 >> 16) & 0xFF
                matrix = self.matrix(w1)
                if flags & 1:
                    # Projection loads reset the extracted view. An affine
                    # projection multiply is the native guLookAt view matrix.
                    if flags & 2:
                        self.view, self.view_address = self.identity(), 0
                    elif all(matrix[i][3] == 0 for i in range(3)) and matrix[3][3] == 1:
                        self.view = self.multiply(matrix, self.view)
                        self.view_address = w1
                else:
                    if flags & 4:
                        self.model.append(self.model[-1])
                        self.model_address.append(self.model_address[-1])
                    self.model[-1] = matrix if flags & 2 else self.multiply(matrix, self.model[-1])
                    self.model_address[-1] = w1
            elif op == 0xBD:
                if len(self.model) <= 1:
                    raise CaptureError("model matrix stack underflow")
                self.model.pop()
                self.model_address.pop()
            elif op == 0x04:
                count, start = (w0 >> 10) & 0x3F, ((w0 >> 16) & 0xFF) >> 1
                if count == 0 or start + count > 64:
                    raise CaptureError("invalid F3DEX vertex load")
                for i in range(count):
                    source = self.resolve(w1) + i * 16
                    p = [self.read((source + j * 2) | 0x80000000, "h") for j in range(3)] + [1]
                    pos = [sum(p[k] * self.model[-1][k][j] for k in range(4)) for j in range(3)]
                    lit = bool(self.geometry & 0x20000)
                    self.vertices[start + i] = {
                        "address": source, "pos": pos, "local": p[:3], "model": self.model[-1], "view": self.view,
                        "state": {"model_address": self.model_address[-1], "view_address": self.view_address,
                                  "transform_group": self.groups[-1], "vertex_lit": lit,
                                  "light_count": self.light_count if lit else 0,
                                  "lights": [self.lights.get(j) for j in range(self.light_count + 1)] if lit else []}}
            elif op in (0xBF, 0xB1):
                if op == 0xB1:
                    self.triangle(command, [(w0 >> shift & 0xFF) // 2 for shift in (16, 8, 0)], owner, stack)
                self.triangle(command, [(w1 >> shift & 0xFF) // 2 for shift in (16, 8, 0)], owner, stack)
            elif op in (0xB6, 0xB7):
                self.geometry = (self.geometry & ~w1) if op == 0xB6 else (self.geometry | w1)
            elif op in (0xB9, 0xBA):
                shift, length = (w0 >> 8) & 0xFF, w0 & 0xFF
                if shift + length > 32:
                    raise CaptureError("invalid other-mode bit range")
                mask = ((1 << length) - 1) << shift
                if op == 0xB9:
                    self.other_lo = (self.other_lo & ~mask) | (w1 & mask)
                else:
                    self.other_hi = (self.other_hi & ~mask) | (w1 & mask)
            elif op == 0xEF:
                self.other_hi, self.other_lo = w0 & 0xFFFFFF, w1
            elif op == 0xFC:
                self.combine = (w0 & 0xFFFFFF) << 32 | w1
            elif op == 0xFA:
                self.prim = w1
            elif op == 0xF8:
                self.fog = w1
            elif op == 0xFD:
                self.texture = w1
            elif op == 0xBC:
                kind = w0 & 0xFF
                if kind == 6:
                    self.segments[(w0 >> 10) & 15] = w1
                elif kind == 2:
                    self.light_count = (w1 & 0xFFFF) // 32 - 1
                    if not 0 <= self.light_count <= 7:
                        raise CaptureError("invalid directional light count")
            elif op == 0x03:
                index = (w0 >> 16) & 0xFF
                if 0x86 <= index <= 0x94 and index % 2 == 0:
                    self.lights[(index - 0x86) // 2] = {
                        "address": w1, "rgb": [self.read(w1 + i, "B") for i in range(3)],
                        "direction": [self.read(w1 + 8 + i, "b") for i in range(3)]}
            elif op in (0xB2, 0xB0, 0xAF):
                # Vertex modification/test/load-ucode can invalidate geometry
                # or face identity. Refuse a misleading diagnostic report.
                raise CaptureError(f"unsupported geometry command {op:#x} at {command:#x}")
            elif op == 0xBE:
                # Conservative candidate enumeration, not RT64 draw selection:
                # extended projection/clipping is intentionally not reproduced.
                self.visibility_tests.append({"command": command, "start": w0 & 0xFFFF, "end": w1})
            elif op not in (0, 0xB3, 0xB4, 0xBB, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
                            0xED, 0xEE, 0xF0, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9,
                            0xFB, 0xFE, 0xFF):
                raise CaptureError(f"unsupported command {op:#x} at {command:#x}")
            offset += 8

    def inspect(self):
        self.walk(self.metadata["root"])
        for draw in self.draws:
            draw["vertex_addresses"] = sorted(draw["vertex_addresses"])
        self.annotate_records()
        return {"schema": 1, "task": self.metadata, "commands": self.commands,
                "excluded_lists": self.excluded_lists, "draws": self.draws,
                "unevaluated_visibility_tests": self.visibility_tests,
                "eligibility": "unproved; native observations only"}

    def annotate_records(self):
        # Offline observations of the USA layouts already used by
        # lambo_interpolation.cpp and lambo_no_lod.cpp. A slot name does not
        # prove opacity, native coverage, or an executed/interpolated RT64 draw.
        segments = {}
        header = self.read(0x80098238, "I")
        records = self.read(0x800BF1D0, "I")
        if 0x80000000 <= header <= 0x807FFFF4 and 0x80000000 <= records < 0x80800000:
            start, end = self.read(header + 4, "I"), self.read(header + 8, "I")
            size = end - start
            if size > 0 and size % 20 == 0 and size // 20 <= 256 and (records & 0x1FFFFFFF) + (size // 20) * 64 <= len(self.ram):
                for index in range(size // 20):
                    for offset, role in ((4, "road"), (8, "walls"), (12, "scenery")):
                        child = self.read(records + index * 64 + offset, "I")
                        if child:
                            segments.setdefault(child, []).append({"segment": index, "record_slot": role})
        for draw in self.draws:
            state = draw["state"]
            draw["segment_records"] = [record for child in state["call_path"] for record in segments.get(child, [])]
            group = state["transform_group"]
            if state["emitter"] == 0x80009AC0 and group & 0xFFF00000 == 0x10000000:
                index = group & 0xFFFF
                # An observation budget, not a claim about the native capacity.
                if index < 128:
                    address = 0x800B69A8 + index * 0x10C
                    draw["native_object"] = {"index": index, "address": address,
                        "flags": self.read(address, "H"), "list": self.read(address + 8, "I"),
                        "parent": self.read(address + 0x58, "h")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="task-N.json next to task-N.bin")
    parser.add_argument("--output", type=Path, required=True, help="local JSON report")
    args = parser.parse_args()
    try:
        metadata = json.loads(args.capture.read_text(encoding="utf-8"))
        result = TaskInspector(args.capture.with_suffix(".bin").read_bytes(), metadata).inspect()
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"task {metadata['sequence']}: {result['commands']} commands, {len(result['draws'])} native draw groups")
    except (CaptureError, OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"capture rejected: {error}\n")


if __name__ == "__main__":
    main()
