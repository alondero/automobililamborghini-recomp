"""Run source-patch hook and ordinary replacement-conflict packages headlessly."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PASS = "[guest-source-smoke] original calls, entry/return hooks and setting helper passed"
RACE_PASS = "[guest-source-smoke] active race updater entry/return passed"
CONFLICT = "Attempted to replace a function that's been patched by the base recomp"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--packages", type=Path, default=Path("build/guest-source-smoke"))
    parser.add_argument("--output", type=Path, default=Path("build/guest-source-mod-check"))
    args = parser.parse_args()
    executable, rom = args.exe.resolve(), args.rom.resolve()
    if (executable.parent / "portable.txt").exists():
        parser.error("Use an executable without portable.txt to keep the test profile isolated")
    args.output.mkdir(parents=True, exist_ok=True)
    failed = False
    for variant in ["hooks", "func_8003F40C", "func_8003F56C", "func_800427D4", "func_80019D20"]:
        with tempfile.TemporaryDirectory(prefix="lambo-guest-source-") as directory:
            root = Path(directory)
            profile = root / "LamborghiniRecomp"
            mods = profile / "mods"
            mods.mkdir(parents=True)
            shutil.copyfile(args.packages / variant / "guest_source_smoke.nrm", mods / "fixture.nrm")
            (profile / "mods.json").write_text(json.dumps({
                "enabled_mods": ["guest_source_smoke"], "mod_order": ["guest_source_smoke"],
            }), encoding="utf-8")
            env = {k:v for k,v in os.environ.items() if not k.startswith("LAMBO_")}
            env.update({"LOCALAPPDATA": str(root), "XDG_CONFIG_HOME": str(root),
                        "XDG_STATE_HOME": str(root), "LAMBO_HEADLESS": "1",
                        "LAMBO_LAUNCHER": "0", "LAMBO_MODERN_MAX_VIS": "600", "LAMBO_WARP": "1",
                        "LAMBO_ANALOG_THROTTLE": "0.5", "LAMBO_ANALOG_BRAKE": "0.25",
                        "LAMBO_ANALOG_THROTTLE_PROBE": "1", "LAMBO_ANALOG_BRAKE_PROBE": "1",
                        "LAMBO_MENU_STICK_SENSITIVITY": "1.5"})
            try:
                result = subprocess.run([str(executable), str(rom), "--console", "--lambo-debug"],
                                        cwd=root, env=env, capture_output=True, timeout=60)
                output = (result.stdout+result.stderr).decode("utf-8", errors="replace")
                if variant == "hooks":
                    passed = (result.returncode == 0 and output.count(PASS) == 1 and
                              output.count(RACE_PASS) == 1 and
                              "analog throttle field:" in output and "analog brake field:" in output and
                              "[guest-source-smoke] FAILED" not in output and
                              "Error loading mods:" not in output)
                else:
                    passed = (result.returncode == 2 and CONFLICT in output and
                              "Error loading mods:" in output and PASS not in output and
                              "game thread #1 started" not in output)
            except subprocess.TimeoutExpired as error:
                output = ((error.stdout or b"")+(error.stderr or b"")).decode("utf-8", errors="replace")
                output += "\nFixture timed out after 60 seconds.\n"
                passed = False
            (args.output / f"{variant}.log").write_text(output, encoding="utf-8")
            print(f"{'PASS' if passed else 'FAIL'} {variant}")
            failed |= not passed
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
