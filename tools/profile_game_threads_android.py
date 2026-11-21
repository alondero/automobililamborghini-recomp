"""Capture issue 426's isolated Android shell from a Windows host.

Requires the debug shell and private fixtures described in docs/performance.md.
All application mutations target profile426. Raw logs and profiles stay local.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PKG = 'io.github.alondero.lamborghinirecomp.profile426'
SDK = Path(os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
           or str(Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'Android/Sdk'))
PERF = SDK / 'ndk/28.2.13676358/simpleperf/bin/windows/x86_64/simpleperf.exe'


def adb(*args, check=True):
    return subprocess.run(['adb', *map(str, args)], check=check, capture_output=True,
                          timeout=30).stdout.decode(errors='replace').strip()


def app(command, check=True):
    return adb('shell', 'run-as', PKG, 'sh', '-c', shlex.quote(command), check=check)


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8', newline='\n')


def log():
    return app('cat files/native-errors.log', check=False)


def focus():
    output = adb('shell', 'dumpsys', 'window')
    match = re.search(r'mCurrentFocus=.*', output)
    return match.group() if match else 'MISSING'


def counters(pid):
    start = time.monotonic()
    output = app(f'for t in /proc/{pid}/task/*; do cat "$t/stat"; done')
    return {'start': start, 'end': time.monotonic(), 'stats': output}


def run(workload):
    warp, mode = ('1:1:0:1', '0') if workload == '1p' else ('1:3:0:4', '2')
    folder = ROOT / 'artifacts/gh426' / (workload + '-' + datetime.now().strftime('%H%M%S'))
    folder.mkdir(parents=True)
    print(f'{workload}: {folder}', flush=True)
    meta = {'workload': workload, 'warp': warp, 'mode': int(mode), 'focus': []}
    recorder = None
    try:
        others = adb('shell', 'ps', '-A')
        if any('lamborghinirecomp' in line and PKG not in line for line in others.splitlines()):
            raise RuntimeError('Another Lamborghini application is running')
        adb('shell', 'am', 'force-stop', PKG)
        app('rm -f files/profile/result.json files/profile/controller.mpk files/profile/perf.data files/native-errors.log files/native.log')
        adb('shell', 'input', 'keyevent', '224')
        adb('shell', 'wm', 'dismiss-keyguard')
        meta['launch'] = adb('shell', 'am', 'start', '-n', PKG + '/io.github.alondero.lamborghinirecomp.GameActivity', '--es', 'warp', warp, '--es', 'mode', mode)
        deadline = time.monotonic() + 90
        while 'playback started' not in log():
            if time.monotonic() > deadline:
                raise RuntimeError('Replay did not start in 90 s')
            time.sleep(0.5)
        meta['warmup_started_utc'] = datetime.now(timezone.utc).isoformat()
        until = time.monotonic() + 10
        while time.monotonic() < until:
            current = focus()
            if PKG not in current:
                raise RuntimeError('Foreground lost during warmup: ' + current)
            time.sleep(0.6)
        pid = adb('shell', 'pidof', PKG + ':game')
        if not pid.isdigit():
            raise RuntimeError('No unique game pid: ' + pid)
        meta['pid'] = int(pid)
        (folder / 'render-before.log').write_text(log(), encoding='utf-8')
        (folder / 'native-before.log').write_text(app('cat files/native.log'), encoding='utf-8')
        meta['before'] = counters(pid)
        meta['clock_before'] = {'host_utc': datetime.now(timezone.utc).isoformat(), 'device_epoch': adb('shell', 'date', '+%s')}
        meta['start_utc'] = datetime.now(timezone.utc).isoformat()
        command = ['adb', 'shell', 'run-as', PKG, './files/simpleperf', 'record', '--in-app', '-p', pid, '-e', 'cpu-clock:u', '-f', '1000', '--call-graph', 'dwarf,16384', '--duration', '15', '--symfs', 'files/symbols', '-o', 'files/profile/perf.data']
        meta['record_command'] = command
        record_deadline = time.monotonic() + 60
        with (folder / 'record.log').open('wb') as stream:
            recorder = subprocess.Popen(command, stdout=stream, stderr=stream)
            while recorder.poll() is None:
                current = focus()
                meta['focus'].append({'host_monotonic': time.monotonic(), 'window': current})
                if PKG not in current:
                    raise RuntimeError('Foreground lost during record: ' + current)
                if time.monotonic() > record_deadline:
                    raise RuntimeError('Recorder did not finish within 60 s')
                time.sleep(0.6)
        meta['record_exit'] = recorder.returncode
        meta['end_utc'] = datetime.now(timezone.utc).isoformat()
        meta['clock_after'] = {'host_utc': datetime.now(timezone.utc).isoformat(), 'device_epoch': adb('shell', 'date', '+%s')}
        meta['after'] = counters(pid)
        (folder / 'render-after.log').write_text(log(), encoding='utf-8')
        (folder / 'native-after.log').write_text(app('cat files/native.log'), encoding='utf-8')
        if recorder.returncode:
            raise RuntimeError('simpleperf failed')
        loss = re.search(r'Samples recorded: ([\d,]+)\. Samples lost: ([\d,]+)',
                         (folder / 'record.log').read_text())
        if not loss or int(loss[1].replace(',', '')) == 0 or int(loss[2].replace(',', '')):
            raise RuntimeError('Missing samples or nonzero sample loss')
        with (folder / 'perf.data').open('wb') as stream:
            subprocess.run(['adb', 'exec-out', 'run-as', PKG, 'cat', 'files/profile/perf.data'], stdout=stream, check=True)
        # Allow a slow four-view run to consume the entire fixture, bounded by both clocks.
        deadline = time.monotonic() + 600
        while not app('test -f files/profile/result.json && echo yes', check=False):
            if time.monotonic() > deadline:
                raise RuntimeError('No harness result within 600 s')
            current = focus()
            if PKG not in current:
                raise RuntimeError('Foreground lost before replay completion: ' + current)
            time.sleep(2)
        meta['harness'] = json.loads(app('cat files/profile/result.json'))
        (folder / 'native-errors.log').write_text(log(), encoding='utf-8')
        (folder / 'native.log').write_text(app('cat files/native.log'), encoding='utf-8')
        for name, options in [('self', ['--sort', 'comm,tid,dso,symbol']), ('children', ['--children', '--sort', 'comm,tid,dso,symbol']), ('threads', ['--sort', 'comm,tid'])]:
            with (folder / (name + '.csv')).open('wb') as stream:
                subprocess.run([str(PERF), 'report', '-i', str(folder / 'perf.data'), '--symdir', str(ROOT / 'build-android/native'), '--csv', '-n', '--print-event-count', *options], stdout=stream, check=True)
        result = meta['harness']
        replay = result.get('replay', {})
        expected_warp = dict(zip(('circuit', 'laps', 'car', 'players'), map(int, warp.split(':'))))
        expected_warp.update(mode=int(mode), applied=True, failed=False, requested=True)
        if any(result.get('warp', {}).get(key) != value for key, value in expected_warp.items()):
            raise RuntimeError('Harness warp does not match requested workload')
        if (result.get('exit_code') != 0 or result.get('outcome') != 'passed'
                or not replay.get('complete') or replay.get('failed')
                or replay.get('frames_consumed') != 1526
                or replay.get('guest_frames_verified') != 1526
                or result.get('loaded_circuit') != 1
                or result.get('max_state', 0) < 8
                or result.get('max_abs_player_speed', 0) < 10
                or result.get('swaps', 0) < 60):
            raise RuntimeError('Harness did not verify the complete workload')
        meta['capture_complete'] = True
        print(f'{workload}: captured; inspect loss counts and symbol coverage before acceptance', flush=True)
    except Exception as error:
        meta['accepted'] = False
        meta['error'] = str(error)
        (folder / 'native-errors.log').write_text(log(), encoding='utf-8')
        raise
    finally:
        try:
            if recorder and recorder.poll() is None:
                try:
                    app('pkill -INT -x simpleperf', check=False)
                finally:
                    try:
                        recorder.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        recorder.kill()
                        recorder.wait()
            adb('shell', 'am', 'force-stop', PKG)
        finally:
            write(folder / 'metadata.json', meta)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('workload', choices=['1p', '4p', 'both'])
    parser.add_argument('--simpleperf', type=Path, default=PERF,
                        help='Host simpleperf.exe from the Android NDK')
    args = parser.parse_args()
    PERF = args.simpleperf.resolve()
    if not PERF.is_file():
        parser.error('Set ANDROID_HOME or pass --simpleperf to the host executable')
    for workload in (['1p', '4p'] if args.workload == 'both' else [args.workload]):
        run(workload)
