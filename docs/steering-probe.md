# Steering probe

Issue [#139](https://github.com/alondero/automobililamborghini-recomp/issues/139)
adds an opt-in per-frame trace of the ROM's human steering path. It does not
change steering behavior.

## Capture

Build the port after regenerating `RecompiledFuncs/`, then run:

```powershell
python tools/emu_instrumentation/probe_steering.py `
  --scenario straight --car 0 --output steering-straight.csv

python tools/emu_instrumentation/probe_steering.py `
  --scenario hairpin --car 3 --output steering-hairpin.csv
```

Both scenarios warp to Circuit 1 and hold A. `straight` holds the stick at zero.
`hairpin` drives a deterministic full-lock sequence at speed: `0 -> +80 -> -80
-> 0`. The tool validates the trim/clamp result, selector-to-curve label, frame
continuity, and the live response to each step before writing CSV.

The checked-in captures are
[`steering-straight-car0.csv`](traces/steering-straight-car0.csv) and
[`steering-hairpin-car3.csv`](traces/steering-hairpin-car3.csv).

The CSV columns are:

```text
frame,vehicle,raw,trimmed,curved,demand,speed,selector,curve
```

`LAMBO_STEERING_PROBE=1` enables trace emission. `LAMBO_STEERING_SEQUENCE` is
the general harness input, encoded as comma-separated `start_vi:stick_x` steps.
Normal runs leave both variables unset, preserve the original steering
behaviour, and emit no trace.

## Guest path

All addresses below are runtime virtual addresses in `func_80019D20`:

| Stage | ROM operation | Probe hook |
| --- | --- | --- |
| Raw | `lb` selected `OSContPad + 2` at `0x8001AA8C` | `0x8001AA94` |
| Trim/clamp, positive | subtract 4, floor at 0, cap at 61 | `0x8001AC4C` |
| Trim/clamp, negative | negate, subtract 4, floor at 0, cap at 61 | `0x8001AD08` |
| Curve result | signed paths merge in work halfword `0x80098694` | `0x8001AD64` |
| Final demand | store work value to current vehicle `+0xB2` | `0x8001B910` |

The current vehicle is the signed halfword at `0x80098398`. Vehicle records
start at `0x800B69A8` and use a `0x10C` stride. Speed at vehicle `+0x90` is
included only as trace context.

The positive and negative trim hooks preserve the sign around the ROM's
magnitude lookup. The deadzone path skips both lookup hooks, so the common
curve hook records zero for both trimmed and curved values.

## Curve selector

Static translation shows the branch at `0x8001AC50`:

- selector nonzero: response table A at `0x80089174`
- selector zero: response table B at `0x800891F0`

The selector is the signed halfword at `0x800CE79C`. It is not a derived car
class. The ROM's state-7 race finalizer copies the exact car cursor at
`0x800CE7A4` into it.

This was verified in one ares v147 session with a two-byte write watchpoint on
`0x800CE79C`. The probe programmatically changed the car-cursor field that the
menu owns, let the ROM complete state 7, waited for the state-8 write, then made
the second choice. This isolates the finalizer copy; it does not claim to test
the menu's input navigation:

```text
car=0 stop={'signal': 5, 'kind': 'watch', 'addr': 0x800ce79c}
      cursor=0 selector=0 curve=B
transition stop={'signal': 5, 'kind': 'watch', 'addr': 0x800ce6ac} state=8
car=3 stop={'signal': 5, 'kind': 'watch', 'addr': 0x800ce79c}
      cursor=3 selector=3 curve=A
```

Reproduce it with:

```powershell
python tools/emu_instrumentation/probe_steering_selector.py `
  --ares path/to/ares.exe --cars 0 3
```

Therefore car cursor 0 uniquely uses curve B; cursor values 1 through 5 use
curve A. The earlier "suspected car class" interpretation is incorrect.

## Live findings

The captured straight trace contains 690 consecutive human-vehicle updates.
It reaches speed 183 with all four steering stages at zero and selector 0 /
curve B.

The scripted hairpin trace contains 618 updates and reaches speed 159. Its
full-lock transition around speed 111 is:

| Frame | Raw | Trimmed | Curved | Final demand | Speed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 494 | 0 | 0 | 0 | 0 | 111 |
| 495 | 80 | 61 | 35 | 0 | 111 |
| 496 | 80 | 61 | 35 | 1 | 112 |
| 497 | 80 | 61 | 35 | 2 | 113 |
| 514 | 80 | 61 | 35 | 18 | 109 |
| 515 | -80 | -61 | -35 | 17 | 110 |
| 516 | -80 | -61 | -35 | 16 | 110 |

This corrects the earlier static-analysis finding that no smoothing or rate
limit exists. Raw input, trim, and curve lookup do update immediately, but the
final demand does not. Downstream code beginning at `0x8001B430` compares the
new demand with the previous accumulator at vehicle `+0xB4`, clamps the delta
by `s16[0x80089170 + selector * 2]`, and stores the accumulator before the final
`+0xB2` write. Live car-3 data limits the final change to one unit per vehicle
update. A separate held `stick_x=53` car-0 trace starts `4, 8, 12, 16, 18`, so
the slew limit also varies with the exact car cursor.
