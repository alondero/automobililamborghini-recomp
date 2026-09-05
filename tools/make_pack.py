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


HASH_RE = re.compile(r"^([0-9a-fA-F]{16})$")
VALID_OPERATIONS = {"stream", "preload", "stall"}
VALID_SHIFTS = {"half", "none"}
DEFAULT_OPERATION = "stream"
# Keep the generic tool's established default independent of any pack.
DEFAULT_SHIFT = "half"


def main() -> int:
    ap = argparse.ArgumentParser(description="Write rt64.json from hash-named replacement images.")
    ap.add_argument("source_dir", type=Path,
                    help="directory containing hash-named replacement images")
    ap.add_argument("--manifest", type=Path,
                    help="manifest path (default: <source_dir>/rt64.json)")
    ap.add_argument("--auto-path", choices=["rt64", "rice"], default="rt64",
                    help="which hash names the files on disk (default rt64)")
    ap.add_argument("--shift", choices=sorted(VALID_SHIFTS), default=None,
                    help="default texel shift (default half)")
    ap.add_argument("--operation", choices=sorted(VALID_OPERATIONS), default=None,
                    help="default load operation (default stream)")
    args = ap.parse_args()

    operation = args.operation or DEFAULT_OPERATION
    shift = args.shift or DEFAULT_SHIFT

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
