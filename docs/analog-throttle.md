# Analog throttle preflight

Issue [#128](https://github.com/alondero/automobililamborghini-recomp/issues/128)
adds a continuous controller source without changing the ROM's default digital
controls. This note records the guest-side seam before the native hook is added.

## Guest control path

`func_80019D20` is the per-vehicle race updater. Splat names it at
`0x80019D20`; the recompiler symbol begins at runtime address `0x80019120`.
The top-level race function, `func_800030F8`, calls it at runtime addresses
`0x800025E0` and `0x800025EC` after the game has entered the race path.

For a human-controlled vehicle, the block beginning at runtime `0x80019FBC`
loads the selected `OSContPad` record and tests `buttons & 0xA000` at
`0x80019FC8`. In N64 button notation this is A (`0x8000`) or Z (`0x2000`).
The two branches rejoin at runtime `0x8001A120`, after the original digital
update and before the remaining vehicle physics. A paired hook at `0x80019FBC`
captures the existing demand immediately before the stock branch, and the merge
hook moves it one stock-sized step toward the continuous target:

```toml
[[patches.hook]]
func = "func_80019D20"
before_vram = 0x80019FBC
text = "extern void lambo_analog_throttle_begin(uint8_t*); lambo_analog_throttle_begin(rdram);"

[[patches.hook]]
func = "func_80019D20"
before_vram = 0x8001A120
text = "extern void lambo_analog_throttle_apply(uint8_t*); lambo_analog_throttle_apply(rdram);"
```

Digital mode makes both hooks no-ops, preserving the original code exactly.
Analog mode replaces only the resulting demand value while retaining the ROM's
`+/-10` pedal ramp; downstream vehicle physics remains ROM code.

## Field, range, and indexing

The current vehicle index is the signed halfword at `D_80098398`. Vehicle
records begin at `0x800B69A8` and have a `0x10C`-byte stride. The throttle
demand is the signed halfword at vehicle offset `0xAA`:

```
throttle = s16[0x800B69A8 + vehicle_index * 0x10C + 0xAA]
```

The updater reads the vehicle's one-based player/channel number from offset
`0x0E`, stores it in `D_800CE6AA`, and selects controller
`channel - 1`. `OSContPad` records begin at `D_800A39E0` with a six-byte stride,
which proves that player one maps to native port zero, player two to port one,
and so on.

The demand ceiling is another signed halfword:

```
limit = s16[D_800A5F2C + channel * 0x84]
```

The original pressed branch adds 10 and clamps to `limit`; the released branch
subtracts 10 and clamps to zero. The valid guest range is therefore
`[0, max(0, limit)]`. The native bridge scales its normalized `[0, 1]` sample
into this dynamic range, then uses the captured pre-branch value to move at the
same `+/-10` rate toward that target. This retains the stock pedal smoothing
instead of assuming that every car or mode uses 100 or jumping directly to the
target.

The signed 32-bit vehicle field at offset `0x90` is the speed value passed to
the stock HUD speedometer. It is sampled by the deterministic probe as the
downstream acceleration metric.

## Runtime evidence

An ares GDB trace warped directly into a one-player race (state 7 to state 8)
and observed the following live mapping:

| Item | Observed value |
| --- | --- |
| Human vehicle index | 1 |
| Vehicle channel | 1 |
| Native controller port | 0 |
| `OSContPad` address | `0x800A39E0` |
| Throttle field address | `0x800B6B5E` |
| Limit address | `0x800A5FB0` |
| Limit value | 100 |
| Released demand samples | 0, 0 |

ares overwrites debugger-injected pad bytes from its virtual-pad state before
the updater consumes them. Held-input measurements therefore use the port's
deterministic `LAMBO_ANALOG_THROTTLE` override and the production atomic bridge,
with the game-thread hook tracing the live guest field:

At fixed human-vehicle update 180, after the starting countdown, the production
probe records:

| Native input | Live limit | Live demand | Speed |
| ---: | ---: | ---: | ---: |
| 0% | 90 | 0 | 0 |
| 25% | 75 | 19 | 6 |
| 50% | 75 | 38 | 13 |
| 75% | 75 | 56 | 38 |
| 100% | 75 | 75 | 46 |
| Analog mode + digital A fallback | 75 | 75 | 46 |
| Original Digital mode + held A | 75 | 75 | 46 |

`python tools/emu_instrumentation/probe_analog_throttle.py --exe
build/lamborghini_modern.exe` reproduces this table across fresh headless race
boots, asserts partial input accelerates less than full input, and requires
Analog 100%, analog-mode A fallback, and original Digital held-A to match within
deterministic tolerance. Its final no-warp run keeps analog throttle active while
pulsing digital A and requires the menu/title state machine to reach race state
8, proving confirm remains a digital edge outside the race-only hooks.

## Menu isolation

The hook is inside `func_80019D20`, which is reached from the race-only branch
of `func_800030F8`; it is not in the controller poll/edge decoder. It neither
changes `D_800A39E0` nor the held/pressed masks at `D_800A39F8` and
`D_800A3A00`. Menu confirmation therefore continues to consume the ROM's
digital A-button edge, including when driving throttle is configured as
analog.

## Native ownership

SDL/controller discovery and profile evaluation stay on the host input thread.
That thread publishes one normalized throttle snapshot per native port through
atomics. The game-thread hook reads only those atomics and RDRAM; it never calls
SDL. Disconnect retains the selected profile's mode and publishes a neutral
analog value, so it cannot fall back to the ROM's gradual digital ramp-down. At the hook, the continuous
sample is combined with the ROM-visible A/Z state using `max(analog, digital)`,
so controller A, keyboard X, scripted input, and the ROM's existing Z alternate
all remain full-throttle fallbacks without synthesizing menu button edges.
