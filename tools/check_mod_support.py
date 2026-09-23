"""Exercise real mod loading in isolated profiles using the developer smoke NRM."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile


MARKER = "[mod-smoke] boot hook executed"


def run_case(executable, package, rom, enabled, minimum_version=None):
    with tempfile.TemporaryDirectory(prefix="lambo-mod-") as directory:
        root = Path(directory)
        config = root / "LamborghiniRecomp"
        mods = config / "mods"
        mods.mkdir(parents=True)
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
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("artifacts/mod-support"))
    args = parser.parse_args()
    executable, package, rom = (path.resolve() for path in (args.exe, args.package, args.rom))
    if (executable.parent / "portable.txt").exists():
        parser.error("use a test executable without portable.txt beside it")
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
