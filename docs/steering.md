# Steering response

Status: **Experimental**. Implemented with host tests and headless traces.
Physical-controller feel is **Unverified**: no playtest with a real stick has
been recorded.

The Driving tab offers two optional steering settings for player one. Both
defaults are the original game. Nothing changes until you move a setting.

## Player settings

| Setting | Default | What it does |
| --- | --- | --- |
| Direct steering response | Off | The car follows the stick at once, without the game's steering delay or its weaker response to small movements. |
| Game steering deadzone | 4 | Stick units the game ignores at center. 4 is the original game. |

These settings are global for player one and are stored with the Driving tab,
like the pedal settings. They are not part of a controller profile, because
the live race stick does not pass through per-controller profiles.

The settings act only inside the game's steering update for a human driver.
Players two to four keep the original steering.

## Why stock steering feels twitchy

Measured with `recordings/steering-sweep.jsonl` (Circuit 1, car 0, Novice,
held accelerator, warp `1:1:0:1`) and `LAMBO_STEERING_TRACE`.
Status: **Confirmed** for this run; other cars and Expert follow the same code
but were not swept.

The human steering chain in `func_80019D20` is:

1. raw pad stick X (±80 at full deflection) is stored to the work halfword
   `0x80098694` (`0x8001AA90`);
2. trim: `|raw| - 4`; below zero steers 0, the result is capped at 61;
3. response curve lookup: curve A `0x80089174` (Expert) or curve B
   `0x800891F0` (Novice), selected by race difficulty `0x800CE79C`
   (see [Car differences](CAR_DIFFERENCES.md#difficulty-dependent-steering-and-physics));
4. one of two follow-up paths chosen by the per-channel flag at
   `0x800A3A16`: a scaled path (`0x8001AD64`, multiplier `0x800CE790`) or a
   counter/speed suppression stage (`0x8001AE44`-`0x8001AF64`). Which path the
   sweep took was not instrumented (**Unverified**);
5. slew clamp at `0x8001B430`: the change from the accumulator `vehicle+0xB4`
   is limited to 4 per update on Novice and 7 on Expert (`0x80089170`);
6. at speed 61 and above (`vehicle+0x90`), a speed factor scales the value
   (`0x8001B8DC`, truncating);
7. the result is stored as steering demand `vehicle+0xB2`.

The curves are gentle and integer. Curve B reads 1 for the first six entries,
reaches 4 at entry 15, 12 at entry 36 and 25 at entry 61. Curve A tops out at
35. Both start at 1, not 0, so a stick exactly at the deadzone edge already
steers by one unit.

Observed authored response (one update is one game frame):

| Stick | Curve value | Stored demand | Notes |
| --- | --- | --- | --- |
| 6 | 1 | 0 | The speed factor truncates 1 to 0 (speed 89). Small input does nothing. |
| 12 | 2 | 1 | |
| 20 | 4 | 3 | |
| 40 | 12 | 3, 6, 9, then 9 | Slew of 4 per update, then speed scaling. |
| 40 to -40 | -12 | 7, 3, 0, -3, -7, -10, -11 | Seven updates to reverse. |
| 65 | 25 | 3, 6, 10, 13, 17, 20, 21, 22 | Eight updates to reach 22. |
| 65 to 0 | 0 | 18, 14, 11, 7, 4, 0 | Six updates to recenter. |

So small movements are lost, mid-range input appears late and then all at once,
and the delay grows with the size of the movement. A modern stick with a fine
center makes this easy to overshoot, which players describe as twitchy.

The slew limit values `[4, 7, ...]` are per difficulty, not per car; only
indices 0 and 1 are legal. A raw reshape that maps input inside the deadzone to
curve entry 0 would steer slightly at rest, because entry 0 is 1. This
implementation steers 0 there.

## How it works

| Hook | Address | Reads | Writes |
| --- | --- | --- | --- |
| `lambo_steering_raw` | `func_80019D20` before `0x8001AA90` | `$t2` raw stick | `$t2` only, when the game deadzone is not 4 |
| `lambo_steering_direct` | `func_80019D20` at `0x8001B8E0` | pad stick, `0x800CE79C`, curve tables | `0x80098694` and `vehicle+0xB4` (s16), direct mode only |

Both run on the game thread inside the race updater, which owns every value
they touch, and act only for channel 1 reading pad `0x800A39E0`, with an active
vehicle index (`0x80098398`) and a channel scheme (`0x800A3A10`) other than 2.
Scheme 2 takes a separate path (`0x8001B594`) without the curve and is left
stock.

- Game deadzone `D`: the raw register becomes 0 when `|raw| < D` and
  `sign * (|raw| - D + 4)` otherwise, so the ROM's own -4 trim leaves exactly
  `D` and the curve starts at entry 0 at the edge, as in the original game.
- Direct mode writes `sign * curve[selector][min(|raw| - D, 61)]` (0 inside the
  deadzone) to the work value and the accumulator. The game then stores it as
  demand in the same update, and the next update's slew clamp starts from the
  same value, so the authored slew and speed stages have no effect.

Direct mode keeps the game's curve, so its full lock is still 25 (Novice) or
35 (Expert). It does not make the car turn harder, only sooner.

Settings cross threads once: the Driving tab publishes them under a mutex and
the game thread copies them per call. All guest access is on the game thread.
These are fixed-address bridges into generated code. A decompiled, named
steering function with a settings parameter would replace both hooks.

### Settings storage

The Driving tab stores `steering_direct` and `steering_game_deadzone` in
`driving-controls.json` (defaults and ranges in
[Configuration](configuration.md#driving-controls-file)). Out-of-range values
are clamped. They share the Driving tab's file rather than `graphics.json` so
the steering settings have one owner beside the pedals and gyro steering.

Headless overrides win over the saved values:

| Variable | Format |
| --- | --- |
| `LAMBO_STEERING_DIRECT` | `1` or `0` |
| `LAMBO_STEERING_DEADZONE` | game deadzone, 0-20 |
| `LAMBO_STEERING_TRACE` | CSV path: one row per player-one update |

Trace columns: `update, raw, selector, index, curve, authored, demand,
accumulator, speed, direct`. `authored` is the game's value before direct mode,
`demand` is what is stored to `vehicle+0xB2`.

## Limits

- Player one only. The other ports are untouched.
- RecompFrontend's General-tab joystick deadzone still applies to the stick
  before the game reads it.
- A replay records the stick but not these settings. Replay a recording with
  the same game deadzone and direct-mode setting.
- Direct mode removes the game's own protection against sudden steering at
  speed. That is the point of the option, but it can make high-speed driving
  harder.

## Verification

- `lambo_steering_response` (`tests/test_steering.cpp`): settings defaults and
  clamping, overrides, the raw reshape over every stick value at the authored
  deadzone, deadzone edges, curve index trimming and capping, and both hooks
  against synthetic RAM (other players, scheme 2, a foreign pad and the
  inactive-vehicle sentinel stay stock).
- `lambo_frontend_settings` checks that both Driving-tab options default to the
  original game, survive a save and reload of `driving-controls.json`, and
  reach the game thread.
- Defaults are bit-identical: the sweep replay produced a byte-identical
  per-frame `LAMBO_CAR_TRACE` dump with the hooks at their defaults, with
  `LAMBO_STEERING_DIRECT=0 LAMBO_STEERING_DEADZONE=4`, and on a build of the
  base commit without the hooks. `scenarios/harness-smoke.json` passes.
- Direct mode: over the 591-update sweep, the stored demand equals the signed
  curve value for the current stick in the same update, every update, with
  game deadzones 4, 0 and 10. A centred stick stores 0. With deadzones 4 and 0
  the demand differs from stock in 207 updates; with deadzone 10 in 106. This
  was read from the steering trace at the demand store; the car trace record
  does not reach `vehicle+0xB2`, so it was not cross-checked there.
- Game deadzone 0 without direct mode: stick 6 now steers (curve 2, demand 1)
  where stock stored 0.
