"""Bounded, isolated Circuit 4 wet race replay; captures are not benchmarks.

Example: python tools/benchmark_circuit4.py --weather build/jp-weather/lambo_jp_weather.nrm
Run baseline and candidate executables serially on the same machine. This does
not change the player's configuration. The replay is a fixed input workload,
not an assertion that a race was completed or that wall time is deterministic.
"""
import argparse, hashlib, json, pathlib, shutil, subprocess, sys, time, zipfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import run_game_scenario as harness

def completed_workload(result, updates):
    replay = result.get('replay', {})
    warp = result.get('warp', {})
    return (result.get('outcome') == 'passed' and result.get('reason') == 'replay_complete'
        and result.get('loaded_circuit') == 4
        and warp.get('applied') and warp.get('circuit') == 4 and warp.get('mode') == 2
        and replay.get('active') and replay.get('complete') and not replay.get('failed')
        and all(replay.get(key) == updates for key in
                ('frames_consumed', 'total_frames', 'guest_frames_verified')))

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--case', default='full', choices=['full','no-rt','no-rain','1440p','six-cars','no-msaa','bounded-distance','bounded-8-rays'])
    p.add_argument('--exe', type=pathlib.Path, default=ROOT / 'build/lamborghini_modern.exe')
    p.add_argument('--weather', type=pathlib.Path, required=True)
    p.add_argument('--seconds', type=int, default=180)
    p.add_argument('--capture', action='store_true')
    p.add_argument('--updates', type=int, default=900)
    a = p.parse_args()
    if a.updates <= 0 or a.seconds <= 0:
        p.error('updates and seconds must be positive')
    scenario = harness.load_scenario(ROOT / 'scenarios/circuit-4-time-trial-car-0.json')
    scenario.update(headless=False, warp='4:3:0:1', warp_mode=2, max_vis=15000)
    scenario.pop('capture', None)
    scenario['expect'].pop('capture', None)
    out = harness.create_artifacts(ROOT / 'artifacts/circuit4-perf', a.case)
    runtime = harness.stage_fixtures(scenario, out)
    replay_path = pathlib.Path(runtime['input']['replay'])
    records = [json.loads(line) for line in replay_path.read_text().splitlines()]
    trimmed, remaining = [], a.updates
    for record in records:
        if record.get('type') == 'end': break
        if record.get('type') == 'run':
            if remaining <= 0: break
            record['frames'] = min(record['frames'], remaining)
            remaining -= record['frames']
        trimmed.append(record)
    trimmed.append(dict(type='end',frames=a.updates-remaining))
    if remaining:
        p.error('requested more updates than the fixed replay contains')
    replay_path.write_text(''.join(json.dumps(record)+'\n' for record in trimmed))
    env = harness.build_environment(scenario, runtime, out)
    env.update(LAMBO_LAUNCHER='0', RT64_RENDER_COST_REPORT='1', LAMBO_LOG_DIR=str(out/'logs'))
    graphics = dict(api_option='D3D12', res_option='Auto', msaa_option='MSAA4X', rr_option='Manual', rr_manual_value=60,
        rt_shadows=True, rt_shadow_rays=16, rt_shadow_softness=0.5, texture_pack='', wm_option='Windowed',
        window_width=3840, window_height=2160, no_lod=True, no_lod_circuit=[True]*6, draw_distance=0.0,
        race_cars=16, show_launcher=False)
    style = 'Modern'
    if a.case == 'no-rt': graphics['rt_shadows'] = False
    if a.case == 'no-rain': style = 'Off'
    if a.case == '1440p': graphics.update(window_width=2560,window_height=1440)
    if a.case == 'six-cars': graphics['race_cars'] = 6
    if a.case == 'no-msaa': graphics['msaa_option'] = 'None'
    if a.case in ('bounded-distance', 'bounded-8-rays'):
        graphics.update(no_lod_circuit=[True,True,True,False,False,False], draw_distance=1.5)
    if a.case == 'bounded-8-rays': graphics['rt_shadow_rays'] = 8
    (out/'graphics.json').write_text(json.dumps(graphics,indent=2))
    profile = out/'user-config/LamborghiniRecomp'
    (profile/'mods').mkdir(parents=True)
    (profile/'mod_config').mkdir()
    package = a.weather
    shutil.copy2(package, profile/'mods/weather.nrm')
    with zipfile.ZipFile(package) as z: manifest=json.loads(z.read('mod.json'))
    if manifest['id'] != 'lambo_jp_weather':
        p.error('--weather must be the Lamborghini JP weather package')
    (profile/'mods.json').write_text(json.dumps(dict(enabled_mods=[manifest['id']],mod_order=[manifest['id']])))
    (profile/'mod_config/lambo_jp_weather.json').write_text(json.dumps(dict(mod_id=manifest['id'],mod_version=manifest['version'],storage={'weather.mode':'Wet','rain.style':style,'rain.intensity':'Heavy','rain.camera':'On'})))
    if a.capture:
        env['LAMBO_RT_SUN_PROBE']='1'
        env['LAMBO_RT_CAPTURE_DIR']=str(out/'tasks')
        env['LAMBO_RT_RENDER_CAPTURE_DIR']=str(out/'captures')
        env['LAMBO_RT_CAPTURE_KEEP_MSAA']='1'
    metadata = dict(case=a.case,exe=str(a.exe.resolve()),binary_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),graphics=graphics,started=time.time())
    metadata.update(updates=a.updates, capture=a.capture,
        replay_sha256=hashlib.sha256(replay_path.read_bytes()).hexdigest(),
        weather_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
        weather_storage={'weather.mode':'Wet','rain.style':style,'rain.intensity':'Heavy','rain.camera':'On'})
    (out/'metadata.json').write_text(json.dumps(metadata,indent=2))
    print(str(out),flush=True)
    with (out/'stdout.log').open('w') as stdout, (out/'stderr.log').open('w') as stderr:
        proc=subprocess.Popen([str(a.exe.resolve()),'--console','--verbose'],cwd=ROOT,env=env,stdout=stdout,stderr=stderr)
        print('PID',proc.pid,flush=True)
        try: metadata['exit_code']=proc.wait(timeout=a.seconds)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(); metadata['timeout']=True
    metadata['elapsed']=time.time()-metadata['started']
    result_path = out/'harness-result.json'
    metadata['harness_result'] = json.loads(result_path.read_text()) if result_path.exists() else {}
    metadata['valid'] = (metadata.get('exit_code') == 0 and not metadata.get('timeout')
        and completed_workload(metadata['harness_result'], a.updates))
    (out/'metadata.json').write_text(json.dumps(metadata,indent=2))
    for line in (out/'stdout.log').read_text(errors='replace').splitlines():
        if '[render-cost]' in line: print(line)
    for line in (out/'stderr.log').read_text(errors='replace').splitlines():
        if any(x in line for x in ['gpu_cost','[jp-weather]','playback','race_cars','[race-field]']): print(line)
    print(json.dumps(metadata))
    sys.exit(0 if metadata['valid'] else 1)


if __name__ == '__main__':
    main()
