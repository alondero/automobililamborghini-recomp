#!/usr/bin/env python3
"""Validate texture-pack metadata and an RT64 replacement directory.

This validator deliberately rejects ROMs and raw RT64 captures.  It is used
before conversion (to validate the editable source tree) and after conversion
(to validate the generated loose pack referenced by rt64.json).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Iterable, Optional


HASH_RE = re.compile(r"^[0-9a-fA-F]{16}$")
SHA1_RE = re.compile(r"^[0-9a-fA-F]{40}$")
SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")
COMMIT_RE = re.compile(r"^[0-9a-fA-F]{7,64}$")
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
IMAGE_SUFFIXES = {".png", ".dds"}
BANNED_SUFFIXES = {
    ".z64", ".n64", ".v64", ".rom", ".tmem", ".rdram", ".lstate",
    ".lstate.tmp", ".palette.rdram", ".bin",
}
PENDING_LICENSES = {"", "todo", "tbd", "pending", "unlicensed-pending"}
PENDING_CREDITS_MARKERS = (
    "not yet licensed",
    "not licensed",
    "unlicensed",
    "release status: pending",
)
VALID_OPERATIONS = {"stream", "preload", "stall"}
VALID_SHIFTS = {"half", "none"}


def _load_json(path: Path, errors: list[str]) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        errors.append(f"cannot read {path}: {exc}")
    except json.JSONDecodeError as exc:
        errors.append(f"invalid JSON in {path}: {exc}")
    return None


def _is_safe_relative(value: str) -> bool:
    value = value.replace("\\", "/")
    path = Path(value)
    return not path.is_absolute() and ".." not in path.parts and value not in {"", "."}


def _iter_files(root: Path) -> Iterable[Path]:
    if not root.is_dir():
        return ()
    return (path for path in root.rglob("*") if path.is_file())


def validate(
    pack_json_path: Path,
    source_dir: Path,
    rt64_json_path: Optional[Path] = None,
    require_license: bool = False,
    allow_generated_cache: bool = False,
) -> list[str]:
    errors: list[str] = []
    metadata = _load_json(pack_json_path, errors)
    if not isinstance(metadata, dict):
        return errors or [f"{pack_json_path} must contain a JSON object"]

    required = {
        "schema_version", "pack_id", "pack_version", "game_id", "region",
        "base_rom_sha1", "min_port_version", "rt64_configuration_version",
        "rt64_hash_version", "format", "variant", "license", "credits",
        "rights_confirmed",
    }
    missing = sorted(required - metadata.keys())
    if missing:
        errors.append("pack.json is missing: " + ", ".join(missing))

    if metadata.get("schema_version") != 1:
        errors.append("pack.json schema_version must be 1")
    for key in ("pack_id", "game_id", "region", "variant", "credits"):
        if not isinstance(metadata.get(key), str) or not metadata[key].strip():
            errors.append(f"pack.json {key} must be a non-empty string")
    pack_id = metadata.get("pack_id")
    if isinstance(pack_id, str) and not ID_RE.fullmatch(pack_id):
        errors.append("pack.json pack_id may contain only letters, digits, '.', '_' and '-'")
    for key in ("pack_version", "min_port_version"):
        value = metadata.get(key)
        if not isinstance(value, str) or not value.strip():
            errors.append(f"pack.json {key} must be a non-empty string")
        elif key == "pack_version" and not SEMVER_RE.fullmatch(value):
            errors.append(f"pack.json pack_version is not SemVer: {value!r}")
    sha1 = metadata.get("base_rom_sha1")
    if not isinstance(sha1, str) or not SHA1_RE.fullmatch(sha1):
        errors.append("pack.json base_rom_sha1 must be a 40-hex-character SHA-1")
    for key in ("rt64_configuration_version", "rt64_hash_version"):
        if not isinstance(metadata.get(key), int) or metadata[key] <= 0:
            errors.append(f"pack.json {key} must be a positive integer")
    if metadata.get("format") != "rtz":
        errors.append("pack.json format must be 'rtz'")

    source_commit = metadata.get("source_commit")
    if source_commit is not None and (
        not isinstance(source_commit, str) or not COMMIT_RE.fullmatch(source_commit)
    ):
        errors.append("pack.json source_commit must be null or a Git commit")
    port_commit = metadata.get("port_commit")
    if port_commit is not None and (
        not isinstance(port_commit, str) or not COMMIT_RE.fullmatch(port_commit)
    ):
        errors.append("pack.json port_commit must be null or a Git commit")

    license_value = metadata.get("license")
    if require_license:
        if not isinstance(license_value, str) or license_value.strip().lower() in PENDING_LICENSES:
            errors.append("pack.json license must be selected before a public release")
        if metadata.get("rights_confirmed") is not True:
            errors.append("pack.json rights_confirmed must be true before a public release")
    elif license_value is not None and not isinstance(license_value, str):
        errors.append("pack.json license must be a string or null")
    elif "rights_confirmed" in metadata and not isinstance(metadata["rights_confirmed"], bool):
        errors.append("pack.json rights_confirmed must be a boolean")

    credits = metadata.get("credits")
    if isinstance(credits, str) and _is_safe_relative(credits):
        credits_path = pack_json_path.parent / credits
        if not credits_path.is_file():
            errors.append(f"credits file does not exist: {credits}")
        elif require_license:
            credits_text = credits_path.read_text(encoding="utf-8").lower()
            for marker in PENDING_CREDITS_MARKERS:
                if marker in credits_text:
                    errors.append(
                        f"credits file still contains pending-rights marker: {marker!r}"
                    )
    elif isinstance(credits, str):
        errors.append("pack.json credits must be a safe relative path")

    if not source_dir.is_dir():
        errors.append(f"source directory does not exist: {source_dir}")
        return errors

    images: dict[str, Path] = {}
    for path in _iter_files(source_dir):
        suffix = path.suffix.lower()
        name_lower = path.name.lower()
        generated_cache = allow_generated_cache and name_lower == "rt64-low-mip-cache.bin"
        if not generated_cache and (suffix in BANNED_SUFFIXES or any(name_lower.endswith(s) for s in BANNED_SUFFIXES)):
            errors.append(f"forbidden game/capture file in source tree: {path.relative_to(source_dir)}")
        if suffix not in IMAGE_SUFFIXES:
            continue
        if not HASH_RE.fullmatch(path.stem):
            errors.append(f"image is not named with a 16-hex RT64 hash: {path.relative_to(source_dir)}")
            continue
        hash_name = path.stem.lower()
        if hash_name in images:
            errors.append(f"duplicate RT64 hash in source tree: {hash_name}")
        images[hash_name] = path
    if not images:
        errors.append(f"source directory contains no hash-named PNG/DDS files: {source_dir}")

    if rt64_json_path is None:
        return errors
    database = _load_json(rt64_json_path, errors)
    if not isinstance(database, dict):
        return errors or [f"{rt64_json_path} must contain a JSON object"]
    config = database.get("configuration")
    if not isinstance(config, dict):
        errors.append("rt64.json configuration must be an object")
    else:
        if config.get("autoPath") not in {"rt64", "rice"}:
            errors.append("rt64.json configuration autoPath must be 'rt64' or 'rice'")
        if config.get("configurationVersion") != metadata.get("rt64_configuration_version"):
            errors.append("rt64.json configurationVersion does not match pack.json")
        if config.get("hashVersion") != metadata.get("rt64_hash_version"):
            errors.append("rt64.json hashVersion does not match pack.json")
        if config.get("defaultOperation") not in VALID_OPERATIONS:
            errors.append("rt64.json configuration defaultOperation is invalid")
        if config.get("defaultShift") not in VALID_SHIFTS:
            errors.append("rt64.json configuration defaultShift is invalid")
    entries = database.get("textures")
    if not isinstance(entries, list) or not entries:
        errors.append("rt64.json textures must be a non-empty array")
        return errors
    seen_paths: set[str] = set()
    manifest_hashes: set[str] = set()
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            errors.append(f"rt64.json textures[{index}] must be an object")
            continue
        path_value = entry.get("path")
        if not isinstance(path_value, str) or not _is_safe_relative(path_value):
            errors.append(f"rt64.json textures[{index}] has an unsafe path")
        else:
            normal_path = path_value.replace("\\", "/")
            if normal_path in seen_paths:
                errors.append(f"duplicate rt64.json path: {path_value}")
            seen_paths.add(normal_path)
            if not (rt64_json_path.parent / Path(normal_path)).is_file():
                errors.append(f"rt64.json path does not exist: {path_value}")
        hashes = entry.get("hashes")
        if not isinstance(hashes, dict):
            errors.append(f"rt64.json textures[{index}] hashes must be an object")
        else:
            rt64_hash = hashes.get("rt64")
            if not isinstance(rt64_hash, str) or not HASH_RE.fullmatch(rt64_hash):
                errors.append(f"rt64.json textures[{index}] has an invalid RT64 hash")
            else:
                manifest_hashes.add(rt64_hash.lower())
        operation = entry.get("operation")
        if operation is not None and operation not in VALID_OPERATIONS:
            errors.append(f"rt64.json textures[{index}] has an invalid operation")
        shift = entry.get("shift")
        if shift is not None and shift not in VALID_SHIFTS:
            errors.append(f"rt64.json textures[{index}] has an invalid shift")

    source_hashes = set(images)
    missing = sorted(source_hashes - manifest_hashes)
    extra = sorted(manifest_hashes - source_hashes)
    if missing:
        errors.append("rt64.json is missing source hash(es): " + ", ".join(missing))
    if extra:
        errors.append("rt64.json references hash(es) without source images: " + ", ".join(extra))

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate an Automobili Lamborghini RT64 texture pack")
    parser.add_argument("--pack-json", type=Path, default=Path("pack.json"))
    parser.add_argument("--source-dir", type=Path, default=Path("textures"))
    parser.add_argument("--rt64-json", type=Path)
    parser.add_argument("--for-release", action="store_true",
                        help="also require a resolved artwork license")
    parser.add_argument("--allow-generated-cache", action="store_true",
                        help="allow RT64's generated rt64-low-mip-cache.bin")
    args = parser.parse_args()
    errors = validate(
        args.pack_json,
        args.source_dir,
        args.rt64_json,
        args.for_release,
        args.allow_generated_cache,
    )
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"validated pack metadata and {sum(1 for p in _iter_files(args.source_dir) if p.suffix.lower() in IMAGE_SUFFIXES)} source image(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
