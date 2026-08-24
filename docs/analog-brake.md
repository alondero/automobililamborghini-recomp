# Analog brake

Issue [#152](https://github.com/alondero/automobililamborghini-recomp/issues/152).

Follow-up to the analog throttle work
([#128](https://github.com/alondero/automobililamborghini-recomp/issues/128),
`docs/analog-throttle.md`). A controller axis can be mapped to the brake pedal
in the controls mapper's DRIVING column; the native bridge drives the ROM's own
brake field continuously while leaving digital play untouched.

## Guest seam (measured, not assumed)

The stock digital path inside `func_80019D20`:

- Brake **demand** is the float at `vehicle + 0xA0`
  (`0x800B69A8 + vehicle_index * 0x10C + 0xA0`). While B (`0x4000`) is held,
  the stock code ramps it by **+1 per race update** up to a ceiling of
  **16.0**, and the not-B release handler snaps it straight back to **0**.
- The release handler (`func_8002A070`, vram tail `0x800295D8`) runs every
  B-free frame: it masks the halfword at `vehicle + 0xAC` down to bit 15
  (clearing the braking latch) and stores `0.0f` into the demand.
- The **physics pass** (`func_8001EC1C`, consumers at vram `0x8001F618` and
  `0x80022810`) applies deceleration of `demand * 10` per update — but only
  while the **signed** halfword at `vehicle + 0xAC` is `> 0`. The pressed
  branch latches bit 0 of that halfword while B is held.
- The throttle field (`+0xAA`, see `docs/analog-throttle.md`) is fully
  independent: holding both pedals works exactly as it does digitally.

Verified live with a gdb write-watchpoint on the guest demand word plus
per-frame field traces: partial demand holds its value and produces partial
deceleration; full demand pins the car exactly like held digital B.

## Hook placement matters

The throttle pair brackets the A/Z block (`before_vram 0x80019FBC` /
`0x8001A120`). The brake block lives **later** in the same updater
(`0x8001A690`–`0x8001A990`), so a hook at the throttle merge would be undone by
the release handler before physics ever reads the value. The brake bridge is a
single apply-only hook at the brake block's own merge:

```toml
[[patches.hook]]
func = "func_80019D20"
before_vram = 0x8001A9A0
text = "extern void lambo_analog_brake_apply(uint8_t*); lambo_analog_brake_apply(rdram);"
```

Both the pressed and released paths pass through `0x8001A9A0`, which then flows
into the physics pass — so the values we write are the values physics consumes.

## Host-side ramp

Because the ROM's handlers rewrite both fields every frame, the continuous
pedal ramp cannot live in guest memory. `src/lambo_analog_brake.cpp` keeps one
float per native port (game-thread-only) and writes the absolute demand plus
latch bit at the merge each frame. Rising demand follows the stock pressed
ramp (+1 per update); falling demand snaps straight to the target, translating
the stock release behaviour rather than inventing a decaying pedal. When the
host publishes digital mode — or physical B is down, which hands control to the
untouched stock path — the native ramp resets to neutral.

## Native ownership / isolation

Same split as the throttle bridge: SDL sampling and profile evaluation stay on
the host input thread and publish one normalized snapshot per port through
atomics; the game-thread hook reads only atomics and RDRAM. Digital mode makes
the hook a no-op. Controller B and keyboard C remain full-brake fallbacks via
the pad-word check, and the hook sits behind the race-only call chain, so menu
B-cancel edges are unaffected.

## Deterministic verification

- `tests/test_analog_brake.cpp` — deterministic RDRAM-backed bridge tests
  (ramp shape, ceiling, channel→port mapping, latch lifecycle, fallback).
- `python tools/emu_instrumentation/probe_analog_brake.py` — warped-race probe:
  asserts demand ramps to the normalized target, late-phase speed orders as
  `0% > 50% > 100%`, full analog matches held digital B within tolerance, and
  an A-confirm menu walk still reaches the race transition.

Measured reference table (warped race, A held):

| Brake input | Median demand | Median late-phase speed |
| ----------- | ------------- | ----------------------- |
| 0%          | 0.0           | 187                     |
| 50%         | 8.0           | ~6                      |
| 100%        | 16.0          | ~2                      |
| Digital B   | 16.0          | 0                       |

(50%/100% both pin the car near stationary under full throttle because the
ROM's deceleration term `demand * 10` dwarfs engine acceleration at low speed;
the ordering assertion is what the probe enforces.)
