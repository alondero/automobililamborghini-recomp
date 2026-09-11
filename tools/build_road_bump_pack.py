#!/usr/bin/env python3
"""Build the road-only baked surface-detail pilot using the companion pack tools."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
ROAD = "296a811299243783"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--companion", required=True, type=Path,
                        help="read-only automobili-lamborghini-textures checkout")
    parser.add_argument("--texconv", required=True, type=Path)
    parser.add_argument("--rt64-packer", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="new, empty build directory")
    parser.add_argument("--install", type=Path, metavar="GRAPHICS_JSON",
                        help="enable the built pack in this existing graphics.json")
    args = parser.parse_args()
    companion = args.companion.resolve()
    output = args.output.resolve()
    texconv = args.texconv.resolve()
    packer = args.rt64_packer.resolve()
    graphics = None
    original_graphics = None
    if args.install:
        try:
            original_graphics = args.install.read_bytes()
            graphics = json.loads(original_graphics)
        except (OSError, ValueError) as error:
            parser.error(f"cannot read graphics config: {error}")
        if not isinstance(graphics, dict):
            parser.error("graphics config must be a JSON object")
        if graphics.get("texture_pack"):
            parser.error("graphics config already selects a pack; refusing to replace it")
    road_source = companion / "textures" / f"{ROAD}.png"
    for path in (companion / "tools/build_pack.py", companion / "pack.json",
                 road_source,
                 texconv, packer):
        if not path.is_file():
            parser.error(f"missing input: {path}")
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error(f"output directory must be empty: {output}")
    source = output / "source"
    source.mkdir(parents=True, exist_ok=True)
    # The driving-surface atlas is 128x16 in-game, including its lane markings.
    # Keep the companion pack's 6x scale and wrap across longitudinal repeats.
    subprocess.run([
        str(texconv), "-nologo", "-w", "768", "-h", "96", "-m", "0",
        "-wrap", "-f", "BC7_UNORM", "-bc", "x", "-nogpu", "-l",
        "-o", str(source), str(road_source),
    ], check=True)
    metadata = json.loads((companion / "pack.json").read_text(encoding="utf-8"))
    port_commit = subprocess.check_output(
        ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    source_commit = subprocess.check_output(
        ["git", "-C", str(companion), "rev-parse", "HEAD"], text=True).strip()
    metadata.update(pack_id="lambo-road-bump-pilot", variant="road-only",
                    source_commit=source_commit, port_commit=port_commit,
                    credits="CREDITS.md")
    (source / "pack.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    (source / "policy.json").write_text(
        json.dumps({"default_shift": "none", "default_operation": "stream"}) + "\n",
        encoding="utf-8")
    shutil.copyfile(companion / "CREDITS.md", source / "CREDITS.md")
    subprocess.run([
        sys.executable, str(companion / "tools/build_pack.py"),
        "--source-dir", str(source), "--pack-json", str(source / "pack.json"),
        "--policy", str(source / "policy.json"), "--output-dir", str(output / "dist"),
        "--texconv", str(texconv), "--rt64-packer", str(packer),
    ], check=True)
    archive, = (output / "dist").glob("*.rtz")
    print(f"Road pack: {archive}")
    if args.install:
        if args.install.read_bytes() != original_graphics:
            parser.error("graphics config changed during the build; pack built but not installed")
        graphics["texture_pack"] = str(archive)
        temporary = args.install.with_name(args.install.name + ".road-bump.tmp")
        temporary.write_text(json.dumps(graphics, indent=4) + "\n", encoding="utf-8")
        os.replace(temporary, args.install)
        print(f"Enabled road detail in {args.install}. Restart the game to apply it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
