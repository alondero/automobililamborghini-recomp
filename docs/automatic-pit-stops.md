# Automatic pit-stops

Status: implemented; verified against the generated USA pit input routine.
Interactive pit-stop presentation and Android device behavior remain unverified.

Enable **Automatic pit-stops (refuelling and tyres)** in **Enhancements**.
It is off by default and is saved as `automatic_pit_stops` in `graphics.json`.
Changes apply immediately. Players still drive into the pits. The game operates
fuel pressure and tyre fitting, retaining the original service animations and
completion rules. The existing button to finish refuelling early still works.

## Guest boundary

The USA ROM SHA-256 used for this investigation is
`CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.
Runtime code addresses are ROM offsets minus `0xC00`, plus `0x80000000`.
`func_8006B470` starts at runtime `0x8006A870`. Its caller,
`func_800716B8`, supplies signed stick X/Y, buttons, previous buttons, and
one of two zero-based pit slots. These are pit presentation slots, not a
four-player controller array. The caller skips input processing while paused.

All hooks execute synchronously on the game thread inside this handler.
Its stack frame is `0x48` bytes. Saved X/Y are signed bytes at `sp+0x4B`
and `sp+0x4F`; the slot is an unsigned byte at `sp+0x5B`. The hooks only
replace the saved stick inputs. They never write the shared controller buffer,
buttons, fuel quantity, tyre condition, or game phase. Runtime `MEM_*` accessors
handle the word-swapped representation of big-endian guest memory.

| Runtime hook | Evidence and action |
| --- | --- |
| `0x8006AAD8` | Waiting stage tests Y >= 8. Supply 8 to start refuelling. |
| `0x8006AC8C` | Fuel stage subtracts 7, clamps to 0..63 and truncates after multiplying by 0.7. Choose the highest representable pressure with one extra unit of margin below the spill limit. |
| `0x8006B610` | Tyre stage has loaded its expected direction into `s0`. Supply X=-60, X=60, Y=60 or Y=-60 for directions 1, 2, 3 or 4 respectively. |

The fuel stage computes a signed 32-bit limit at `0x80229CA8 + slot*4`
as `44 - (fuel >> 7)`, where fuel is at `0x8020FCA4 + slot*4`.
Pressure is a signed 32-bit value at `0x8020FC9C + slot*4`. The original
code ramps it by at most +12/-6 per update. Pressure at or above the limit
can trigger a spill once the nozzle animation reaches frame 13. The margin
allows the limit to drop by one on the following update without spilling.
The original sine-shaped fuel delivery calculation and full-tank transition
at `fuel >> 6 >= 46` remain active.

Tyre progress is at `0x8020FBA0 + slot*4`, with a rotation index at
`0x8020FBB0 + slot*4` into the direction table at `0x8020FB90`.
The stock input handler advances one quarter turn per call and caps progress
at 16. Its alternative control scheme already advances automatically and
branches around the tyre hook.

The enabled flag is atomic because UI and game threads differ. Hooks retain
no host progress, so savestate loading and toggling need no synchronization
beyond that flag. Disabled hooks, slots outside 0..1, unrecognized directions,
and fuel limits outside 1..44 are no-ops. These fixed seams should eventually
be replaced by named decompiled pit-service interfaces.

## Verification

`lambo_automatic_pit_stops` extracts the complete freshly generated handler;
no ROM-derived code is committed. Sound calls are stubbed and the sine call
uses host `sin`. Fixtures exercise both pit slots, stock spill detection,
full-tank completion, all four tyres, conflicting stick input, live disable,
and invalid slots. This validates the input handler, not the whole animated
pit presentation. Config and frontend tests cover the off default, persistence,
reload, and live UI changes.

Run after building:

~~~text
ctest --test-dir build -R "lambo_(automatic_pit_stops|config_live_updates|frontend_settings)" --output-on-failure
~~~

Verification on 2026-09-25 used Windows, MinGW GCC 15.2.0, and changes based
on `6c5dc09`. `build.ps1` completed, then N64Recomp regenerated the checked-in
configuration and the game plus the three focused test targets built successfully.
All three CTests above passed. The 29-test Python suite, documentation checker,
and `git diff --check` passed. The headless command
`python tools/run_game_scenario.py scenarios/harness-smoke.json --artifacts-dir verify_pit_smoke`
passed with 1,275 VIs, 601 swaps, state 8 and 600/600 replay frames. This smoke
run checks ordinary driving with the default-off setting; the generated-handler
test supplies the pit-specific coverage. Local captures and reports are under
`verify_pit_smoke` (ignored). No interactive renderer or Android device check
was performed.
