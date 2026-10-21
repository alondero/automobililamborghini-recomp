"""Check native championship progression using controlled outcomes in a v2 snapshot.

First run check_race_field.py --cars 16 --mode 3, then pass its race.lstate here.
This exercises results and standings logic, not natural race completion.
"""
import argparse
import json
import struct
from pathlib import Path

import check_race_field
import run_game_scenario as runner

RACE_MODE_ADDR = 0x800CE6B4


def offset(address):
    value = address & 0xffffff
    return 32 + ((value - 0x100000 if value >= 0x900000 else value) ^ 2)


def half(blob, address):
    return struct.unpack_from('<h', blob, offset(address))[0]


def require_championship_mode(blob):
    mode = half(blob, RACE_MODE_ADDR)
    if mode != 3:
        raise ValueError(
            f'input snapshot has race mode {mode}; championship progression requires mode 3. '
            'Create a suitable snapshot with: python tools/check_race_field.py '
            '--cars 16 --circuit 1 --reverse 0 --mode 3'
        )


def run(args):
    blob = bytearray(args.state.read_bytes())
    require_championship_mode(blob)
    check_race_field.inspect(args.state, 16)
    roster = [half(blob, 0x80901000 + i * 2) for i in range(16)]
    # Enter native results initialization in championship round one. Round two
    # uses circuit two, three laps, forward direction and no mandatory stops.
    values = [(0x800ce6ac, 9), (0x800ce6a4, 1), (RACE_MODE_ADDR, 3),
              (0x800ce6ba, 1), (0x800ce6b6, 3), (0x80901400, 16),
              (0x80901402, 3), (0x80901408, 0), (0x800ce6c4, 1),
              (0x800ce6e4, 3), (0x800ce704, 0), (0x800ce724, 0)]
    places = [args.player_place] + [place for place in range(1, 17) if place != args.player_place]
    for racer in range(1, 17):
        values.extend([(0x80902000 + racer * 0x84 + 6, places[racer - 1]),
                       (0x80901200 + racer * 2, 100 - racer)])
    for address, value in values:
        struct.pack_into('<h', blob, offset(address), value)
    repo = Path(__file__).resolve().parents[1]
    artifact = runner.create_artifacts(repo / 'artifacts/race-fields', 'championship16')
    state = artifact / 'results.lstate'
    state.write_bytes(blob)
    trace = [{'type': 'header', 'format': 'lambo-input-trace', 'version': 1,
              'clock': 'game-dispatch', 'ports': 1}]
    for frames, buttons in [(180, 0), (1, 32768), (180, 0), (1, 32768), (420, 0)]:
        trace.append({'type': 'run', 'frames': frames, 'buttons': buttons,
                      'stick_x': 0, 'stick_y': 0, 'throttle_analog': False,
                      'throttle': 0, 'brake_analog': False, 'brake': 0})
    trace.append({'type': 'end', 'frames': 782})
    replay = artifact / 'advance.jsonl'
    replay.write_text('\n'.join(json.dumps(row) for row in trace) + '\n', encoding='utf-8')
    scenario_path = artifact / 'input-scenario.json'
    scenario_path.write_text(json.dumps({'schema': 1, 'name': 'championship16',
        'headless': not args.render, 'state_load': str(state), 'max_vis': 5000,
        'input': {'replay': str(replay), 'start_state': 10, 'exit_on_end': True},
        'capture': {'path': 'results.bmp', 'state': 10, 'every': 60},
        'expect': {'replay_complete': True, 'max_state_at_least': 10, 'min_swaps': 100},
        'developer_env': {'LAMBO_RACE_CARS': '2'}}), encoding='utf-8')
    scenario = runner.load_scenario(scenario_path)
    runtime = runner.stage_fixtures(scenario, artifact)
    environment = runner.build_environment(scenario, runtime, artifact)
    result_path = artifact / 'round2.lstate'
    environment.update(LAMBO_STATE_SAVE=str(result_path), LAMBO_STATE_SAVE_DELAY='300')
    executable = runner.find_executable(args.exe, repo)
    code, stdout, stderr, timed_out = runner.run_process(runner.command_for(executable), environment, repo, 180)
    runner.write_artifacts(artifact, scenario_path, runtime, environment, stdout, stderr)
    assert not timed_out and code == 0, (code, artifact)
    assert not runner.evaluate(runtime, runner.load_native_result(artifact / 'harness-result.json'), code, stderr), artifact
    result = result_path.read_bytes()
    assert [half(result, a) for a in (0x800ce6ac, 0x800ce6b4, 0x800ce6ba, 0x800ce7ac)] == [8, 3, 2, 16]
    assert [half(result, 0x80901000 + i * 2) for i in range(16)] == roster
    points = {1: 9, 2: 6, 3: 4, 4: 2, 5: 1}
    awards = [points.get(place, 0) for place in places]
    assert [half(result, 0x80901200 + i * 2) for i in range(1, 17)] == [99 - i + award for i, award in enumerate(awards)]
    print(f'PASS {artifact}: round two retains 16 entrants, roster and accumulated points', flush=True)
    return artifact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('state', type=Path)
    parser.add_argument('--render', action='store_true', help='Show the RT64 window for visual inspection')
    parser.add_argument('--player-place', type=int, choices=range(1, 17), default=16)
    parser.add_argument('--exe', help='Path to the game executable')
    try:
        run(parser.parse_args())
    except ValueError as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
