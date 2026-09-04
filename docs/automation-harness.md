# Automated game harness

The harness makes a real game run repeatable and bounded. It starts through the
normal race loader (or a local save-state), applies effective N64 input on game
updates, captures frames, and emits a JSON result that a script can assert on.
It does not replace physics, teleport the car along a route, or invent a second
simulation.

## Why input replay is the first driver

The repo already had four useful pieces: `LAMBO_WARP`, RDRAM save/load, the
headless software renderer, and fixed held/pulsed input. The alternatives were:

- Reuse the game's demo or opponent AI. This is the best prospective closed-loop
  driver, but the player/AI slot handoff and lap semantics have not yet been
  verified. Enabling it by guessed flags could test a different code path from a
  human-controlled race.
- Steer from screenshots or synthetic OS key events. That is wall-clock and
  focus dependent, Windows-specific in the existing helper, and too slow for a
  reliable 30 Hz feedback loop.
- Keep a save-state atlas. This is excellent for settled visual bug locations,
  but it neither drives through gameplay nor captures native thread state.
- Record the final guest-visible input. This exercises the normal controller,
  vehicle, physics, race, and rendering paths; works headlessly; and is small
  enough to ship and inspect. That is the implemented baseline.

Open-loop input can drift after intentional physics changes or collisions. Record
time-trial runs where possible. If that becomes limiting, the next step is to
measure the original AI/demo seam or add feedback against verified vehicle pose
and waypoint fields—not to hide divergence with teleports.

## Run a scenario

After building `lamborghini_modern`, run:

```sh
python tools/run_game_scenario.py scenarios/harness-smoke.json
```

Use `--exe PATH` for a non-default build and `--artifacts-dir DIR` to choose the
artifact parent. Each invocation gets a unique directory containing the original
scenario, a rerunnable `scenario-resolved.json`, copied replay/save fixtures with
SHA-256 hashes, the managed environment, stdout/stderr, the native
`harness-result.json`, and the runner's final `runner-result.json`. A wall-clock
timeout is only a deadlock backstop; input timing never uses it.

The smoke trace holds A after the starting countdown and then releases it. It
proves warp, game-clock replay, vehicle motion, rendering, clean EOF, and result
assertions; it is deliberately not presented as a completed lap.
`scenarios/harness-one-frame-a.json` is a shorter timing regression proving that
even a one-frame A press reaches the guest pad and a full dispatcher update.

A scenario can select these fields:

```json
{
  "schema": 1,
  "name": "circuit-1-lap",
  "headless": true,
  "warp": "1:1:0:1",
  "warp_mode": 0,
  "max_vis": 7200,
  "input": {
    "replay": "../recordings/circuit-1-time-trial.jsonl",
    "start_state": 8,
    "start_delay": 0,
    "exit_on_end": true
  },
  "capture": {
    "path": "circuit-1-last.bmp",
    "state": 8,
    "every": 300
  },
  "expect": {
    "max_state_at_least": 8,
    "replay_complete": true,
    "min_swaps": 60,
    "min_abs_player_speed": 10,
    "capture": true
  }
}
```

`warp` uses the existing `circuit:laps:car:players` syntax. `warp_mode: 0` is
time trial; omit it for the normal single-race mode (`2`). Relative replay and
save-state paths resolve beside the scenario file. Capture paths are always
relative to that invocation's unique artifact directory, so concurrent runs
cannot consume or overwrite each other's evidence. The runner clears inherited
input/warp/state variables and redirects graphics config and Controller Pak
storage into the same isolated directory. It also runs from copied replay/save
fixtures, so changing the source file during a run cannot change its evidence.
The strict scenario wrapper currently accepts only runtime-verified car `0`;
measure another car-select value before widening that validation.
The developer warp also accepts 1–2 laps for short tests even though the normal
single-race menu starts at 3; this is an explicit harness extension, not a claim
about menu options.

Choose either `warp` or `state_load` as the bootstrap for a scenario. Combining
them is rejected because the save-state hook runs after the warp hook and would
restore the warp's guest configuration in the same update, making the requested
track assertion ambiguous.

## Record a drive or lap

Launch a windowed time trial and drive it normally. PowerShell example:

```powershell
$env:LAMBO_WARP = '1:3:0:1'
$env:LAMBO_WARP_MODE = '0'
$env:LAMBO_INPUT_RECORD = 'recordings/circuit-1-time-trial.jsonl'
$env:LAMBO_INPUT_START_STATE = '8'
./build/lamborghini_modern.exe --console --verbose
```

Recording begins on the first top-level game update in state 8, so it includes
the race countdown. Close the game after the desired finish; the trace is
atomically published only after clean finalization. Consecutive identical input
frames are run-length encoded. Record one trace for each initial
track/car/mode/state combination that needs coverage.
Keep a short neutral pre-roll (the checked-in smoke uses one) and replay from
the same warp or settled save-state so the first button-edge history matches.

To record from an exact local fixture, set `LAMBO_STATE_LOAD` instead of a warp.
The save-state rule still applies: park and let asset
streaming settle before saving. An `.lstate` contains only the low 8 MiB of guest
RAM, not native thread, renderer, or audio state. Loading another state after
playback or recording begins is therefore reported as an explicit failure rather
than silently rewinding only half of the system.

## Direct record/replay controls

| Environment variable | Meaning |
|---|---|
| `LAMBO_INPUT_RECORD=path` | Record one effective-input trace; mutually exclusive with replay. |
| `LAMBO_INPUT_REPLAY=path` | Replay a validated trace exclusively on controller port 0. |
| `LAMBO_INPUT_START_STATE=n` | Arm until this top-level game state; default `8` (race). |
| `LAMBO_INPUT_START_DELAY=n` | Wait this many game-dispatch updates after reaching the state. |
| `LAMBO_INPUT_EXIT_ON_END=0/1` | Exit after replay EOF; default `1`. Input stays neutral at EOF. |
| `LAMBO_HARNESS_RESULT=path` | Atomically write the machine-readable final outcome. |

Scenario replay defaults to requiring EOF even when `exit_on_end` is false. Set
`expect.replay_complete` to false explicitly for a deliberately partial run; the
runner still requires that at least one trace frame reached a game update.

Playback replaces physical, held, and pulsed port-0 sources after it starts.
Before it starts, normal inputs still work so menus can be navigated. The UI
capture/release barrier pauses consumption and feeds neutral input while open.
The trace clock comes from `func_800028D0`, which runs once per game update
(about 30 Hz), rather than the 60 Hz VI callback or a controller poll that may
occur a variable number of times. Replay stages the decoded pad immediately
before the ROM's own held/pressed-mask derivation. The following dispatcher
verifies that pad, consumes it, and counts it only at the common epilogue. Thus
even a one-frame movie cannot finish before its button reaches a game update.

## Trace format

Traces are strict JSON Lines. All records are validated before the first replay
frame is exposed:

```jsonl
{"type":"header","format":"lambo-input-trace","version":1,"clock":"game-dispatch","ports":1}
{"type":"run","frames":90,"buttons":32768,"stick_x":0,"stick_y":0,"throttle_analog":false,"throttle":0,"brake_analog":false,"brake":0}
{"type":"end","frames":90}
```

`buttons` is the final 16-bit `OSContPad` mask; sticks are integers from -80 to
80. Pedal values are exact normalized integers from 0 to 65535 and have an
explicit analog-mode flag. The trailer total must equal the sum of positive run
lengths. Unknown fields, duplicate keys, bad ranges, unsupported versions, and
incomplete traces are rejected.

## Current oracles and limits

The native result reports process reason/status, VI and framebuffer-swap counts,
top-level state, the loader's live circuit, the applied warp or save-state
bootstrap, player-channel vehicle/speed, and replay/guest-pad progress. The
runner verifies requested warp values against both the cursor writes and the
track actually loaded, every consumed replay frame's guest-pad application, state 8,
EOF, sustained swaps, nonzero vehicle speed, and structurally complete renderer
BMP captures (including their declared byte size).

There is not yet a verified lap-counter/finish oracle. Trace EOF proves the
recorded control sequence ran, not that a lap completed. A real lap recording is
the right fixture for measuring the finish-line fields; only then should a lap
assertion be added. Headless software rendering exercises display-list output,
but RT64-specific shaders, presentation, and native UI still need an optional
windowed visual run.
