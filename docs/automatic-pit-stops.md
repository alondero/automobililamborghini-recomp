# Automatic pit-stops

Enable **Automatic pit-stops (refuelling and tyres)** in **Enhancements**.
It is off by default and saved as `automatic_pit_stops` in `graphics.json`.
Players still drive into the pits. Assistance supplies fuel and tyre input;
the original pressure ramp, spill penalties, animations, completion rules and
button to finish refuelling early remain active. Changes apply on the next
pit update. Interactive presentation and Android device behavior are unverified.

## Source replacement

[guest/pit_stop.c](../guest/pit_stop.c) reconstructs the entire USA pit input
handler, `func_8006B470`: runtime `0x8006A870`, ROM `0x6B470..0x6CA74`
(1,409 instructions). The evidence is the USA ROM with SHA-256
`CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.
The earlier decomp project's disassembly supplied a starting point; its
instruction words were checked against this ROM. The committed C expresses
the recovered behavior; it is not a matching build of the original source.

The caller, `func_800716B8`, supplies signed stick X/Y, two unused button masks
and an unsigned pit slot. These are two presentation slots, not four controller
indices. The caller skips input processing while paused. The guest function
runs synchronously on the game thread and rejects slots outside 0..1.

Clang compiles the C to MIPS; LLD retains its relocations; the pinned N64Recomp
produces host code plus patch bytes and registration metadata under `build/guest/`.
See [Building](../BUILDING.md#guest-source-patches). The ROM configuration omits
the original handler and its instruction hooks. Registration preserves its
original entry address for indirect calls. Compiled patch sections supply the
bytes needed for runtime mod-hook regeneration and replacement conflict checks.

The only host interface is `lambo_get_automatic_pit_stops()`, returning the atomic
setting. It receives no game pointer and writes no guest memory. The three old
hooks reading stack offsets and register `s0` have been removed. No host progress
is retained, so toggling and savestate loading require no extra state reset.

## Guest layouts and behavior

[pit_stop.h](../guest/pit_stop.h) declares the recovered field widths and
structure strides; [symbols.ld](../guest/symbols.ld) holds USA addresses.
These declarations are compiled for big-endian MIPS and never cast over host
RDRAM. Unknown fields remain unnamed. Addresses and layouts remain ROM-specific;
this migration does not claim a Japanese replacement.

The vehicle and racer tables use this port's existing relocated layouts:
`racer_bodies` at `0x80901100` has 12 signed-halfword entries per record,
vehicles at `0x80910000` have stride `0x10C`, and racers at `0x80902000` have
stride `0x84`. The pit reads record zero's slot-plus-one vehicle map. Vehicle
category, channel, tyre coefficients and racer fuel have compile-time offset
assertions. Other bindings retain the original ROM addresses. Changes to the
port's race layouts must update these bindings and rerun parity tests.

Fuel quantity and pressure are signed 32-bit game units. Each update computes
the pressure limit as `44 - (fuel >> 7)`. Assistance selects the greatest
representable pressure with one extra unit below that limit, preserving the
original +12/-6 pressure ramp and sine-shaped delivery. Limits outside 1..44
leave physical input unchanged. Full fuel is `fuel >> 6 >= 46`.

Tyre assistance supplies one of four signed stick directions. The stock
quarter-turn counter, wrong-direction reset, timeout and sixteen-turn cap still
run. Unrecognized directions retain physical input. Control scheme 2 already
advances tyres automatically. Signed-byte narrowing and float/double rounding
in the recovered code are deliberate compatibility details.

## Verification

`lambo_pit_stop_parity` compares the compiled guest replacement against an
independent recompilation of the complete original ROM handler. The oracle
applies only the existing race-table relocations and, for assisted cases, the
three frozen old input substitutions. It is generated locally; no ROM code is
committed. Across 5,120 deterministic cases, both implementations must agree
on persistent game RAM and ordered service calls, including their arguments.
Cases cover both slots, all five stages, manual/assisted input, spills, button
completion, control schemes, tyre errors, difficulty and timing variation.
Caller/callee stack scratch is excluded because the compilers use different
frames. Sound calls are recorded stubs and sine uses host `sin`.

`lambo_automatic_pit_stops` additionally runs multi-update service sequences
for both slots, checking completion, no spills, conflicting physical input,
live disable and invalid slots. These tests validate service logic, not the
whole animated pit presentation.

The migration was also checked on Windows/MinGW with the full port build and
the headless driving smoke (600/600 replay frames). A local NRM fixture hooked
the pit function's entry and return, called it through the original symbol,
and verified the regenerated helper and host setting query. An ordinary mod
replacement was rejected as a base-patch conflict. These runtime checks used
the default-off setting and do not establish interactive or Android behavior.

~~~text
cmake --build build --target lambo_pit_stop_tests lambo_pit_parity_tests
ctest --test-dir build -R "lambo_(automatic_pit_stops|pit_stop_parity)" --output-on-failure
~~~
