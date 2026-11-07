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
