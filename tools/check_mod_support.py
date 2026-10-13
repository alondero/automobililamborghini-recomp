"""Exercise real mod loading with a smoke NRM and an optional RT64 texture pack."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


MARKER = "[mod-smoke] boot hook executed"
TEXTURE_ID = "texture_smoke"


def run_case(executable, package, rom, enabled, minimum_version=None, *, texture=False):
    with tempfile.TemporaryDirectory(prefix="lambo-mod-") as directory:
        root = Path(directory)
        config = root / "LamborghiniRecomp"
        mods = config / "mods"
        mods.mkdir(parents=True)
        if texture:
            # RT64 packs may use zstd ZIP entries. Preserve every byte and let
            # the runtime synthesize the identity from this stable filename.
            shutil.copyfile(package, mods / f"{TEXTURE_ID}.rtz")
            mod_id = TEXTURE_ID
        else:
            destination = mods / "smoke.nrm"
            with zipfile.ZipFile(package) as source:
                manifest = json.loads(source.read("mod.json"))
                if minimum_version is not None:
                    manifest["minimum_recomp_version"] = minimum_version
                with zipfile.ZipFile(destination, "w") as output:
                    for entry in source.infolist():
                        data = (json.dumps(manifest).encode() if entry.filename == "mod.json"
                                else source.read(entry.filename))
                        output.writestr(entry, data)
            mod_id = manifest["id"]
        (config / "mods.json").write_text(json.dumps({
            "enabled_mods": [mod_id] if enabled else [], "mod_order": [mod_id],
        }), encoding="utf-8")
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith("LAMBO_")}
        environment.update({
            "LOCALAPPDATA": str(root), "XDG_CONFIG_HOME": str(root),
            "XDG_STATE_HOME": str(root), "LAMBO_HEADLESS": "1",
            "LAMBO_LAUNCHER": "0", "LAMBO_MODERN_MAX_VIS": "120",
        })
        # Use an empty cwd so a developer's portable.txt cannot redirect writes
        # into a real player profile. The executable must not be portable-marked.
        result = subprocess.run(
            [str(executable), str(rom), "--console", "--verbose"],
            cwd=root, env=environment, capture_output=True, timeout=40,
        )
        output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
        return result.returncode, output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--protected-package", type=Path)
    parser.add_argument("--texture-package", type=Path,
                        help="optional RT64 .rtz with rt64.json and no mod.json")
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("artifacts/mod-support"))
    args = parser.parse_args()
    executable, package, rom = (path.resolve() for path in (args.exe, args.package, args.rom))
    if (executable.parent / "portable.txt").exists():
        parser.error("use a test executable without portable.txt beside it")
    if args.texture_package:
        texture_package = args.texture_package.resolve()
        try:
            if not texture_package.is_file():
                parser.error("texture package must be an existing regular .rtz ZIP file")
            # Inspect only the ZIP directory: Python need not support the RT64
            # pack's compression method, and the archive must remain unchanged.
            with zipfile.ZipFile(texture_package) as archive:
                entries = {entry.filename: entry for entry in archive.infolist()}
                if "rt64.json" not in entries or entries["rt64.json"].is_dir():
                    parser.error("texture package must contain rt64.json at the archive root")
                if "mod.json" in entries:
                    parser.error("texture package must not contain mod.json")
        except (OSError, zipfile.BadZipFile) as error:
            parser.error(f"cannot read texture package ZIP: {error}")
    args.output.mkdir(parents=True, exist_ok=True)
    cases = [("enabled", package, True, None, 0, 1),
             ("disabled", package, False, None, 0, 0),
             ("minimum-version", package, True, "999.0.0", 2, 0)]
    if args.protected_package:
        cases.append(("protected-hook", args.protected_package.resolve(), True, None, 2, 0))
    for name, path, enabled, minimum, expected_code, markers in cases:
        code, output = run_case(executable, path, rom, enabled, minimum)
        (args.output / f"{name}.log").write_text(output, encoding="utf-8")
        passed = code == expected_code and output.count(MARKER) == markers
        if expected_code == 0:
            passed &= "game thread #1 started" in output
        else:
            passed &= "Error loading mods:" in output and "game thread #1 started" not in output
        print(f"{'PASS' if passed else 'FAIL'} {name}: exit={code}, markers={output.count(MARKER)}")
        if not passed:
            return 1
    if args.texture_package:
        code, output = run_case(executable, args.texture_package.resolve(), rom, True,
                                texture=True)
        (args.output / "texture-pack.log").write_text(output, encoding="utf-8")
        # A skipped/unregistered container can still boot successfully.
        passed = (code == 0 and f"Loading mod {TEXTURE_ID}" in output.splitlines()
                  and "game thread #1 started" in output
                  and "Skipping non-mod" not in output
                  and "Error loading mods:" not in output)
        print(f"{'PASS' if passed else 'FAIL'} texture-pack: exit={code}")
        if not passed:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
