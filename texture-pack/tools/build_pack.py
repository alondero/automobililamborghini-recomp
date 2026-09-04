#!/usr/bin/env python3
"""Build a distributable RT64 .rtz pack from editable source images.

PNG sources are converted with Microsoft's texconv.  The RT64 texture packer
then creates the low-mipmap cache and final archive.  All output is generated
under dist/ and can be deleted/rebuilt at any time.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Optional


HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
from validate_pack import validate  # noqa: E402


def _run(command: list[str]) -> None:
    if len(command) > 12:
        shown = " ".join(command[:5]) + f" ... ({len(command) - 5} more args)"
    else:
        shown = " ".join(command)
    print("+", shown)
    result = subprocess.run(command, check=False)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {command[0]}")


def _git_commit() -> Optional[str]:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=REPO,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode == 0:
        value = result.stdout.strip()
        if value:
            return value
    return None


def _load_policy(path: Optional[Path]) -> dict[str, Any]:
    if path is None or not path.is_file():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"cannot read policy {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise RuntimeError(f"policy must be a JSON object: {path}")
    return value


def _policy_texture_sets(policy: dict[str, Any]) -> tuple[set[str], set[str]]:
    """Return (lossless, single-level) hashes from the canonical policy.

    The legacy top-level arrays remain accepted so older companion checkouts can
    still be rebuilt, while new policy files keep all per-texture decisions in
    one ``protected`` entry instead of duplicating three hash lists.
    """
    lossless = {str(value).lower() for value in policy.get("lossless", [])}
    single_level = {str(value).lower() for value in policy.get("single_level", [])}
    protected = policy.get("protected", [])
    if not isinstance(protected, list):
        raise RuntimeError("texture policy 'protected' must be an array")
    for entry in protected:
        if not isinstance(entry, dict):
            raise RuntimeError("each protected texture policy entry must be an object")
        texture_hash = entry.get("hash")
        if not isinstance(texture_hash, str):
            raise RuntimeError("each protected texture policy entry needs a string hash")
        texture_hash = texture_hash.lower()
        texture_format = entry.get("format")
        if texture_format == "R8G8B8A8_UNORM":
            lossless.add(texture_hash)
        elif texture_format not in (None, "BC7_UNORM"):
            raise RuntimeError(f"unsupported texture format in policy: {texture_format!r}")
        if entry.get("mipmaps") is False:
            single_level.add(texture_hash)
    return lossless, single_level


def _safe_name(metadata: dict[str, Any]) -> str:
    version = str(metadata["pack_version"]).replace("+", "_")
    return f"{metadata['pack_id']}-{metadata['variant']}-v{version}"


def _copy_metadata(pack_json: Path, loose: Path, metadata: dict[str, Any]) -> None:
    generated = dict(metadata)
    if not generated.get("source_commit"):
        commit = _git_commit()
        if commit:
            generated["source_commit"] = commit
    (loose / "pack.json").write_text(
        json.dumps(generated, indent=2) + "\n", encoding="utf-8"
    )
    credits = metadata.get("credits")
    if isinstance(credits, str):
        source_credits = pack_json.parent / credits
        if source_credits.is_file():
            destination = loose / Path(credits).name
            shutil.copy2(source_credits, destination)
    for extra in ("NOTICE.md", "PROVENANCE.md", "LICENSE", "LICENSE.md"):
        source_extra = pack_json.parent / extra
        if source_extra.is_file():
            shutil.copy2(source_extra, loose / extra)


def _argument_chunks(paths: list[Path], fixed: list[str], max_chars: int = 12000) -> list[list[Path]]:
    """Split texconv inputs below Windows' CreateProcess command-line limit."""
    chunks: list[list[Path]] = []
    current: list[Path] = []
    current_length = sum(len(value) + 1 for value in fixed)
    for path in paths:
        path_length = len(str(path)) + 1
        if current and current_length + path_length > max_chars:
            chunks.append(current)
            current = []
            current_length = sum(len(value) + 1 for value in fixed)
        current.append(path)
        current_length += path_length
    if current:
        chunks.append(current)
    return chunks


def _convert_sources(
    source_dir: Path,
    loose: Path,
    texconv: Path,
    lossless: set[str],
    single_level: set[str],
) -> int:
    images = sorted(
        path for path in source_dir.rglob("*")
        if path.is_file() and path.suffix.lower() in {".png", ".dds"}
    )
    png_groups: dict[tuple[str, str], list[Path]] = {}
    converted = 0
    for image in images:
        target = loose / f"{image.stem}.dds"
        if image.suffix.lower() == ".dds":
            shutil.copy2(image, target)
            converted += 1
            continue
        texture_hash = image.stem.lower()
        fmt = "R8G8B8A8_UNORM" if texture_hash in lossless else "BC7_UNORM"
        mip_levels = "1" if texture_hash in single_level else "0"
        png_groups.setdefault((fmt, mip_levels), []).append(image)

    for (fmt, mip_levels), group in png_groups.items():
        if not group:
            continue
        fixed = [str(texconv), "-nologo", "-y", "-m", mip_levels, "-f", fmt, "-o", str(loose)]
        for chunk in _argument_chunks(group, fixed):
            _run([*fixed, *(str(path) for path in chunk)])
        for image in group:
            generated = next(
                (
                    path for path in loose.iterdir()
                    if path.is_file()
                    and path.stem.lower() == image.stem.lower()
                    and path.suffix.lower() == ".dds"
                ),
                None,
            )
            if generated is None:
                raise RuntimeError(f"texconv did not produce {image.stem}.dds")
            target = loose / f"{image.stem}.dds"
            if generated != target:
                if target.exists():
                    target.unlink()
                generated.rename(target)
            converted += 1
    return converted


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Build an Automobili Lamborghini RT64 .rtz pack")
    parser.add_argument("--source-dir", type=Path, default=REPO / "textures")
    parser.add_argument("--output-dir", type=Path, default=REPO / "dist")
    parser.add_argument("--pack-json", type=Path, default=REPO / "pack.json")
    parser.add_argument("--policy", type=Path, default=REPO / "texture-policy.json")
    parser.add_argument("--texconv", type=Path, required=True,
                        help="path to DirectXTex texconv.exe")
    parser.add_argument("--rt64-packer", type=Path, required=True,
                        help="path to RT64 texture_packer(.exe)")
    parser.add_argument("--release-tag",
                        help="optional Git tag; must match pack_version (for example v0.1.0)")
    parser.add_argument("--for-release", action="store_true",
                        help="require a resolved artwork license")
    args = parser.parse_args()

    source_dir = args.source_dir.resolve()
    output_dir = args.output_dir.resolve()
    pack_json = args.pack_json.resolve()
    policy_path = args.policy.resolve()
    texconv = args.texconv.resolve()
    rt64_packer = args.rt64_packer.resolve()
    for label, path in (("source directory", source_dir), ("pack metadata", pack_json),
                        ("texconv", texconv), ("RT64 packer", rt64_packer)):
        if not path.exists():
            raise RuntimeError(f"{label} does not exist: {path}")

    metadata = json.loads(pack_json.read_text(encoding="utf-8"))
    if not isinstance(metadata, dict):
        raise RuntimeError(f"pack metadata must be a JSON object: {pack_json}")
    if args.release_tag and args.release_tag != f"v{metadata.get('pack_version', '')}":
        raise RuntimeError(
            f"release tag {args.release_tag!r} does not match pack_version "
            f"{metadata.get('pack_version')!r}"
        )
    source_manifest = pack_json.parent / "rt64.json"
    errors = validate(
        pack_json,
        source_dir,
        source_manifest if source_manifest.is_file() else None,
        require_license=args.for_release,
    )
    if errors:
        raise RuntimeError("metadata/source validation failed:\n  " + "\n  ".join(errors))

    policy = _load_policy(policy_path)
    lossless, single_level = _policy_texture_sets(policy)
    output_dir.mkdir(parents=True, exist_ok=True)
    loose = output_dir / _safe_name(metadata)
    if loose.exists():
        shutil.rmtree(loose)
    loose.mkdir(parents=True)
    _copy_metadata(pack_json, loose, metadata)
    converted = _convert_sources(source_dir, loose, texconv, lossless, single_level)
    if converted == 0:
        raise RuntimeError("no source images were converted")

    make_pack = HERE / "make_pack.py"
    command = [
        sys.executable, str(make_pack), str(loose),
        "--auto-path", "rt64",
        "--shift", str(policy.get("default_shift", "none")),
        "--operation", str(policy.get("default_operation", "stream")),
    ]
    if policy_path.is_file():
        command.extend(["--policy", str(policy_path)])
    _run(command)

    errors = validate(
        loose / "pack.json",
        loose,
        loose / "rt64.json",
        args.for_release,
        allow_generated_cache=True,
    )
    if errors:
        raise RuntimeError("generated pack validation failed:\n  " + "\n  ".join(errors))

    _run([str(rt64_packer), str(loose), "--create-low-mip-cache"])
    _run([str(rt64_packer), str(loose), "--create-pack"])
    # RT64 writes the archive inside the loose directory. with_suffix() would
    # also truncate the patch component of a SemVer directory name (v0.1.0 ->
    # v0.1), so construct the filename explicitly and move the release asset to
    # dist/ after the packer finishes.
    produced_archive = loose / (loose.name + ".rtz")
    if not produced_archive.is_file():
        raise RuntimeError(f"RT64 packer did not produce {produced_archive}")
    archive = output_dir / produced_archive.name
    if archive.exists():
        archive.unlink()
    shutil.move(str(produced_archive), str(archive))
    sidecars: list[Path] = []
    for name in ("pack.json", "CREDITS.md", "NOTICE.md", "PROVENANCE.md", "LICENSE", "LICENSE.md"):
        source_sidecar = loose / name
        if not source_sidecar.is_file():
            continue
        destination = output_dir / name
        shutil.copy2(source_sidecar, destination)
        sidecars.append(destination)
    checksum = output_dir / "SHA256SUMS"
    release_files = [archive, *sidecars]
    checksum.write_text(
        "".join(f"{_sha256(path)}  {path.name}\n" for path in release_files),
        encoding="utf-8",
    )
    print(f"built {archive} ({archive.stat().st_size} bytes)")
    print(f"sha256 {_sha256(archive)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
