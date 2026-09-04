#!/usr/bin/env python3
"""Generate an RT64 texture-pack manifest (rt64.json) from replacement images.

RT64 loads a texture pack by reading rt64.json from the pack directory (or .rtz) -- it
does NOT auto-scan for hash-named files. RT64's own `texture_hasher` only *upgrades* an
existing rt64.json; it will not create one from scratch. This tool fills that gap: point
it at a directory of replacement images named by the RT64 texture hash
(`<16-hex-hash>.png` or `.dds`, the same hash RT64 writes as the dump filename) and it
writes a valid rt64.json listing each.

    python tools/make_pack.py <pack_dir> [--auto-path rt64|rice]
                                         [--shift half|none] [--operation stream|preload]

Then either point the port at <pack_dir> directly (graphics.json `texture_pack` /
LAMBO_TEXTURE_PACK), or zip it into a shippable .rtz with RT64's texture_packer.

Self-contained: standard library only.
"""

import argparse
import json
import re
from pathlib import Path

HASH_RE = re.compile(r"^([0-9a-fA-F]{16})$")


def write_manifest(pack_dir, auto_path="rt64", shift="half", operation="stream"):
    """Write and return an RT64 replacement database for hash-named images."""
    pack_dir = Path(pack_dir)
    textures = []
    for p in sorted(pack_dir.iterdir()):
        if p.suffix.lower() not in (".png", ".dds"):
            continue
        m = HASH_RE.match(p.stem)
        if not m:
            print(f"skip {p.name}: stem is not a 16-hex RT64 hash")
            continue
        h = m.group(1).lower()
        textures.append({
            "path": p.name,
            "hashes": {"rt64": h, "rice": ""},
            "operation": "auto",
            "shift": "auto",
        })

    if not textures:
        raise ValueError(f"No <hash>.png/.dds files in {pack_dir}")

    db = {
        "configuration": {
            "autoPath": auto_path,
            "defaultOperation": operation,
            "defaultShift": shift,
            "hashVersion": 5,  # TMEMHasher::CurrentHashVersion
        },
        "textures": textures,
    }
    out = pack_dir / "rt64.json"
    out.write_text(json.dumps(db, indent=2))
    print(f"wrote {out} with {len(textures)} texture(s)")
    return db


def main():
    ap = argparse.ArgumentParser(description="Write rt64.json from hash-named replacement images.")
    ap.add_argument("pack_dir", type=Path)
    ap.add_argument("--auto-path", choices=["rt64", "rice"], default="rt64",
                    help="which hash names the files on disk (default rt64)")
    ap.add_argument("--shift", choices=["half", "none", "auto"], default="half",
                    help="default texel shift; 'half' suits modern-tool exports (default)")
    ap.add_argument("--operation", choices=["stream", "preload", "auto"], default="stream",
                    help="default load operation (default stream)")
    args = ap.parse_args()

    try:
        write_manifest(args.pack_dir, args.auto_path, args.shift, args.operation)
    except ValueError as error:
        print(error)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
