#!/usr/bin/env python3
"""Generate an RT64 v5 replacement manifest from hash-named images."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


HASH_RE = re.compile(r"^([0-9a-fA-F]{16})$")

# This file intentionally remains standalone: bootstrap-companion.ps1 copies it
# into the independent pack repository, where the main port checkout is absent.


def main() -> int:
    parser = argparse.ArgumentParser(description="Write rt64.json from hash-named replacement images.")
    parser.add_argument("source_dir", type=Path,
                        help="directory containing hash-named replacement images")
    parser.add_argument("--manifest", type=Path,
                        help="manifest path (default: <source_dir>/rt64.json)")
    parser.add_argument("--auto-path", choices=["rt64", "rice"], default="rt64")
    parser.add_argument("--shift", choices=["half", "none"], default="none")
    parser.add_argument("--operation", choices=["stream", "preload", "stall"], default="stream")
    parser.add_argument("--policy", type=Path,
                        help="optional JSON policy with protected texture entries")
    args = parser.parse_args()

    policy = {}
    if args.policy:
        policy = json.loads(args.policy.read_text(encoding="utf-8"))
        if not isinstance(policy, dict):
            parser.error(f"policy must be a JSON object: {args.policy}")
    preload = {str(value).lower() for value in policy.get("preload", [])}
    shift_none = {str(value).lower() for value in policy.get("shift_none", [])}
    for entry in policy.get("protected", []):
        if not isinstance(entry, dict):
            continue
        texture_hash = entry.get("hash")
        if not isinstance(texture_hash, str):
            continue
        texture_hash = texture_hash.lower()
        if entry.get("operation") == "preload":
            preload.add(texture_hash)
        if entry.get("shift") == "none":
            shift_none.add(texture_hash)

    source_dir = args.source_dir.resolve()
    if not source_dir.is_dir():
        parser.error(f"source directory does not exist: {source_dir}")
    output = (args.manifest or (source_dir / "rt64.json")).resolve()
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        parser.error(f"cannot create manifest directory {output.parent}: {exc}")

    textures = []
    for image in sorted(source_dir.rglob("*")):
        if not image.is_file():
            continue
        if image.suffix.lower() not in (".png", ".dds"):
            continue
        match = HASH_RE.match(image.stem)
        if not match:
            print(f"skip {image.name}: stem is not a 16-hex RT64 hash")
            continue
        texture_hash = match.group(1).lower()
        try:
            relative_path = image.relative_to(output.parent).as_posix()
        except ValueError:
            parser.error(f"manifest must be in or above source directory: {output}")
        texture = {
            "path": relative_path,
            "hashes": {"rt64": texture_hash, "rice": ""},
        }
        # Ordinary entries inherit the valid stream/shift defaults. RT64 does not
        # accept the tempting but invalid per-texture values "auto".
        if texture_hash in preload:
            texture["operation"] = "preload"
        if texture_hash in shift_none:
            texture["shift"] = "none"
        textures.append(texture)

    if not textures:
        print(f"No <hash>.png/.dds files in {source_dir}")
        return 1

    database = {
        "configuration": {
            "autoPath": args.auto_path,
            "configurationVersion": 3,
            "defaultOperation": args.operation,
            "defaultShift": args.shift,
            "hashVersion": 5,
        },
        "textures": textures,
    }
    output.write_text(json.dumps(database, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {output} with {len(textures)} texture(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
