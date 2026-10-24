"""ROM-backed field regression. Snapshots/captures stay in ignored artifacts/."""
import argparse
import json
import struct
from pathlib import Path
import run_game_scenario as runner


def inspect(path, count, reverse=None, players=1):
    blob = path.read_bytes()
    assert blob[:8] == b'LMBOSTAT' and struct.unpack_from('<I', blob, 8)[0] == 2
    assert len(blob) == 32 + 0x800000 + 0x24000
    def read(address, fmt):
        offset = address & 0xffffff
        if offset >= 0x900000:
            offset -= 0x100000
        if fmt == 'h':
            offset ^= 2
        return struct.unpack_from('<' + fmt, blob, 32 + offset)[0]
    task, cursor = read(0x800a2bfc, 'I'), read(0x800a39cc, 'I')
    assert task in (0x800bf240, 0x800c6c90)
    assert task + 0x1c0 <= cursor <= task + 0x79c0, (hex(task), hex(cursor))
    assert read(0x800ce7ac, 'h') == count
    if reverse is not None:
        assert read(0x800ce7a8, 'h') == reverse
    cars = []
    for racer in range(1, count + 1):
        body = read(0x80901100 + racer * 2, 'h')
        assert 0 <= body < 64, (racer, body)
        base = 0x80910000 + body * 0x10c
        assert read(base, 'h') & 8
        cars.append({'racer': racer, 'body': body, 'speed': read(base + 0x90, 'i'),
                     'rank': read(0x80902000 + racer * 0x84 + 4, 'h'),
                     'model': read(0x80901000 + (racer - 1) * 2, 'h'),
                     'position': [read(base + off, 'f') for off in (0x14, 0x18, 0x1c)]})
    assert len({car['body'] for car in cars}) == count
    assert sorted(car['rank'] for car in cars) == list(range(1, count + 1)), cars
    assert all(car['speed'] > 0 for car in cars if car['racer'] == 1 or car['racer'] > players), cars
    assert read(0x800a672a, 'h') >= 0 and read(0x800a6730, 'h') >= 0
    return cars


def run(args):
    expected = 1 if args.mode == 0 else 4 if args.players > 1 else args.cars
    repo = Path(__file__).resolve().parents[1]
    artifact = runner.create_artifacts(repo / 'artifacts/race-fields', f'{args.cars}-cars-c{args.circuit}-r{args.reverse}-m{args.mode}')
    scenario_path = artifact / 'input-scenario.json'
    scenario_path.write_text(json.dumps({'schema': 1, 'name': artifact.name,
        'headless': True, 'warp': f'{args.circuit}:1:0:{args.players}', 'warp_mode': args.mode,
        'max_vis': 3600, 'input': {'replay': str(repo / 'recordings/harness-smoke.jsonl'),
        'start_state': 8, 'start_delay': 0, 'exit_on_end': True},
        'capture': {'path': 'race.bmp', 'state': 8, 'every': 300},
        'expect': {'max_state_at_least': 8, 'replay_complete': True, 'min_swaps': 60},
        'developer_env': {'LAMBO_RACE_CARS': str(args.cars)}}), encoding='utf-8')
    scenario = runner.load_scenario(scenario_path)
    runtime = runner.stage_fixtures(scenario, artifact)
    environment = runner.build_environment(scenario, runtime, artifact)
    state = artifact / 'race.lstate'
    environment.update(LAMBO_STATE_SAVE=str(state), LAMBO_STATE_SAVE_DELAY='300', LAMBO_WARP_REVERSE=str(args.reverse))
    executable = runner.find_executable(args.exe, repo)
    code, stdout, stderr, timed_out = runner.run_process(runner.command_for(executable), environment, repo, 180)
    runner.write_artifacts(artifact, scenario_path, runtime, environment, stdout, stderr)
    assert not timed_out and code == 0, (code, artifact)
    result = runner.load_native_result(artifact / 'harness-result.json')
    assert not runner.evaluate(runtime, result, code, stderr), artifact
    cars = inspect(state, expected, args.reverse, args.players)
    (artifact / 'field-result.json').write_text(json.dumps(cars, indent=2), encoding='utf-8')
    print(f'PASS {artifact.name}: {expected} cars; active racers moving; unique bodies and ranks', flush=True)
    return artifact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cars', type=int, choices=range(2, 17), default=16)
    parser.add_argument('--circuit', type=int, choices=range(1, 7), default=1)
    parser.add_argument('--reverse', type=int, choices=(0, 1), default=0)
    parser.add_argument('--mode', type=int, choices=range(4), default=2)
    parser.add_argument('--players', type=int, choices=range(1, 5), default=1)
    parser.add_argument('--exe', help='Path to the game executable')
    run(parser.parse_args())


if __name__ == '__main__':
    main()
