# Vehicle updater source replacement

Status: guest C implementation with 2,304 deterministic ROM-oracle parity cases.
This replaces the complete USA linked-object updater, not only the pedal blocks.

## Evidence and ownership

[guest/vehicle_update.c](../guest/vehicle_update.c) reconstructs
`func_80019D20`, runtime `0x80019120..0x8001E01C`, ROM
`0x19D20..0x1EC1C` (5,055 instructions). The complete body SHA-256 is
`03213bd6766ae072f5c6ef0e0797c552f5a1573ccc617fb0b1df4a089c4aad11`.
The USA ROM identity is
`CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.

The older disassembly splits this body at ROM `0x1E01C`; that fragment is not
a callable function. Both fragments' instruction words were compared with the
ROM, including the final return at runtime `0x8001E014`. The initial 15-entry
jump table was read from ROM data at runtime `0x8008D990`. A static control-flow
scan found two external direct calls, at `0x800025E0` and `0x800025EC`, both to
the function entry. Indirect callers are not established by that scan.
M2C assisted control-flow recovery; its unresolved type and byte-offset
expressions were replaced with explicit guest layouts. Tool output alone is
not proof of behavior. The independent ROM oracle is the behavioral check.

The function runs synchronously on the game thread. Its argument is the first
signed-halfword object index; `-1` is the empty-list sentinel. Each object links
to the next through +`0x06`. Human channels 1..4, AI channels 5..12 and authored
scenery channels 13/14 share this owner. Expanded racer records with flag bit
`0x08` and channel >=13 retain the port's AI dispatch substitution. It preserves
the original entry for indirect calls and runtime mod-hook regeneration.

[The guest layout header](../guest/vehicle_update.h) binds the existing relocated
vehicle, racer, camera and scenery tables. Vehicles have stride `0x10C` at
`0x80910000`; racers have stride `0x84` at `0x80902000`. These are big-endian MIPS
layouts, never host views of word-swapped RDRAM. Unknown fields retain offset
names. Signed loads, narrowing stores and the original float/double evaluation
steps are compatibility details, not opportunities to simplify arithmetic.

## Input and diagnostics

The host publishes controller input and steering settings. Guest code owns
throttle demand, brake demand/latch, steering work and accumulator updates.
`lambo_vehicle_input(port, kind)` returns a value, never a game pointer:

| Kind | Snapshot |
| --- | --- |
| 0, throttle | Bit 31 selects analog input, bit 30 requests diagnostics, low 16 bits are normalized demand 0..65535. |
| 1, brake | The same packed representation for brake demand. |
| 2, steering | Low byte is deadzone 0..20, bit 8 selects direct response. |

The throttle query remains at both the pre-digital capture and common merge,
preserving the existing sampling points. Brake rises by one demand unit per
update and drops immediately to its target; digital B hands ownership to the
stock branch. Throttle retains the ten-unit ramp and A/Z fallback. Player-one
steering retains the scheme-2 exclusion and the centered-stick special case,
including a configured deadzone of zero.

`lambo_vehicle_observe(kind, authored, snapshot)` only records diagnostics.
The host observer reads guest state on the game thread and writes no game RAM.
For steering, the pre-override work value and the settings snapshot are passed
explicitly so the existing CSV's authored/demand distinction is preserved.

## Brake history and snapshots

Four float32 ramp values occupy `0x80901440..0x80901450`. This reserved space is
inside the existing snapshotted race region, after race metadata at
`0x80901400` (current fields end at +`0x08`) and before racer storage at
`0x80902000`. The guest updates it on the game thread. It is not patch BSS.

Normal transitions preserve the old bridge's behavior, including its early
return in digital mode when diagnostics are off. A physical B fallback clears
the corresponding ramp. Entering a new race does not introduce a new reset.
Snapshot restore now restores this history with game RAM; previously the host
array survived a RAM restore. This deliberate lifecycle change makes repeated
restores deterministic. It is not a claim that developer snapshots serialize
the complete native runtime.

## Verification boundary

[The oracle builder](../tests/build_vehicle_reference.py) recompiles the full
ROM body independently with frozen old input helpers and the existing race
relocations. [The parity harness](../tests/test_vehicle_update_parity.cpp)
compares persistent RAM and ordered external calls. Compiler stack scratch and
the newly guest-owned ramp have separate treatment. Service stubs cannot prove
full downstream physics or presentation behavior, so the full build, driving
smoke, mod-hook compatibility and interactive checks must be reported separately.

The parity matrix passed 2,304 updates: 16 channels, 48 variants and three
successive updates, using one or three linked records. Cases include schemes
0..2, difficulty, button combinations, analog modes, deadzones 0/4/10/20,
negative speeds, throttle limits -1/0/10/100/32767, AI service states 0..3 and
racer status 0..2. Each update also compares all four guest ramp values with
the frozen bridge's host history. A separate source-only snapshot replay
restores guest RAM after advancing the brake ramp, then verifies the same
input reproduces the same updated RAM and ordered service calls.
The native publication tests now exercise the actual packed input interface;
they no longer test retired host functions that wrote guest state.

The historical [throttle](analog-throttle.md), [brake](analog-brake.md) and
[steering](steering.md) notes explain the measured behavior being preserved.
Their former instruction-hook addresses are provenance for the oracle, not the
production interface after this replacement.

Windows verification of this migration passed a full Release application build
(MinGW GCC 15.2, Zig 0.13), all ten focused CTests, and the normal driving
harness (`harness-smoke`: 1,368 VIs, 601 swaps, state 8, 600/600 replay frames).
All five runtime mod checks passed: original-symbol calls and entry/return
hooks, active-race input queries/observers, and four expected ordinary-replacement
conflicts. This is automated runtime evidence; no interactive visual or Android
validation was performed. Logs are local build artifacts, not checked-in evidence.
