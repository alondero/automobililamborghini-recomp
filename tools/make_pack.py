#!/usr/bin/env python3
"""Generate an RT64 texture-pack manifest (rt64.json) from replacement images.

RT64 loads a texture pack by reading rt64.json from the pack directory (or .rtz); it
does not auto-scan for hash-named files. This tool fills that gap: point it at a
directory of replacement images named by the RT64 texture hash
(`<16-hex-hash>.png` or `.dds`) and it writes a valid rt64.json listing each.

Self-contained: standard library only.
"""

import argparse
import json
import re
from pathlib import Path
from typing import Any, Dict, Optional, Tuple


HASH_RE = re.compile(r"^([0-9a-fA-F]{16})$")
VALID_OPERATIONS = {"stream", "preload", "stall"}
VALID_SHIFTS = {"half", "none"}
DEFAULT_OPERATION = "stream"
# Keep the generic tool's established default independent of any pack's policy.
DEFAULT_SHIFT = "half"


def _load_policy(ap: argparse.ArgumentParser, path: Optional[Path]) -> Dict[str, Any]:
    if path is None:
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        ap.error(f"cannot read policy {path}: {exc}")
    if not isinstance(value, dict):
        ap.error(f"policy must be a JSON object: {path}")
    return value


def _policy_overrides(
    ap: argparse.ArgumentParser, policy: Dict[str, Any]
) -> Tuple[Dict[str, str], Dict[str, str]]:
    operations: Dict[str, str] = {}
    shifts: Dict[str, str] = {}
    preload = policy.get("preload", [])
    shift_none = policy.get("shift_none", [])
    if not isinstance(preload, list) or not isinstance(shift_none, list):
        ap.error("policy 'preload' and 'shift_none' must be arrays")
    for value in preload:
        if not isinstance(value, str) or not HASH_RE.fullmatch(value):
            ap.error("policy preload entries must be 16-hex hashes")
        operations[str(value).lower()] = "preload"
    for value in shift_none:
        if not isinstance(value, str) or not HASH_RE.fullmatch(value):
            ap.error("policy shift_none entries must be 16-hex hashes")
        shifts[str(value).lower()] = "none"
    protected = policy.get("protected", [])
    if not isinstance(protected, list):
        ap.error("policy 'protected' must be an array")
    for entry in protected:
        if not isinstance(entry, dict):
            ap.error("each policy 'protected' entry must be an object")
        texture_hash = entry.get("hash")
        if not isinstance(texture_hash, str) or not HASH_RE.fullmatch(texture_hash):
            ap.error("each policy 'protected' entry needs a 16-hex hash")
        texture_hash = texture_hash.lower()
        operation = entry.get("operation")
        if operation is not None:
            if not isinstance(operation, str) or operation not in VALID_OPERATIONS:
                ap.error(f"unsupported policy operation: {operation!r}")
            operations[texture_hash] = operation
        shift = entry.get("shift")
        if shift is not None:
            if not isinstance(shift, str) or shift not in VALID_SHIFTS:
                ap.error(f"unsupported policy shift: {shift!r}")
            shifts[texture_hash] = shift
    return operations, shifts


def main() -> int:
    ap = argparse.ArgumentParser(description="Write rt64.json from hash-named replacement images.")
    ap.add_argument("source_dir", type=Path,
                    help="directory containing hash-named replacement images")
    ap.add_argument("--manifest", type=Path,
                    help="manifest path (default: <source_dir>/rt64.json)")
    ap.add_argument("--auto-path", choices=["rt64", "rice"], default="rt64",
                    help="which hash names the files on disk (default rt64)")
    ap.add_argument("--shift", choices=sorted(VALID_SHIFTS), default=None,
                    help="default texel shift (policy value or half)")
    ap.add_argument("--operation", choices=sorted(VALID_OPERATIONS), default=None,
                    help="default load operation (policy value or stream)")
    ap.add_argument("--policy", type=Path,
                    help="optional JSON policy with protected texture entries")
    args = ap.parse_args()

    policy = _load_policy(ap, args.policy)
    operations, shifts = _policy_overrides(ap, policy)
    operation = args.operation or policy.get("default_operation", DEFAULT_OPERATION)
    shift = args.shift or policy.get("default_shift", DEFAULT_SHIFT)
    if not isinstance(operation, str) or operation not in VALID_OPERATIONS:
        ap.error(f"policy default_operation is invalid: {operation!r}")
    if not isinstance(shift, str) or shift not in VALID_SHIFTS:
        ap.error(f"policy default_shift is invalid: {shift!r}")

    source_dir = args.source_dir.resolve()
    if not source_dir.is_dir():
        ap.error(f"source directory does not exist: {source_dir}")
    out = (args.manifest or (source_dir / "rt64.json")).resolve()
    try:
        out.parent.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        ap.error(f"cannot create manifest directory {out.parent}: {exc}")

    textures = []
    seen_hashes = set()
    for path in sorted(source_dir.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in (".png", ".dds"):
            continue
        match = HASH_RE.match(path.stem)
        if not match:
            print(f"skip {path.name}: stem is not a 16-hex RT64 hash")
            continue
        texture_hash = match.group(1).lower()
        if texture_hash in seen_hashes:
            ap.error(f"duplicate RT64 hash in source directory: {texture_hash}")
        seen_hashes.add(texture_hash)
        try:
            relative_path = path.relative_to(out.parent).as_posix()
        except ValueError:
            ap.error(f"manifest must be in or above source directory: {out}")
        texture = {
            "path": relative_path,
            "hashes": {"rt64": texture_hash, "rice": ""},
        }
        # Ordinary entries inherit valid defaults; protected entries carry their
        # explicit policy overrides for behavior that must not drift.
        if texture_hash in operations:
            texture["operation"] = operations[texture_hash]
        if texture_hash in shifts:
            texture["shift"] = shifts[texture_hash]
        textures.append(texture)

    if not textures:
        print(f"No <hash>.png/.dds files in {source_dir}")
        return 1

    database = {
        "configuration": {
            "autoPath": args.auto_path,
            "configurationVersion": 3,
            "defaultOperation": operation,
            "defaultShift": shift,
            "hashVersion": 5,
        },
        "textures": textures,
    }
    out.write_text(json.dumps(database, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out} with {len(textures)} texture(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
