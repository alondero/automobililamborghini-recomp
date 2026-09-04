#!/usr/bin/env python3
"""Extract and compile the first safe, deliberately narrow track-edit format.

Track Lab v1 edits only the active track's authored visibility (PVS) rows.  It
also exports the segment records, referenced cull anchors, and one assumed
waypoint record per segment for inspection.  Those records are intentionally
not compiled: geometry, collision, and the true waypoint extent are not yet
understood well enough to expose as writable data.

The input snapshot is either:

* exactly 8 MiB of raw N64Recomp RDRAM (32-bit words stored little-endian), or
* a ``LMBOSTAT`` v1 32-byte header followed by that same 8 MiB payload.

Usage::

    python tools/track_lab.py extract race.lstate circuit.json
    python tools/track_lab.py validate circuit.json
    python tools/track_lab.py diff circuit.json
    python tools/track_lab.py compile circuit.json circuit.altrk

The module is dependency-free so it can be used from a stock Python install.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


RDRAM_BASE = 0x80000000
RDRAM_SIZE = 0x800000
RDRAM_END = RDRAM_BASE + RDRAM_SIZE

ACTIVE_CONTEXT_PTR = 0x80098238
ACTIVE_CIRCUIT = 0x800CE794

DOCUMENT_FORMAT = "al-track-document"
DOCUMENT_VERSION = 1
GAME_ID = "lamborghini.us"
ROM_XXH3_64 = "525201d7279f34e3"

SLOTS_PER_ROW = 10
PVS_ROW_SIZE = SLOTS_PER_ROW * 2
MIN_ROW_COUNT = 2
MAX_ROW_COUNT = 200

CAPABILITIES_EDITABLE = ("visibility",)
CAPABILITIES_INSPECT_ONLY = ("segments", "anchors", "waypoints")
CAPABILITIES_UNSUPPORTED = ("geometry", "collision", "new_track")

PATCH_MAGIC = b"ALTRKPV1"
PATCH_VERSION = 1
PATCH_HEADER_SIZE = 64
PATCH_FLAGS_PVS_CORRECTIONS = 1
PATCH_HEADER = struct.Struct("<8sHHIQIBBHIIQQQ")
PATCH_EDIT = struct.Struct("<HBBhh")

FNV1A64_OFFSET = 0xCBF29CE484222325
FNV1A64_PRIME = 0x100000001B3
U64_MASK = 0xFFFFFFFFFFFFFFFF

WAYPOINT_ASSUMPTION = (
    "Assumption: context +0x08 is both the PVS end and the waypoint base; "
    "exactly segment_count 16-byte records are shown for inspection, but the "
    "waypoint table's true extent and the semantics of its unknown fields are "
    "not yet proven."
)

_HEX16_RE = re.compile(r"^[0-9a-f]{16}$")
_HEX32_RE = re.compile(r"^0x[0-9a-f]{8}$")
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class TrackLabError(ValueError):
    """An input error suitable for presenting directly to a CLI user."""


@dataclass(frozen=True)
class Snapshot:
    rdram: bytes
    snapshot_format: str
    snapshot_sha256: str
    rdram_sha256: str
    savestate_state: int | None


@dataclass(frozen=True)
class VisibilityEdit:
    row: int
    slot: int
    before: int | None
    after: int | None
    expected_raw: int
    replacement_raw: int


@dataclass(frozen=True)
class ValidatedDocument:
    document: dict[str, Any]
    row_count: int
    raw_base_rows: list[list[int]]
    edits: list[VisibilityEdit]
    base_hash: int
    patched_hash: int


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _hex_pointer(value: int) -> str | None:
    return None if value == 0 else f"0x{value:08x}"


def _fnv_hex(value: int) -> str:
    return f"{value:016x}"


def fnv1a64_pvs(rows: Iterable[Iterable[int]]) -> int:
    """Fingerprint signed PVS values as their logical big-endian halfword bytes."""
    result = FNV1A64_OFFSET
    for row in rows:
        for value in row:
            if not _is_int(value) or not -0x8000 <= value <= 0x7FFF:
                raise TrackLabError(f"PVS fingerprint value is not an s16: {value!r}")
            for byte in struct.pack(">h", value):
                result ^= byte
                result = (result * FNV1A64_PRIME) & U64_MASK
    return result


class Rdram:
    """Logical big-endian reads over N64Recomp's raw word-swapped byte array."""

    def __init__(self, physical: bytes):
        if len(physical) != RDRAM_SIZE:
            raise TrackLabError(
                f"RDRAM payload is {len(physical)} bytes; expected exactly {RDRAM_SIZE}"
            )
        self._physical = physical

    @staticmethod
    def require_span(address: int, size: int, alignment: int, label: str) -> None:
        if not _is_int(address):
            raise TrackLabError(f"{label} is not an integer guest address")
        if address % alignment:
            raise TrackLabError(
                f"{label} 0x{address:08x} is not {alignment}-byte aligned"
            )
        if size < 0 or address < RDRAM_BASE or address + size > RDRAM_END:
            raise TrackLabError(
                f"{label} span 0x{address:08x}..0x{address + size:08x} "
                "is outside 8 MiB guest RDRAM"
            )

    @staticmethod
    def require_end_pointer(address: int, alignment: int, label: str) -> None:
        if not _is_int(address) or address < RDRAM_BASE or address > RDRAM_END:
            shown = f"0x{address:08x}" if _is_int(address) else repr(address)
            raise TrackLabError(f"{label} {shown} is outside 8 MiB guest RDRAM")
        if address % alignment:
            raise TrackLabError(
                f"{label} 0x{address:08x} is not {alignment}-byte aligned"
            )

    def logical_bytes(self, address: int, size: int, *, label: str = "read") -> bytes:
        self.require_span(address, size, 1, label)
        offset = address - RDRAM_BASE
        # MEM_B uses address ^ 3.  RDRAM_BASE is word-aligned, so applying the
        # xor to the payload-relative byte offset recovers logical N64 order.
        return bytes(self._physical[(offset + i) ^ 3] for i in range(size))

    def u16(self, address: int, *, label: str = "u16 read") -> int:
        self.require_span(address, 2, 2, label)
        return int.from_bytes(self.logical_bytes(address, 2, label=label), "big")

    def s16(self, address: int, *, label: str = "s16 read") -> int:
        value = self.u16(address, label=label)
        return value - 0x10000 if value >= 0x8000 else value

    def u32(self, address: int, *, label: str = "u32 read") -> int:
        self.require_span(address, 4, 4, label)
        return int.from_bytes(self.logical_bytes(address, 4, label=label), "big")


def read_snapshot(path: str | os.PathLike[str]) -> Snapshot:
    snapshot_path = Path(path)
    try:
        blob = snapshot_path.read_bytes()
    except OSError as exc:
        raise TrackLabError(f"cannot read snapshot {snapshot_path}: {exc}") from exc

    snapshot_hash = hashlib.sha256(blob).hexdigest()
    state: int | None = None
    if len(blob) == RDRAM_SIZE:
        payload = blob
        snapshot_format = "raw-word-swapped-rdram"
    elif blob[:8] == b"LMBOSTAT":
        if len(blob) < 32:
            raise TrackLabError("LMBOSTAT file is truncated before its 32-byte header")
        magic, version, rdram_size, state, *_reserved = struct.unpack(
            "<8s6I", blob[:32]
        )
        if magic != b"LMBOSTAT":
            raise TrackLabError("save-state has invalid LMBOSTAT magic")
        if version != 1:
            raise TrackLabError(f"unsupported LMBOSTAT version {version}; expected 1")
        if rdram_size != RDRAM_SIZE:
            raise TrackLabError(
                f"LMBOSTAT declares {rdram_size} RDRAM bytes; expected {RDRAM_SIZE}"
            )
        expected_size = 32 + RDRAM_SIZE
        if len(blob) != expected_size:
            raise TrackLabError(
                f"LMBOSTAT file is {len(blob)} bytes; expected exactly {expected_size}"
            )
        payload = blob[32:]
        snapshot_format = "lmbostat-v1"
    else:
        raise TrackLabError(
            f"snapshot is {len(blob)} bytes; expected raw {RDRAM_SIZE}-byte RDRAM "
            f"or a {32 + RDRAM_SIZE}-byte LMBOSTAT v1 file"
        )

    return Snapshot(
        rdram=payload,
        snapshot_format=snapshot_format,
        snapshot_sha256=snapshot_hash,
        rdram_sha256=hashlib.sha256(payload).hexdigest(),
        savestate_state=state,
    )


def extract_document(path: str | os.PathLike[str]) -> dict[str, Any]:
    """Extract an ``al-track-document`` v1 dictionary from a settled race snapshot."""
    snapshot = read_snapshot(path)
    memory = Rdram(snapshot.rdram)

    context = memory.u32(ACTIVE_CONTEXT_PTR, label="active context pointer")
    memory.require_span(context, 0x14, 4, "active track context")

    segment_base = memory.u32(context + 0x00, label="context +0x00 segments")
    pvs_base = memory.u32(context + 0x04, label="context +0x04 PVS base")
    pvs_end = memory.u32(context + 0x08, label="context +0x08 PVS end/waypoints")
    unknown_base = memory.u32(context + 0x0C, label="context +0x0c unknown table")
    anchor_base = memory.u32(context + 0x10, label="context +0x10 anchors")

    memory.require_end_pointer(pvs_base, 2, "PVS base")
    memory.require_end_pointer(pvs_end, 2, "PVS end")
    if pvs_end <= pvs_base:
        raise TrackLabError(
            f"PVS end 0x{pvs_end:08x} does not follow base 0x{pvs_base:08x}"
        )
    pvs_size = pvs_end - pvs_base
    if pvs_size % PVS_ROW_SIZE:
        raise TrackLabError(
            f"PVS span is {pvs_size} bytes; it is not a whole number of "
            f"{PVS_ROW_SIZE}-byte rows"
        )
    row_count = pvs_size // PVS_ROW_SIZE
    if not MIN_ROW_COUNT <= row_count <= MAX_ROW_COUNT:
        raise TrackLabError(
            f"PVS has {row_count} rows; supported range is "
            f"{MIN_ROW_COUNT}..{MAX_ROW_COUNT}"
        )

    memory.require_span(pvs_base, pvs_size, 2, "PVS block")
    memory.require_span(segment_base, row_count * 64, 4, "segment record block")
    # +0x0c is not decoded yet, but a null, unaligned, or out-of-RDRAM pointer is
    # still evidence that this is not a valid live race context.
    memory.require_span(unknown_base, 4, 4, "context +0x0c table")
    memory.require_span(anchor_base, 16, 2, "anchor record block")
    memory.require_span(pvs_end, row_count * 16, 4, "assumed waypoint record block")

    circuit = memory.s16(ACTIVE_CIRCUIT, label="active circuit index")
    if not 0 <= circuit <= 5:
        raise TrackLabError(
            f"active circuit index is {circuit}; expected a race circuit in 0..5"
        )

    raw_base_rows: list[list[int]] = []
    for row_index in range(row_count):
        row: list[int] = []
        for slot in range(SLOTS_PER_ROW):
            value = memory.s16(
                pvs_base + row_index * PVS_ROW_SIZE + slot * 2,
                label=f"PVS row {row_index} slot {slot}",
            )
            if value >= row_count:
                raise TrackLabError(
                    f"PVS row {row_index} slot {slot} references segment {value}; "
                    f"valid indices are 0..{row_count - 1} (negative values are holes)"
                )
            row.append(value)
        raw_base_rows.append(row)

    base_rows: list[list[int | None]] = [
        [None if value < 0 else value for value in row] for row in raw_base_rows
    ]

    segment_parts: list[tuple[dict[str, Any], int]] = []
    referenced_anchor_indices: set[int] = set()
    for index in range(row_count):
        address = segment_base + index * 64
        raw = memory.logical_bytes(address, 64, label=f"segment {index}")
        road_dl, wall_dl, far_scenery_dl = struct.unpack_from(">III", raw, 4)
        offset_x, offset_z = struct.unpack_from(">hh", raw, 0x10)
        anchor_index = struct.unpack_from(">h", raw, 0x20)[0]
        if anchor_index < 0:
            raise TrackLabError(
                f"segment {index} has negative cull-anchor index {anchor_index}"
            )
        memory.require_span(
            anchor_base + anchor_index * 16,
            16,
            2,
            f"segment {index} referenced anchor {anchor_index}",
        )
        referenced_anchor_indices.add(anchor_index)
        segment_parts.append(
            (
                {
                    "index": index,
                    "raw_be_hex": raw.hex(),
                    "decoded": {
                        "road_dl": _hex_pointer(road_dl),
                        "wall_dl": _hex_pointer(wall_dl),
                        "far_scenery_dl": _hex_pointer(far_scenery_dl),
                        "cull_anchor": {
                            "anchor_index": anchor_index,
                            "segment_offset_x": offset_x,
                            "segment_offset_z": offset_z,
                        },
                    },
                },
                anchor_index,
            )
        )

    anchors: list[dict[str, Any]] = []
    anchor_coordinates: dict[int, tuple[int, int]] = {}
    for index in sorted(referenced_anchor_indices):
        address = anchor_base + index * 16
        raw = memory.logical_bytes(address, 16, label=f"anchor {index}")
        x, z = struct.unpack_from(">hh", raw, 0)
        anchor_coordinates[index] = (x, z)
        anchors.append(
            {
                "index": index,
                "raw_be_hex": raw.hex(),
                "decoded": {"x": x, "z": z},
            }
        )

    segments: list[dict[str, Any]] = []
    for segment, anchor_index in segment_parts:
        anchor_x, anchor_z = anchor_coordinates[anchor_index]
        cull = segment["decoded"]["cull_anchor"]
        cull["world_x"] = anchor_x + cull["segment_offset_x"]
        cull["world_z"] = anchor_z + cull["segment_offset_z"]
        segments.append(segment)

    waypoint_records: list[dict[str, Any]] = []
    for index in range(row_count):
        address = pvs_end + index * 16
        raw = memory.logical_bytes(address, 16, label=f"waypoint {index}")
        plane_a, plane_b, progress, unknown_u16, unknown_u32 = struct.unpack(
            ">ffhHI", raw
        )
        if not math.isfinite(plane_a) or not math.isfinite(plane_b):
            raise TrackLabError(
                f"assumed waypoint {index} contains a non-finite coordinate; "
                "snapshot/context is not safe to extract"
            )
        waypoint_records.append(
            {
                "index": index,
                "raw_be_hex": raw.hex(),
                "decoded": {
                    "plane_a": plane_a,
                    "plane_b": plane_b,
                    "progress_or_extent": progress,
                    "unknown_u16": unknown_u16,
                    "unknown_u32": unknown_u32,
                },
            }
        )

    base_hash = fnv1a64_pvs(raw_base_rows)
    return {
        "format": DOCUMENT_FORMAT,
        "version": DOCUMENT_VERSION,
        "target": {
            "game_id": GAME_ID,
            "rom_xxh3_64": ROM_XXH3_64,
            "circuit": circuit,
        },
        "provenance": {
            "snapshot_format": snapshot.snapshot_format,
            "snapshot_sha256": snapshot.snapshot_sha256,
            "rdram_sha256": snapshot.rdram_sha256,
            "rdram_size": RDRAM_SIZE,
            "savestate_state": snapshot.savestate_state,
            "active_context_pointer_address": _hex_pointer(ACTIVE_CONTEXT_PTR),
            "active_context": _hex_pointer(context),
            "context_pointers": {
                "segments": _hex_pointer(segment_base),
                "visibility_base": _hex_pointer(pvs_base),
                "visibility_end_waypoints": _hex_pointer(pvs_end),
                "unknown_table": _hex_pointer(unknown_base),
                "anchors": _hex_pointer(anchor_base),
            },
        },
        "capabilities": {
            "editable": list(CAPABILITIES_EDITABLE),
            "inspect_only": list(CAPABILITIES_INSPECT_ONLY),
            "unsupported": list(CAPABILITIES_UNSUPPORTED),
        },
        "visibility": {
            "row_count": row_count,
            "slots_per_row": SLOTS_PER_ROW,
            "base_fnv1a64": _fnv_hex(base_hash),
            "base_rows": base_rows,
            "raw_base_rows": raw_base_rows,
            "rows": [row.copy() for row in base_rows],
        },
        "segments": segments,
        "anchors": anchors,
        "waypoints": {
            "assumption_warning": WAYPOINT_ASSUMPTION,
            "records": waypoint_records,
        },
    }


def _object_pairs_no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise TrackLabError(f"JSON contains duplicate object key {key!r}")
        result[key] = value
    return result


def load_document(path: str | os.PathLike[str]) -> dict[str, Any]:
    document_path = Path(path)
    try:
        text = document_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise TrackLabError(f"cannot read JSON document {document_path}: {exc}") from exc
    try:
        value = json.loads(text, object_pairs_hook=_object_pairs_no_duplicates)
    except TrackLabError:
        raise
    except json.JSONDecodeError as exc:
        raise TrackLabError(
            f"invalid JSON in {document_path} at line {exc.lineno}, "
            f"column {exc.colno}: {exc.msg}"
        ) from exc
    if not isinstance(value, dict):
        raise TrackLabError("document root must be a JSON object")
    return value


def _require_object(value: Any, where: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise TrackLabError(f"{where} must be an object")
    return value


def _require_array(value: Any, where: str) -> list[Any]:
    if not isinstance(value, list):
        raise TrackLabError(f"{where} must be an array")
    return value


def _require_keys(
    value: dict[str, Any],
    required: set[str],
    where: str,
    optional: set[str] | None = None,
) -> None:
    optional = optional or set()
    missing = required - value.keys()
    unknown = value.keys() - required - optional
    if missing:
        raise TrackLabError(f"{where} is missing field(s): {', '.join(sorted(missing))}")
    if unknown:
        raise TrackLabError(f"{where} has unknown field(s): {', '.join(sorted(unknown))}")


def _require_int(value: Any, where: str, minimum: int, maximum: int) -> int:
    if not _is_int(value) or not minimum <= value <= maximum:
        raise TrackLabError(f"{where} must be an integer in {minimum}..{maximum}")
    return value


def _require_hex(value: Any, regex: re.Pattern[str], where: str) -> str:
    if not isinstance(value, str) or regex.fullmatch(value) is None:
        raise TrackLabError(f"{where} has invalid hexadecimal encoding")
    return value


def _require_raw_hex(value: Any, byte_count: int, where: str) -> bytes:
    if (
        not isinstance(value, str)
        or len(value) != byte_count * 2
        or re.fullmatch(r"[0-9a-f]+", value) is None
    ):
        raise TrackLabError(
            f"{where} must be exactly {byte_count * 2} lowercase hex digits"
        )
    return bytes.fromhex(value)


def _require_pointer(value: Any, where: str, *, nullable: bool = False) -> int:
    if nullable and value is None:
        return 0
    text = _require_hex(value, _HEX32_RE, where)
    return int(text[2:], 16)


def _validate_string_array(value: Any, where: str) -> list[str]:
    values = _require_array(value, where)
    if not all(isinstance(item, str) for item in values):
        raise TrackLabError(f"{where} must contain only strings")
    if len(values) != len(set(values)):
        raise TrackLabError(f"{where} contains duplicate labels")
    return values


def _validate_semantic_rows(
    value: Any, row_count: int, where: str
) -> list[list[int | None]]:
    rows = _require_array(value, where)
    if len(rows) != row_count:
        raise TrackLabError(f"{where} has {len(rows)} rows; expected {row_count}")
    result: list[list[int | None]] = []
    for row_index, candidate in enumerate(rows):
        row = _require_array(candidate, f"{where}[{row_index}]")
        if len(row) != SLOTS_PER_ROW:
            raise TrackLabError(
                f"{where}[{row_index}] has {len(row)} slots; expected {SLOTS_PER_ROW}"
            )
        checked: list[int | None] = []
        for slot, item in enumerate(row):
            if item is None:
                checked.append(None)
            elif _is_int(item) and 0 <= item < row_count:
                checked.append(item)
            else:
                raise TrackLabError(
                    f"{where}[{row_index}][{slot}] must be null or a segment "
                    f"index in 0..{row_count - 1}"
                )
        result.append(checked)
    return result


def _validate_raw_rows(value: Any, row_count: int) -> list[list[int]]:
    rows = _require_array(value, "visibility.raw_base_rows")
    if len(rows) != row_count:
        raise TrackLabError(
            f"visibility.raw_base_rows has {len(rows)} rows; expected {row_count}"
        )
    result: list[list[int]] = []
    for row_index, candidate in enumerate(rows):
        row = _require_array(candidate, f"visibility.raw_base_rows[{row_index}]")
        if len(row) != SLOTS_PER_ROW:
            raise TrackLabError(
                f"visibility.raw_base_rows[{row_index}] has {len(row)} slots; "
                f"expected {SLOTS_PER_ROW}"
            )
        checked: list[int] = []
        for slot, item in enumerate(row):
            if not _is_int(item) or not -0x8000 <= item <= 0x7FFF:
                raise TrackLabError(
                    f"visibility.raw_base_rows[{row_index}][{slot}] must be an s16"
                )
            if item >= row_count:
                raise TrackLabError(
                    f"visibility.raw_base_rows[{row_index}][{slot}] references "
                    f"segment {item}; valid indices are 0..{row_count - 1}"
                )
            checked.append(item)
        result.append(checked)
    return result


def _validate_inspection_data(document: dict[str, Any], row_count: int) -> None:
    segments = _require_array(document["segments"], "segments")
    if len(segments) != row_count:
        raise TrackLabError(f"segments has {len(segments)} records; expected {row_count}")

    segment_anchor_ids: set[int] = set()
    segment_culls: list[tuple[int, int, int, int, int]] = []
    for index, candidate in enumerate(segments):
        segment = _require_object(candidate, f"segments[{index}]")
        _require_keys(segment, {"index", "raw_be_hex", "decoded"}, f"segments[{index}]")
        if segment["index"] != index:
            raise TrackLabError(f"segments[{index}].index must be {index}")
        raw = _require_raw_hex(segment["raw_be_hex"], 64, f"segments[{index}].raw_be_hex")
        decoded = _require_object(segment["decoded"], f"segments[{index}].decoded")
        _require_keys(
            decoded,
            {"road_dl", "wall_dl", "far_scenery_dl", "cull_anchor"},
            f"segments[{index}].decoded",
        )
        raw_dls = struct.unpack_from(">III", raw, 4)
        for field, raw_value in zip(
            ("road_dl", "wall_dl", "far_scenery_dl"), raw_dls
        ):
            decoded_value = _require_pointer(
                decoded[field], f"segments[{index}].decoded.{field}", nullable=True
            )
            if decoded_value != raw_value:
                raise TrackLabError(
                    f"segments[{index}].decoded.{field} does not match raw_be_hex"
                )
        cull = _require_object(
            decoded["cull_anchor"], f"segments[{index}].decoded.cull_anchor"
        )
        _require_keys(
            cull,
            {"anchor_index", "segment_offset_x", "segment_offset_z", "world_x", "world_z"},
            f"segments[{index}].decoded.cull_anchor",
        )
        raw_offset_x, raw_offset_z = struct.unpack_from(">hh", raw, 0x10)
        raw_anchor = struct.unpack_from(">h", raw, 0x20)[0]
        anchor_index = _require_int(
            cull["anchor_index"],
            f"segments[{index}].decoded.cull_anchor.anchor_index",
            0,
            0x7FFF,
        )
        for field, raw_value in (
            ("segment_offset_x", raw_offset_x),
            ("segment_offset_z", raw_offset_z),
        ):
            value = _require_int(
                cull[field], f"segments[{index}].decoded.cull_anchor.{field}", -0x8000, 0x7FFF
            )
            if value != raw_value:
                raise TrackLabError(
                    f"segments[{index}].decoded.cull_anchor.{field} does not match raw_be_hex"
                )
        world_x = _require_int(
            cull["world_x"],
            f"segments[{index}].decoded.cull_anchor.world_x",
            -0x10000,
            0xFFFE,
        )
        world_z = _require_int(
            cull["world_z"],
            f"segments[{index}].decoded.cull_anchor.world_z",
            -0x10000,
            0xFFFE,
        )
        segment_anchor_ids.add(anchor_index)
        segment_culls.append((anchor_index, raw_offset_x, raw_offset_z, world_x, world_z))

    anchors = _require_array(document["anchors"], "anchors")
    anchor_coordinates: dict[int, tuple[int, int]] = {}
    previous_index = -1
    for position, candidate in enumerate(anchors):
        anchor = _require_object(candidate, f"anchors[{position}]")
        _require_keys(anchor, {"index", "raw_be_hex", "decoded"}, f"anchors[{position}]")
        index = _require_int(anchor["index"], f"anchors[{position}].index", 0, 0x7FFF)
        if index <= previous_index:
            raise TrackLabError("anchors must be unique and sorted by index")
        previous_index = index
        raw = _require_raw_hex(anchor["raw_be_hex"], 16, f"anchors[{position}].raw_be_hex")
        raw_x, raw_z = struct.unpack_from(">hh", raw, 0)
        decoded = _require_object(anchor["decoded"], f"anchors[{position}].decoded")
        _require_keys(decoded, {"x", "z"}, f"anchors[{position}].decoded")
        x = _require_int(decoded["x"], f"anchors[{position}].decoded.x", -0x8000, 0x7FFF)
        z = _require_int(decoded["z"], f"anchors[{position}].decoded.z", -0x8000, 0x7FFF)
        if (x, z) != (raw_x, raw_z):
            raise TrackLabError(f"anchors[{position}].decoded does not match raw_be_hex")
        anchor_coordinates[index] = (x, z)
    if set(anchor_coordinates) != segment_anchor_ids:
        raise TrackLabError("anchors must contain exactly the records referenced by segments")
    for index, (anchor_index, dx, dz, world_x, world_z) in enumerate(segment_culls):
        x, z = anchor_coordinates[anchor_index]
        if (world_x, world_z) != (x + dx, z + dz):
            raise TrackLabError(
                f"segments[{index}].decoded.cull_anchor world coordinates are inconsistent"
            )

    waypoints = _require_object(document["waypoints"], "waypoints")
    _require_keys(waypoints, {"assumption_warning", "records"}, "waypoints")
    warning = waypoints["assumption_warning"]
    if not isinstance(warning, str) or "assumption" not in warning.lower():
        raise TrackLabError("waypoints.assumption_warning must explicitly identify the assumption")
    records = _require_array(waypoints["records"], "waypoints.records")
    if len(records) != row_count:
        raise TrackLabError(
            f"waypoints.records has {len(records)} records; expected {row_count}"
        )
    for index, candidate in enumerate(records):
        record = _require_object(candidate, f"waypoints.records[{index}]")
        _require_keys(
            record, {"index", "raw_be_hex", "decoded"}, f"waypoints.records[{index}]"
        )
        if record["index"] != index:
            raise TrackLabError(f"waypoints.records[{index}].index must be {index}")
        raw = _require_raw_hex(
            record["raw_be_hex"], 16, f"waypoints.records[{index}].raw_be_hex"
        )
        raw_values = struct.unpack(">ffhHI", raw)
        decoded = _require_object(
            record["decoded"], f"waypoints.records[{index}].decoded"
        )
        _require_keys(
            decoded,
            {"plane_a", "plane_b", "progress_or_extent", "unknown_u16", "unknown_u32"},
            f"waypoints.records[{index}].decoded",
        )
        for field, raw_value in zip(
            ("plane_a", "plane_b"), raw_values[:2]
        ):
            value = decoded[field]
            if (
                isinstance(value, bool)
                or not isinstance(value, (int, float))
                or not math.isfinite(float(value))
                or float(value) != raw_value
            ):
                raise TrackLabError(
                    f"waypoints.records[{index}].decoded.{field} does not match raw_be_hex"
                )
        for field, raw_value, low, high in (
            ("progress_or_extent", raw_values[2], -0x8000, 0x7FFF),
            ("unknown_u16", raw_values[3], 0, 0xFFFF),
            ("unknown_u32", raw_values[4], 0, 0xFFFFFFFF),
        ):
            value = _require_int(
                decoded[field], f"waypoints.records[{index}].decoded.{field}", low, high
            )
            if value != raw_value:
                raise TrackLabError(
                    f"waypoints.records[{index}].decoded.{field} does not match raw_be_hex"
                )


def validate_document(
    document: dict[str, Any], *, include_inspection: bool = True
) -> ValidatedDocument:
    """Validate a document and return the deterministic sparse edit set.

    The full document validator checks the evidence-bearing inspection records.
    Patch consumers can set ``include_inspection=False`` because those records
    are not part of the compiled package and may be stripped before compilation.
    """
    document = _require_object(document, "document")
    required_keys = {"format", "version", "target", "visibility"}
    optional_keys = set()
    if include_inspection:
        required_keys.update(
            {"provenance", "capabilities", "segments", "anchors", "waypoints"}
        )
    else:
        optional_keys.update(
            {"provenance", "capabilities", "segments", "anchors", "waypoints"}
        )
    _require_keys(
        document,
        required_keys,
        "document",
        optional=optional_keys,
    )
    if document["format"] != DOCUMENT_FORMAT:
        raise TrackLabError(
            f"format must be {DOCUMENT_FORMAT!r}; got {document['format']!r}"
        )
    if document["version"] != DOCUMENT_VERSION or not _is_int(document["version"]):
        raise TrackLabError(f"version must be integer {DOCUMENT_VERSION}")

    target = _require_object(document["target"], "target")
    _require_keys(target, {"game_id", "rom_xxh3_64", "circuit"}, "target")
    if target["game_id"] != GAME_ID:
        raise TrackLabError(f"target.game_id must be {GAME_ID!r}")
    rom_hash = _require_hex(target["rom_xxh3_64"], _HEX16_RE, "target.rom_xxh3_64")
    if rom_hash != ROM_XXH3_64:
        raise TrackLabError(
            f"target.rom_xxh3_64 is {rom_hash}; this tool supports {ROM_XXH3_64}"
        )
    _require_int(target["circuit"], "target.circuit", 0, 5)

    if include_inspection:
        provenance = _require_object(document["provenance"], "provenance")
        _require_keys(
            provenance,
            {
                "snapshot_format",
                "snapshot_sha256",
                "rdram_sha256",
                "rdram_size",
                "savestate_state",
                "active_context_pointer_address",
                "active_context",
                "context_pointers",
            },
            "provenance",
        )
        if provenance["snapshot_format"] not in {
            "raw-word-swapped-rdram",
            "lmbostat-v1",
        }:
            raise TrackLabError("provenance.snapshot_format is not a supported snapshot type")
        _require_hex(provenance["snapshot_sha256"], _SHA256_RE, "provenance.snapshot_sha256")
        _require_hex(provenance["rdram_sha256"], _SHA256_RE, "provenance.rdram_sha256")
        if provenance["rdram_size"] != RDRAM_SIZE:
            raise TrackLabError(f"provenance.rdram_size must be {RDRAM_SIZE}")
        if provenance["savestate_state"] is not None:
            _require_int(
                provenance["savestate_state"],
                "provenance.savestate_state",
                0,
                0xFFFFFFFF,
            )
        context_pointer_address = _require_pointer(
            provenance["active_context_pointer_address"],
            "provenance.active_context_pointer_address",
        )
        if context_pointer_address != ACTIVE_CONTEXT_PTR:
            raise TrackLabError(
                f"provenance.active_context_pointer_address must be 0x{ACTIVE_CONTEXT_PTR:08x}"
            )
        _require_pointer(provenance["active_context"], "provenance.active_context")
        context_pointers = _require_object(
            provenance["context_pointers"], "provenance.context_pointers"
        )
        _require_keys(
            context_pointers,
            {
                "segments",
                "visibility_base",
                "visibility_end_waypoints",
                "unknown_table",
                "anchors",
            },
            "provenance.context_pointers",
        )
        for field in (
            "segments",
            "visibility_base",
            "visibility_end_waypoints",
            "unknown_table",
            "anchors",
        ):
            _require_pointer(
                context_pointers[field], f"provenance.context_pointers.{field}"
            )

        capabilities = _require_object(document["capabilities"], "capabilities")
        _require_keys(capabilities, {"editable", "inspect_only", "unsupported"}, "capabilities")
        editable = _validate_string_array(capabilities["editable"], "capabilities.editable")
        unknown_editable = set(editable) - set(CAPABILITIES_EDITABLE)
        if unknown_editable:
            raise TrackLabError(
                "capabilities.editable contains unsupported label(s): "
                + ", ".join(sorted(unknown_editable))
            )
        if set(editable) != set(CAPABILITIES_EDITABLE):
            raise TrackLabError("capabilities.editable must declare visibility")
        inspect_only = _validate_string_array(
            capabilities["inspect_only"], "capabilities.inspect_only"
        )
        if set(inspect_only) != set(CAPABILITIES_INSPECT_ONLY):
            raise TrackLabError(
                "capabilities.inspect_only must declare segments, anchors, and waypoints"
            )
        unsupported = _validate_string_array(
            capabilities["unsupported"], "capabilities.unsupported"
        )
        if set(unsupported) != set(CAPABILITIES_UNSUPPORTED):
            raise TrackLabError(
                "capabilities.unsupported must declare geometry, collision, and new_track"
            )

    visibility = _require_object(document["visibility"], "visibility")
    _require_keys(
        visibility,
        {"row_count", "slots_per_row", "base_fnv1a64", "base_rows", "rows"},
        "visibility",
        optional={"raw_base_rows"},
    )
    row_count = _require_int(
        visibility["row_count"], "visibility.row_count", MIN_ROW_COUNT, MAX_ROW_COUNT
    )
    if visibility["slots_per_row"] != SLOTS_PER_ROW:
        raise TrackLabError(f"visibility.slots_per_row must be {SLOTS_PER_ROW}")
    stored_hash_text = _require_hex(
        visibility["base_fnv1a64"], _HEX16_RE, "visibility.base_fnv1a64"
    )
    base_rows = _validate_semantic_rows(
        visibility["base_rows"], row_count, "visibility.base_rows"
    )
    rows = _validate_semantic_rows(visibility["rows"], row_count, "visibility.rows")
    if "raw_base_rows" in visibility:
        raw_base_rows = _validate_raw_rows(visibility["raw_base_rows"], row_count)
    else:
        raw_base_rows = [
            [-1 if value is None else value for value in row] for row in base_rows
        ]

    for row_index in range(row_count):
        for slot in range(SLOTS_PER_ROW):
            semantic_raw = (
                None
                if raw_base_rows[row_index][slot] < 0
                else raw_base_rows[row_index][slot]
            )
            if base_rows[row_index][slot] != semantic_raw:
                raise TrackLabError(
                    f"visibility.base_rows[{row_index}][{slot}] disagrees with "
                    "visibility.raw_base_rows"
                )

    base_hash = fnv1a64_pvs(raw_base_rows)
    stored_hash = int(stored_hash_text, 16)
    if stored_hash != base_hash:
        raise TrackLabError(
            "visibility baseline hash mismatch: document says "
            f"{stored_hash_text}, rows fingerprint as {_fnv_hex(base_hash)}"
        )

    edits: list[VisibilityEdit] = []
    patched_rows = [row.copy() for row in raw_base_rows]
    for row_index in range(row_count):
        for slot in range(SLOTS_PER_ROW):
            before = base_rows[row_index][slot]
            after = rows[row_index][slot]
            if before == after:
                continue
            replacement = -1 if after is None else after
            expected = raw_base_rows[row_index][slot]
            edits.append(
                VisibilityEdit(
                    row=row_index,
                    slot=slot,
                    before=before,
                    after=after,
                    expected_raw=expected,
                    replacement_raw=replacement,
                )
            )
            patched_rows[row_index][slot] = replacement
    patched_hash = fnv1a64_pvs(patched_rows)

    if include_inspection:
        _validate_inspection_data(document, row_count)
    return ValidatedDocument(
        document=document,
        row_count=row_count,
        raw_base_rows=raw_base_rows,
        edits=edits,
        base_hash=base_hash,
        patched_hash=patched_hash,
    )


def validate_patch_document(document: dict[str, Any]) -> ValidatedDocument:
    """Validate only fields consumed by the portable visibility patch."""
    return validate_document(document, include_inspection=False)


def diff_document(document: dict[str, Any]) -> dict[str, Any]:
    validated = validate_patch_document(document)
    return {
        "base_fnv1a64": _fnv_hex(validated.base_hash),
        "patched_fnv1a64": _fnv_hex(validated.patched_hash),
        "edit_count": len(validated.edits),
        "edits": [
            {
                "row": edit.row,
                "slot": edit.slot,
                "before": edit.before,
                "after": edit.after,
                "expected_raw": edit.expected_raw,
                "replacement_raw": edit.replacement_raw,
            }
            for edit in validated.edits
        ],
    }


def compile_document(document: dict[str, Any]) -> bytes:
    """Compile patch-relevant fields to deterministic portable ``.altrk`` bytes."""
    validated = validate_patch_document(document)
    edit_count = len(validated.edits)
    payload_size = edit_count * PATCH_EDIT.size
    file_size = PATCH_HEADER_SIZE + payload_size
    target = validated.document["target"]
    header = PATCH_HEADER.pack(
        PATCH_MAGIC,
        PATCH_VERSION,
        PATCH_HEADER_SIZE,
        file_size,
        int(target["rom_xxh3_64"], 16),
        PATCH_FLAGS_PVS_CORRECTIONS,
        target["circuit"],
        SLOTS_PER_ROW,
        validated.row_count,
        edit_count,
        payload_size,
        validated.base_hash,
        validated.patched_hash,
        0,
    )
    payload = b"".join(
        PATCH_EDIT.pack(
            edit.row,
            edit.slot,
            0,
            edit.expected_raw,
            edit.replacement_raw,
        )
        for edit in validated.edits
    )
    return header + payload


def _canonical_json(value: Any) -> str:
    try:
        return json.dumps(
            value,
            ensure_ascii=False,
            allow_nan=False,
            indent=2,
            sort_keys=True,
        ) + "\n"
    except (TypeError, ValueError) as exc:
        raise TrackLabError(f"cannot encode canonical JSON: {exc}") from exc


def _atomic_write(path: str | os.PathLike[str], data: bytes) -> None:
    output = Path(path)
    temporary = output.with_name(output.name + ".tmp")
    try:
        temporary.write_bytes(data)
        os.replace(temporary, output)
    except OSError as exc:
        raise TrackLabError(f"cannot write {output}: {exc}") from exc


def write_document(path: str | os.PathLike[str], document: dict[str, Any]) -> None:
    _atomic_write(path, _canonical_json(document).encode("utf-8"))


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Extract, validate, diff, and compile safe track visibility edits."
    )
    commands = parser.add_subparsers(dest="command", required=True)

    extract = commands.add_parser("extract", help="extract a race snapshot to canonical JSON")
    extract.add_argument("snapshot", type=Path)
    extract.add_argument("json", type=Path)

    validate = commands.add_parser("validate", help="validate an al-track-document JSON file")
    validate.add_argument("json", type=Path)

    diff = commands.add_parser("diff", help="print the sparse visibility edit set as JSON")
    diff.add_argument("json", type=Path)

    compile_command = commands.add_parser(
        "compile", help="compile guarded visibility edits to a portable .altrk file"
    )
    compile_command.add_argument("json", type=Path)
    compile_command.add_argument("altrk", type=Path)
    return parser


def _make_console_output_total() -> None:
    """Keep diagnostics printable on legacy Windows code pages.

    Track/package paths are Unicode.  Python may still attach stdout or stderr
    to a cp1252 console, where an otherwise successful command would raise
    UnicodeEncodeError while reporting its result.  Backslash replacement keeps
    the command's real success/failure authoritative without changing the
    caller's chosen stream encoding.
    """
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            try:
                reconfigure(errors="backslashreplace")
            except (OSError, ValueError):
                pass


def main(argv: list[str] | None = None) -> int:
    _make_console_output_total()
    args = _build_parser().parse_args(argv)
    try:
        if args.command == "extract":
            document = extract_document(args.snapshot)
            write_document(args.json, document)
            print(
                f"extracted circuit {document['target']['circuit']} with "
                f"{document['visibility']['row_count']} visibility rows -> {args.json}"
            )
        elif args.command == "validate":
            validated = validate_document(load_document(args.json))
            print(
                f"valid: circuit {validated.document['target']['circuit']}, "
                f"{validated.row_count} rows, {len(validated.edits)} edit(s), "
                f"base {_fnv_hex(validated.base_hash)}"
            )
        elif args.command == "diff":
            sys.stdout.write(_canonical_json(diff_document(load_document(args.json))))
        elif args.command == "compile":
            document = load_document(args.json)
            patch = compile_document(document)
            _atomic_write(args.altrk, patch)
            edit_count = (len(patch) - PATCH_HEADER_SIZE) // PATCH_EDIT.size
            print(f"compiled {edit_count} guarded edit(s), {len(patch)} bytes -> {args.altrk}")
        else:  # pragma: no cover - argparse makes this unreachable.
            raise TrackLabError(f"unknown command {args.command!r}")
    except TrackLabError as exc:
        print(f"track_lab: error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
