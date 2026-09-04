#!/usr/bin/env python3
"""Generate an RT64 texture-pack manifest (rt64.json) from replacement images.

RT64 loads a texture pack by reading rt64.json from the pack directory (or .rtz) -- it
does NOT auto-scan for hash-named files. RT64's own `texture_hasher` only *upgrades* an
existing rt64.json; it will not create one from scratch. This tool fills that gap: point
it at a directory of replacement images named by the RT64 texture hash
(`<16-hex-hash>.png` or `.dds`, the same hash RT64 writes as the dump filename) and it
writes a valid rt64.json listing each.

    python tools/make_pack.py <source_dir> [--manifest rt64.json]
                                         [--auto-path rt64|rice]
                                         [--shift half|none] [--operation stream|preload|stall]
                                         [--policy texture-policy.json]

Then either point the port at the manifest's containing directory (graphics.json
`texture_pack` / `LAMBO_TEXTURE_PACK`), or zip it into a shippable .rtz with
RT64's texture_packer.

Self-contained: standard library only.
"""

import argparse
import json
import re
from pathlib import Path

HASH_RE = re.compile(r"^([0-9a-fA-F]{16})$")

# The companion seed carries a standalone copy of this generator so its GitHub
# workflow does not depend on the port checkout.


def main():
    ap = argparse.ArgumentParser(description="Write rt64.json from hash-named replacement images.")
    ap.add_argument("source_dir", type=Path,
                    help="directory containing hash-named replacement images")
    ap.add_argument("--manifest", type=Path,
                    help="manifest path (default: <source_dir>/rt64.json)")
    ap.add_argument("--auto-path", choices=["rt64", "rice"], default="rt64",
                    help="which hash names the files on disk (default rt64)")
    ap.add_argument("--shift", choices=["half", "none"], default="half",
                    help="default texel shift; 'half' suits modern-tool exports (default)")
    ap.add_argument("--operation", choices=["stream", "preload", "stall"], default="stream",
                    help="default load operation (default stream)")
    ap.add_argument("--policy", type=Path,
                    help="optional JSON policy with protected texture entries")
    args = ap.parse_args()

    policy = {}
    if args.policy:
        try:
            policy = json.loads(args.policy.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            ap.error(f"cannot read policy {args.policy}: {exc}")
        if not isinstance(policy, dict):
            ap.error(f"policy must be a JSON object: {args.policy}")
    preload = {str(h).lower() for h in policy.get("preload", [])}
    shift_none = {str(h).lower() for h in policy.get("shift_none", [])}
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
        ap.error(f"source directory does not exist: {source_dir}")
    out = (args.manifest or (source_dir / "rt64.json")).resolve()
    try:
        out.parent.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        ap.error(f"cannot create manifest directory {out.parent}: {exc}")

    textures = []
    for p in sorted(source_dir.rglob("*")):
        if not p.is_file():
            continue
        if p.suffix.lower() not in (".png", ".dds"):
            continue
        m = HASH_RE.match(p.stem)
        if not m:
            print(f"skip {p.name}: stem is not a 16-hex RT64 hash")
            continue
        h = m.group(1).lower()
        try:
            relative_path = p.relative_to(out.parent).as_posix()
        except ValueError:
            ap.error(f"manifest must be in or above source directory: {out}")
        texture = {
            "path": relative_path,
            "hashes": {"rt64": h, "rice": ""},
        }
        # Omit ordinary entries so RT64 inherits the valid stream/shift defaults;
        # "auto" is not a valid per-texture operation or shift value.
        if h in preload:
            texture["operation"] = "preload"
        if h in shift_none:
            texture["shift"] = "none"
        textures.append(texture)

    if not textures:
        print(f"No <hash>.png/.dds files in {source_dir}")
        return 1

    db = {
        "configuration": {
            "autoPath": args.auto_path,
            "configurationVersion": 3,
            "defaultOperation": args.operation,
            "defaultShift": args.shift,
            "hashVersion": 5,  # TMEMHasher::CurrentHashVersion
        },
        "textures": textures,
    }
    out.write_text(json.dumps(db, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out} with {len(textures)} texture(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
