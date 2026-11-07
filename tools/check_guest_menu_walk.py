"""Replay the normal menu route with read-only original-symbol hook observers."""
import argparse
import json
from pathlib import Path
import shutil

import run_game_scenario as harness


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default="build/lamborghini_modern.exe")
    parser.add_argument("--package", type=Path,
                        default=Path("build/guest-source-smoke/menu-walk/guest_source_smoke.nrm"))
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[1]
    executable = harness.find_executable(args.exe, repository)
    if (executable.parent / "portable.txt").exists():
        parser.error("Use an executable without portable.txt to isolate the profile")
    source = repository / "scenarios/guest-menu-walk.json"
    scenario = harness.load_scenario(source)
    artifact = harness.create_artifacts(repository / "build/menu-walk", scenario["name"])
    runtime = harness.stage_fixtures(scenario, artifact)
    environment = harness.build_environment(scenario, runtime, artifact)
    profile = artifact / "user-config/LamborghiniRecomp"
    (profile / "mods").mkdir(parents=True)
    shutil.copyfile(args.package, profile / "mods/observer.nrm")
    (profile / "mods.json").write_text(json.dumps({
        "enabled_mods": ["guest_source_smoke"], "mod_order": ["guest_source_smoke"],
    }), encoding="utf-8")
    returncode, stdout, stderr, timed_out = harness.run_process(
        harness.command_for(executable), environment, repository, 90)
    harness.write_artifacts(artifact, source, runtime, environment, stdout, stderr)
    failures = ["timed out after 90s"] if timed_out else []
    native = None
    try:
        native = harness.load_native_result(artifact / "harness-result.json")
        failures.extend(harness.evaluate(runtime, native, returncode, stderr))
        for bootstrap in ("warp", "state_load"):
            if native[bootstrap]["applied"] or native[bootstrap]["requested"]:
                failures.append(f"unexpected {bootstrap}")
    except (harness.ScenarioError, KeyError) as error:
        failures.append(str(error))
    harness.write_runner_result(artifact, scenario["name"], executable, returncode,
                                timed_out, failures, native)
    print(f"{'FAIL' if failures else 'PASS'} guest menu walk: {failures or 'normal route and both selector directions'} ({artifact})")
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
