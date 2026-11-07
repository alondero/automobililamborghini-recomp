# Guest-source migration candidates

Status: **Confirmed** source inventory at `b5ae30c` (2026-11-07);
ranking and proposed interfaces are **Inferred**. This is the bounded audit for
[#393](https://github.com/alondero/automobililamborghini-recomp/issues/393).
No replacement was implemented or run. See the [bridge inventory](guest-bridge-inventory.md)
for the other architecture categories and exclusions.

This records the original audit. Subsequent implementation is documented in
[vehicle selection](guest-vehicle-select.md), [menu input](menu-input.md) and
[vehicle update](vehicle-source.md); those pages own current verification status.

## Ranking

Rank balances removable maintenance dependencies, behavior risk and the cost of
an independent oracle. Instruction counts below come from declared symbol sizes,
not a new proof of function boundaries.

| Rank | Complete replacement unit | Maintenance impact | Behavior risk / verification feasibility |
| --- | --- | --- | --- |
| 1 | Vehicle selection left/right, `func_8003F40C` and `func_8003F56C` | Remove four instruction-address/register overrides behind one existing boolean host query. | Lowest of these candidates: two 86-instruction declared bodies; complete-function synthetic-RAM tests already exist. ROM parity still required. |
| 2 | Menu input, `func_800427D4` | Remove four register substitutions; guest source owns both cursor velocity and synthesized direction input. | Medium: 1,054 declared instructions and broader menu state; current tests cover the scale helper, not complete handler parity. |
| 3 | Per-vehicle update, `func_80019D20` | Largest payoff: throttle capture/apply, brake merge and two steering hooks; host pedal history and guest writes can move into guest ownership. | High: 5,055 declared instructions, AI and physics paths, and cross-update brake state. Existing bridge tests are useful fixtures but not a full-function oracle. |

The selectors win on bounded, independently testable removal of fragile hooks.
Menu input is a better second step than rewriting the race updater. Pedals and
steering share the latter function: treating them as independent small complete
functions would invent boundaries that the current symbol table does not establish.

## Evidence convention

[The USA symbol table](../lamborghini.syms.toml) and
[generator](../scripts/gen_syms_toml.py) own current sizes and hook locations.
The linear USA mapping is ROM offset = runtime address - `0x80000000` + `0xC00`.
Ranges below are half-open. The existing [cheat](cheats.md),
[menu](menu-input.md), [throttle](analog-throttle.md), [brake](analog-brake.md)
and [steering](steering.md) notes record prior ROM/runtime evidence, including
USA SHA-256 `CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.
Those measurements were not repeated for this audit. Before implementation,
verify the selected bodies, incoming edges, calls and data references against
that ROM. Generated C is an oracle input, never verified decompiled source.

All fields below are big-endian guest data; host `MEM_*` access handles the
runtime representation. Compile recovered layouts for MIPS rather than casting
host structs over RDRAM. Unknown fields must remain unnamed.

## 1. Vehicle selector pair: recommended next migration

Owners: `func_8003F40C`, runtime `0x8003E80C..0x8003E964`, ROM
`0x3F40C..0x3F564`; `func_8003F56C`, runtime `0x8003E96C..0x8003EAC4`,
ROM `0x3F56C..0x3F6C4`. Each declared body is `0x158` bytes.

The game thread runs these handlers during vehicle selection. They read a
one-based selected-player s16 at `0x800CE6A6`, a model cursor s16 at
`0x800CE7E8 + (player-1)*2`, category s16 at the start of each 24-byte descriptor
at `0x8013D490 + model*24`, and availability s16 at `0x800985C0 + category*2`.
The cursor is a model index (0..23), not a category. Both handlers update the
selected cursor, retaining direction, wrapping and locked-category skipping.
Full write/call sets must be checked against the ROM before reconstruction.

Four [generator hooks](../scripts/gen_syms_toml.py) at `0x8003E8AC`,
`0x8003E954`, `0x8003EA0C`, `0x8003EAB4` replace t4/t3/t3/t9 respectively.
They call the existing [extra-vehicles query](../src/lambo_cheats_runtime.cpp).
Keep that host query as the interface: it returns a setting and owns no game
pointer. Guest source owns descriptor access, availability decisions and cursor
writes. Keep both original callable entries and runtime mod patch metadata.
Do not broaden the cheat into saved progression or reverse-track access.

[The current extractor](../tests/extract_cheat_funcs.py) retains both complete
generated functions with their injected hooks.
[The tests](../tests/test_cheats.cpp) cover four selected players, both
directions, all 24 cursors with the cheat, locked-category skipping, toggling,
and unchanged availability/progress. They use synthetic descriptor categories;
they do not prove the actual ROM table or every availability pattern.

Build a separate ROM oracle before omitting the production originals. Compare
stock behavior with the query off and the frozen four old substitutions with it
on, against the compiled guest replacement. Compare persistent RAM and ordered
external calls, excluding compiler stack scratch. Enumerate valid model/player
indices, wrap points and reachable availability states; retain progress and
unrelated-player sentinels. Establish the stock precondition for an all-locked
table before testing it, since a skip loop may not terminate. Verify the ROM
descriptor data, signed loads and narrowing, rather than trusting synthetic
fixtures alone. Run a menu walk, plus the headless driving smoke and mod
entry/return/conflict checks used by the [pit migration](automatic-pit-stops.md).

No related issue listed in #393 owns this reconstruction. #394 concerns future
extension contracts; the existing boolean query is sufficient here. #338 still
owns JP addresses and layouts.

## 2. Complete menu-input handler

Owner: `func_800427D4`, runtime `0x80041BD4..0x80042C4C`, ROM
`0x427D4..0x4384C`, declared size `0x1078`. It runs on the game thread in
pre-race menus. [The native scale helper](https://github.com/alondero/automobililamborghini-recomp/blob/b5ae30c/src/lambo_menu_stick.cpp) rewrites
t0/t1/t7/t6 at `0x80042500`, `0x800426E4`, `0x800428D4`, `0x8004294C`.

The input is an OSContPad record at `0x800A39E0 + channel*6`: u16 buttons and
s8 X/Y at +2/+3. Known output work halfwords are `0x8009873E` and `0x80098760`.
The velocity path deadzones by 7, clamps X/Y to +/-48 and +/-32, and advances
the cursor by velocity >> 3 per update. Separate X thresholds synthesize
direction bits at +/-51. The complete handler's remaining menu state and
callee dependencies need recovery; these four sites are not its full footprint.

A setting-only host query could supply sensitivity; guest source would scale
local input and perform all menu decisions without changing the shared pad.
Preserve rounding away from zero at half values, the enabled clamp to +/-127,
and the exact disabled return (including -128). Preserve the current per-site
setting sampling or explicitly test a deliberate snapshot policy change.
Dependencies include menu globals, input decoding and the host setting query.

[Helper tests](https://github.com/alondero/automobililamborghini-recomp/blob/b5ae30c/tests/test_menu_stick.cpp) and
[config tests](../tests/test_live_config.cpp) seed cases, but do not cover the
complete handler. A separate ROM-handler oracle must compare cursor, direction,
button-edge and other persistent state across stick boundaries, both axes,
channels, menu modes and toggling. Stub and record external calls consistently.
Test a real menu walk and verify race input remains unchanged. This is larger
than the selector pair despite removing the same number of hooks.

## 3. Complete per-vehicle update

Owner: `func_80019D20`, runtime `0x80019120..0x8001E01C`, ROM
`0x19D20..0x1EC1C`, declared size `0x4EFC`. Called from the race path of
`func_800030F8` on the game thread. SDL/profile sampling remains host-owned;
the existing atomics publish pedal mode and normalized demand per port.

Current semantic hooks are throttle before `0x80019FBC` and `0x8001A120`,
brake at `0x8001A9A0`, and steering at `0x8001AA90` and `0x8001B8E0`.
See [throttle](../src/lambo_analog_throttle.cpp),
[brake](../src/lambo_analog_brake.cpp) and [steering](../src/lambo_steering.cpp).
Current source uses relocated vehicles at `0x80910000`, stride `0x10C`, and
racer records at `0x80902000`, stride `0x84`; the historical native addresses
in pedal notes are not the current bindings. The index is s16 at `0x80098398`,
channel s16 at `0x800CE6AA`, selected pad pointer u32 at `0x800A39DC`.

Known state includes throttle s16 at vehicle +`0xAA`, limit s16 at
`0x80902044 + channel*0x84`, brake float at +`0xA0` (0..16 demand units),
latch s16 at +`0xAC`, steering demand/accumulator s16 at +`0xB2`/+`0xB4`,
and speed s32 at +`0x90` in game units. Throttle moves by 10 per update;
brake rises by 1 and falls immediately to target. Steering reads work s16
`0x80098694`, difficulty s16 `0x800CE79C`, and 62-entry s16 curves at
`0x80089174`/`0x800891F0`. Scheme 2 and non-player-one steering stay stock.

Dependencies include pressed/released brake helpers `func_80029DCC` and
`func_8002A070`, downstream physics `func_8001EC1C`, relocated tables, AI
paths and input settings. This is not a verified complete dependency list.
A host sample/settings interface can replace raw guest writes, but moving the
per-port brake ramp to guest state requires explicit ownership, reset and
savestate semantics. Replacing it with the already-overwritten guest brake
field would change behavior. Preserve digital A/Z/B fallback and inactive
vehicle handling. A smaller extraction needs proven live-in/live-out contracts;
it cannot be described as a complete-function replacement yet.

Use the existing [throttle](../tests/test_analog_throttle.cpp),
[brake](../tests/test_analog_brake.cpp) and [steering](../tests/test_steering.cpp)
fixtures plus deterministic pedal probes and steering replay. A future oracle
must run the complete original updater with frozen enhancements and relocations,
comparing multi-update state and calls across human/AI, four channels, schemes,
difficulty, pause/restart and toggles. Whole-function stock parity is absent
today. [#154](https://github.com/alondero/automobililamborghini-recomp/issues/154)
owns bridge deduplication, not this reconstruction; current source already
shares vehicle layout and input quantization, so re-check that issue's scope.

## Focused follow-up scope

Follow-up [#395: Replace USA vehicle-selection handlers with compiled guest C](https://github.com/alondero/automobililamborghini-recomp/issues/395).
Limit implementation to the two selector bodies, their four cheat
hooks, guest declarations/bindings, registration and parity tests. Require
ROM-verified boundaries and data, disabled/enabled oracle parity, preserved
unlocks, original entry compatibility and mod hook/conflict verification.
Use the pit source-patch build path. Keep menu sensitivity, timers, reverse
setup gates, vehicle physics, JP support and a general gameplay interface out
of this implementation. Interactive menu/Android checks must be reported
separately from synthetic parity. This audit recommends the issue; it does not
claim the reconstruction is already safe or complete.
