# Guest source runtime compatibility fixture

This developer-only package checks runtime hook regeneration for compiled guest
replacements. It calls both vehicle selectors, menu input and the empty-list
vehicle updater through their original symbols, requiring entry and return
hooks to execute once each. Synthetic guest fields are saved and restored around
the calls. Menu sensitivity 1.5 checks the guest helper and host double query.

The runner then warps into a race with partial analog throttle and brake,
requiring another updater entry/return and both read-only probe outputs. This
exercises active updater helpers and host query/observer calls after runtime
regeneration. Separate packages attempt ordinary replacements of each of the
four functions and must be rejected as base-recomp conflicts.

Build with the same Zig and pinned RecompModTool used by the port, then run
against the newly built executable:

~~~text
python scripts/build_guest_source_smoke.py --zig path/to/zig --mod-tool path/to/RecompModTool
python tools/check_guest_source_mods.py --exe build/lamborghini_modern.exe --rom "Automobili Lamborghini (USA).z64"
~~~

Packages and logs stay under `build/guest-source-smoke` and
`build/guest-source-mod-check`. The runner stages a temporary isolated profile,
rejects portable-marked executables, and never installs these packages in a real
player profile. Runtime checks complement the ROM parity tests; they do not
establish interactive menu feel, visual correctness or Android behavior.

## Recorded result

On 2026-11-07, all five cases passed against the rebuilt Windows/MinGW GCC 15.2
port using the USA ROM documented in the source-patch pages. Packages were built
with Zig 0.13.0 and the pinned RecompModTool. The hook case logged one successful
synthetic original-call/entry-return/helper check, then a successful active-race
updater hook check. Throttle and brake observer output confirmed analog samples
32768 and 16384, with the brake ramp reaching 4.0. The run reached 600 VIs,
272 swaps and race state 8, then exited normally.

Each ordinary replacement was rejected before the game thread started, naming
the expected USA entry: `0x8003E80C`, `0x8003E96C`, `0x80041BD4` and
`0x80019120`. Logs are generated locally under `build/guest-source-mod-check`.
The isolated temporary profiles were removed and no game process remained.


## Normal menu traversal

Build the separate read-only observer and run the no-warp replay:

```text
python scripts/build_guest_source_smoke.py --menu-walk-only --zig build/toolchain/zig-windows-x86_64-0.13.0/zig.exe --mod-tool ../../build/lib/N64ModernRuntime/librecomp/N64Recomp/RecompModTool.exe
python tools/check_guest_menu_walk.py --exe build/lamborghini_modern.exe
```

The checker stages the observer in a fresh artifact-local profile. The scenario
and recording are `scenarios/guest-menu-walk.json` and
`recordings/guest-menu-walk.jsonl`. Run this scenario through the checker: its
log expectations require the observer package. Neither warp nor state loading
is allowed. The observer has no init callback or synthetic calls, and only reads
game state at actual original-symbol entry/return hooks. Its halfword addresses
are the USA bindings verified in [vehicle selectors](../../../docs/guest-vehicle-select.md)
and [menu input](../../../docs/menu-input.md). It compiles as big-endian guest
MIPS, so ordinary halfword loads use guest byte order, not host RDRAM indexing.

The replay extends the existing menu-to-race recording at dispatch frame 290.
It moves from the car confirmation item onto the right arrow, presses A, moves
through the center onto the left arrow, presses A, returns to the center and
resumes confirmation. Each press lasts six dispatches and each release thirty.
The USA ROM item records at offsets `0x14A000`, `0x14A020`, `0x14A040` verify
center/right/left navigation and the arrow callbacks; selecting an arrow alone
does not activate a vehicle change.

On 2026-11-07 the rebuilt Windows executable passed: normal menu screens
`1 -> 2 -> 11 -> 13 -> 21 -> 31`, menu-handler return on car screen 13, right
selector `00 -> 01`, left selector `01 -> 00`, then race state 8 at VI 1779.
All 1488 replay frames were guest-verified; the run ended normally after 3425 VIs
and 1690 swaps. Both warp and state-load requested/applied flags were false.
Evidence is under `build/menu-walk/guest-menu-walk-x3_6s6m7` (stderr, managed
environment, replay, native and runner results). No game process remained.
`python -m unittest tests.test_game_scenario` also passed all 16 tests.
This is scripted headless dispatch evidence, not a visual or physical-controller
acceptance test.
