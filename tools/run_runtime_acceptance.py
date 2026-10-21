"""Run field/HUD, championship, Quit and reset acceptance checks on one build."""
import argparse
import hashlib
import json
import platform
import re
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

import check_field_championship
import check_race_field
import run_game_scenario as runner

ROOT = Path(__file__).resolve().parents[1]


def build_evidence(build):
    compiler = {}
    for path in (build / 'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'):
        source = path.read_text(encoding='utf-8')
        for name in ('ID', 'VERSION'):
            match = re.search(r'set\(CMAKE_CXX_COMPILER_' + name + r' "([^"]+)"\)', source)
            if match:
                compiler[name.lower()] = match.group(1)
    return {'compiler': compiler or 'unavailable',
            'generated_files': {name: (ROOT / name).is_file()
                                for name in ('RecompiledFuncs/funcs.h', 'src/aspMain.cpp')},
            'generation_freshness': 'not inferred; use the supported build scripts before this run'}


def backend_evidence(artifact):
    path = artifact / 'stderr.log'
    if path.is_file():
        for line in path.read_text(encoding='utf-8').splitlines():
            if 'SDL window created' in line:
                return line
    return 'no window-backend diagnostic; see scenario headless setting'


def run(executable, build, *, headless=False):
    artifact = runner.create_artifacts(ROOT / 'artifacts/runtime-acceptance', 'suite')
    report = {'outcome': 'failed', 'executable': str(executable), 'headless': headless,
              'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
              'os': platform.platform(),
              'not_run': ['launcher-quit', 'repeated-reset'] if headless else [],
              'build': build_evidence(build),
              'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'rom_sha256': hashlib.sha256((ROOT / 'Automobili Lamborghini (USA).z64').read_bytes()).hexdigest(),
              'cases': []}
    try:
        # These execute the actual generated minimap and rank emitters, and the
        # reset safety fixture; a successful movement replay alone cannot cover them.
        for name in ('race_field', 'reset', 'reset_observation', 'quit_path'):
            subprocess.run(['ctest', '--test-dir', str(build), '--output-on-failure', '--no-tests=error',
                            '-R', f'^lambo_{name}$'], cwd=ROOT, check=True)
        for count in (2, 6, 16):
            field = check_race_field.run(SimpleNamespace(cars=count, circuit=1, reverse=0,
                mode=3 if count == 16 else 2, players=1, exe=str(executable)))
            report['cases'].append({'name': f'field-{count}', 'artifacts': str(field), 'headless': True})
            if count == 16:
                for place in (10, 16):
                    championship = check_field_championship.run(SimpleNamespace(
                        state=field / 'race.lstate', render=not headless,
                        player_place=place, exe=str(executable)))
                    report['cases'].append({'name': f'championship-place-{place}', 'artifacts': str(championship),
                                            'headless': headless, 'backend_log': backend_evidence(championship)})
        if not headless:
            for name in ('launcher-quit', 'repeated-reset'):
                if runner.main([str(ROOT / f'scenarios/{name}.json'), '--exe', str(executable),
                                '--artifacts-dir', str(artifact)]) != 0:
                    raise RuntimeError(f'{name} failed; see {artifact}')
                runs = list(artifact.glob(name + '-*'))
                case = {'name': name}
                if len(runs) == 1:
                    case.update(artifacts=str(runs[0]), backend_log=backend_evidence(runs[0]))
                report['cases'].append(case)
        report['outcome'] = 'passed'
    finally:
        (artifact / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'Runtime acceptance report: {artifact / "result.json"}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', help='Path to the freshly built game executable')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--headless', action='store_true', help='Run ROM-backed checks without the rendered Quit/reset checks')
    args = parser.parse_args()
    try:
        run(runner.find_executable(args.exe, ROOT), args.build_dir, headless=args.headless)
    except (AssertionError, OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
