"""Run the experimental JP weather package in isolated USA port profiles.

This checks loading, saved configuration and race-start resolution. It does
not certify JP AI trajectories, rendering, or the unimplemented throttle cap.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

USA_SHA256 = "cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c"


def markers_match(markers, expected):
    if expected is None:
        return not markers
    wanted = [expected] if expected == "US default" else ["data verified", expected]
    return markers == wanted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("artifacts/jp-weather-mod"))
    args = parser.parse_args()
    exe, package, rom = (p.resolve() for p in (args.exe, args.package, args.rom))
    if (exe.parent / "portable.txt").exists():
        parser.error("Use an executable without portable.txt beside it")
    if hashlib.sha256(rom.read_bytes()).hexdigest() != USA_SHA256:
        parser.error("USA cartridge identity mismatch")
    with zipfile.ZipFile(package) as archive:
        manifest = json.loads(archive.read("mod.json"))
    if manifest["id"] != "lambo_jp_weather":
        parser.error("Not a JP weather package")
    args.output.mkdir(parents=True, exist_ok=True)
    cases = [("absent", False, "Wet", 1, 2, None),
             ("us-default", True, "US default", 1, 2, "US default"),
             ("dry", True, "Dry", 1, 2, "dry"),
             ("wet", True, "Wet", 1, 2, "wet"),
             ("night", True, "Night", 1, 2, "night"),
             ("three-player", True, "Wet", 3, 2, "dry"),
             ("time-challenge", True, "Wet", 1, 1, "dry"),
             ("championship", True, "Dry", 1, 3, "wet")]
    report = {"schema": 1, "executable_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
              "package_sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
              "rom_sha256": USA_SHA256,
              "scope": "Package loading and race-start resolution; no JP behavioral parity claim",
              "requested_cases": [c[0] for c in cases], "complete": False, "cases": []}
    for name, enabled, choice, players, race_mode, expected in cases:
        record = {"name": name, "passed": False}
        with tempfile.TemporaryDirectory(prefix="jp-weather-mod-") as directory:
            root = Path(directory)
            profile = root / "LamborghiniRecomp"
            (profile / "mods").mkdir(parents=True)
            (profile / "mod_config").mkdir()
            if enabled:
                shutil.copyfile(package, profile / "mods/weather.nrm")
            (profile / "mods.json").write_text(json.dumps({
                "enabled_mods": [manifest["id"]] if enabled else [],
                "mod_order": [manifest["id"]] if enabled else []}), encoding="utf-8")
            (profile / f"mod_config/{manifest['id']}.json").write_text(json.dumps({
                "mod_id": manifest["id"], "mod_version": manifest["version"],
                "storage": {"weather.mode": choice, "weather.round1": "Wet"}}), encoding="utf-8")
            trace = (args.output / f"{name}.trace").resolve()
            trace.unlink(missing_ok=True)
            env = {k: v for k, v in os.environ.items() if not k.startswith("LAMBO_")}
            env.update(LOCALAPPDATA=directory, XDG_CONFIG_HOME=directory, XDG_STATE_HOME=directory,
                       LAMBO_HEADLESS="1", LAMBO_LAUNCHER="0", LAMBO_MODERN_MAX_VIS="600",
                       LAMBO_WARP=f"1:3:0:{players}", LAMBO_WARP_MODE=str(race_mode),
                       LAMBO_CAR_TRACE="1", LAMBO_CAR_TRACE_PATH=str(trace))
            try:
                run = subprocess.run([str(exe), str(rom), "--console", "--verbose"],
                                     cwd=root, env=env, capture_output=True, timeout=120)
                log = (run.stdout + run.stderr).decode("utf-8", errors="replace")
                (args.output / f"{name}.log").write_text(log, encoding="utf-8")
                record["exit_code"] = run.returncode
                markers = [line.split("[jp-weather] ", 1)[1] for line in log.splitlines()
                           if "[jp-weather] " in line]
                record["markers"] = markers
                record["reached_race"] = trace.exists() and any(
                    line.startswith("F ") and " st 8 " in line
                    for line in trace.read_text().splitlines())
                record["passed"] = (run.returncode == 0 and record["reached_race"] and
                    "reached 600 VIs; quitting" in log and
                    markers_match(markers, expected))
            except (OSError, subprocess.TimeoutExpired) as error:
                record["error"] = str(error)
        report["cases"].append(record)
        (args.output / "summary.json").write_text(json.dumps(report, indent=2) + "\n",
                                                  encoding="utf-8", newline="\n")
        print(json.dumps(record), flush=True)
    report["complete"] = True
    (args.output / "summary.json").write_text(json.dumps(report, indent=2) + "\n",
                                              encoding="utf-8", newline="\n")
    return 0 if all(case["passed"] for case in report["cases"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
