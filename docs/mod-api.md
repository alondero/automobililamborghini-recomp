# Mod game interface v1

Status: **Experimental**. The USA game interface complements the runtime's
package loader, per-mod configuration and frontend UI exports. See
[Modding](modding.md) for installation and fixture commands.

[The guest header](../include/lambo/mod_api.h) and
[import stubs](../include/lambo/mod_imports.c) define the MIPS interface. Compile
the stubs once into a mod. `lambo_mod_api_version()` returns 1. Import functions
from `*`; native module exports are imported from `.`. Versioned names retain
their documented meaning when additional interfaces are introduced.

All calls and callbacks belong to the game thread. Do not call them from a
renderer, native worker or guest audio thread. Callbacks receive an isolated
register context; memory writes persist, register changes do not. Dispatch also
preserves the interrupted game's host floating-point environment. They run in
the manager's mod order and must return promptly. Do not retain stack pointers.
The runtime shares the floating-point environment between callbacks within one
event; handlers should restore any rounding mode they change before returning.

## Events

| Event | Arguments | Timing |
| --- | --- | --- |
| `recomp_on_init` | None | Once at the main game-thread entry (`BootLoadInitialAssets`, USA `0x80000564`), after code loading and heap initialization, before retail game initialization. Register extensions here. |
| `lambo_on_frame_v1` | Current game state | Each accepted top-level dispatch, after warp/reset/save-state handling and before retail logic. This is a logic boundary, not a rendered-frame or VI promise. |
| `lambo_on_race_start_v1` | None | First dispatch in state 8 after setup, including attract races; entrant handles have already been published. |
| `lambo_on_race_end_v1` | None | Before body-pool initialization or the first observed dispatch outside state 8; handles remain valid during the callback. |
| `lambo_on_racer_added_v1` | Handle, one-based entrant | An entrant first appears in the race mapping. |
| `lambo_on_racer_removed_v1` | Handle, one-based entrant | Before its extension memory is released and handle invalidated. |
| `recomp_on_load` | None | First dispatch after the SI bridge initializes the Controller Pak image from disk or formats a fresh image. |
| `recomp_on_save` | None | First dispatch after accepted Controller Pak RAM writes have been queued for persistence. Multiple block writes coalesce. This is not a disk-completion or whole-save-transaction event. |
| `lambo_on_state_restored_v1` | None | First dispatch after a developer RAM restore, after old race handles/extensions are invalidated and before fresh race discovery. |

Callbacks use the runtime's `.recomp_callback.*:<event>` section convention;
the [smoke fixture](../tests/mods/smoke/smoke.c) supplies working examples. Save
notifications are deferred from the SI bridge to this boundary so guest
callbacks run on the game thread. An exit before another dispatch need not
deliver a pending notification.
[Per-mod save-file extensions](https://github.com/alondero/automobililamborghini-recomp/issues/21)
are separate; per-mod configuration already persists difficulty/weather choices.

## Named data

`lambo_get_game_value_v1` returns the signed halfword selected by
`LamboGameValue`, or `INT32_MIN` for an unknown key. State, race mode, player
count, field count, race difficulty and menu track cursor are available. The
track cursor is a menu choice, not a universal active-circuit ID. Its meaning
depends on mode and player count; see [Track index](TRACK_INDEX.md).
For direct reads, pass [the data symbols](../lamborghini.datasyms.toml) in
`inputs.data_reference_syms_files`. These six read-only, volatile guest halfword symbols
are USA-specific. They are not host pointers. Their address/width evidence is
in [the vehicle schema](../src/lambo_vehicle.h),
[warp bridge](../src/lambo_warp.c) and [race fields](race-fields.md).

## Racer extensions

`lambo_get_racer_v1(entrant)` resolves one-based entrants 1-16 to opaque handles;
zero means absent. `lambo_get_racer_body_v1(handle)` returns the current guest
record address, or zero for a stale/invalid handle. The USA racer-to-body map is
the relocated s16 array at `0x80901100`; records use the documented `0x10C` stride
and car flag at `0x80910000`. Only current race entrants are represented.
This is the Lamborghini object-lifetime interface; props and arbitrary shared
pool allocations are outside its contract.

Register a private extension size with `lambo_register_racer_extension_v1` in
the init callback, then use `lambo_get_racer_extension_v1(handle, extension)`.
Registration returns a nonzero ID; lookup lazily allocates zeroed, word-aligned
guest memory. Repeated lookups preserve its contents for that entrant lifetime.
IDs belong to the importing mod. Invalid/foreign IDs, exhausted storage and
allocation failure return zero. Limits are 256 registrations across all mods
and 1-4096 requested bytes per extension per entrant. Registration during frame
notifications is rejected; registration does not allocate per-entrant memory.

Race exit, body-map changes, pool reset (`func_80011710`, USA `0x80010B10`) and
developer RAM restore release extension memory. Removed callbacks may still
read it; afterwards all retained pointers and handles are invalid. Handles
never recycle within the process. The interface observes race membership at
dispatch boundaries and invalidates immediately at pool reset; it is not an
allocator hook for transient props. RAM snapshots do not serialize extensions
or utility stores. Snapshot restore rebuilds racer extensions; stores continue
until destroyed or a fresh game boot.

### Pool-reset caller evidence

The USA reset routine has exactly three stock `jal` sites. The tracked
[diagnostic generator](../scripts/gen_mod_pool_trace.py) creates a separate
configuration with a `printf` hook immediately before each call; generated C
is rebuilt, not edited. The headless [scenario](../scenarios/mod-pool-callers.json)
uses `recordings/menu-to-race-smoke.jsonl`. It runs with the ordinary executable;
the diagnostic build is needed only to collect the dynamic reset counts.
The recorded run consumed 1272/1272 frames, reached state 8 and produced:

| Call site | Generated caller | Dispatcher states with a direct call path | Observed state at reset |
| --- | --- | --- | --- |
| `0x800062D8` | `boot_pad_apply_calibration` | 7 (race setup) | 7, once |
| `0x80037E1C` | `func_800389A4` | 5, 9, 11, 15 (menu initializers) | 5, once |
| `0x8003C1CC` | `func_8003CD84` | 5, 6, 9, 10, 11, 12, 13, 14, 15, 16 (menu/results setup and rebuilding) | 6, six times |

The state-to-function map comes from dispatcher `func_800028D0` and its USA
jump table at `0x8008D610`. `func_800389A4` is entered through the initializers
`func_80038BD8`, `func_80038C88`, `func_80038CD4` and `func_80038D20`.
`func_8003CD84` also has paths through `func_80038D6C` and the state-13 results
initializer `func_8004DCC0`. These are direct-call reachability sets, not a
measurement of every menu choice. The state-8 updater `func_800030F8` has no
stock direct-call path to a reset caller. No resets occurred after race entry
in the measured run. The native race-load branch in `func_80038D6C` writes
state 7 at USA `0x800385F4` and branches past the menu rebuild call.

[The ROM-backed regression](../tests/test_mod_pool_callers.py) checks the
three sites, the dispatcher jump-table mapping and the direct-call sets above.
[The trace summary](evidence/mod-pool-reset-usa.json) records the measurement
without ROM bytes. Diagnostic hooks were removed by regenerating from the
canonical configuration before the final build and package tests.

To regenerate the dynamic evidence on Windows after the normal build:

~~~text
python scripts/gen_mod_pool_trace.py --output build/mod-pool-trace.toml
build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp.exe build/mod-pool-trace.toml
cmake --build build --target lamborghini_modern
Copy-Item build/lamborghini_modern.exe build/lamborghini_modern_pool_trace.exe
python tools/run_game_scenario.py scenarios/mod-pool-callers.json --exe build/lamborghini_modern_pool_trace.exe
~~~

The runner prints its artifact directory. Pass that directory to
`python scripts/gen_mod_pool_trace.py --summarize ARTIFACT_DIRECTORY --output build/mod-pool-reset-usa.json`
to regenerate the summary. Timing counters such as VIs can vary between runs.
The summarizer rejects successful ordinary runs without diagnostic markers.
Restore the production executable afterwards:

~~~text
build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp.exe lamborghini.us.toml
cmake --build build --target lamborghini_modern
~~~

No observed caller runs inside the continuing state-8 race, so no state guard
was added. The reset hook still invalidates handles for every actual pool
reset. Retaining old extension pointers across a pool reset is outside the
contract. The v1 interface does not expose an operation to reset the pool.

## Utility stores

`lambo_store_create_v1` creates a u32-key/u32-value map, a u32 set, or a slot map.
Store and slot handles are opaque and never recycled within the process.
All stores belong to their importing mod. Each mod may hold 64 stores, each
with at most 4096 entries; destroy releases a store, and a fresh boot clears all.

`put` inserts or overwrites a map/set entry, returning 1 for new, 0 for existing
or -1 on failure. Sets store the value 1. Slot maps instead use `insert`, which
returns a fresh slot handle or zero on failure. `get` returns 1 and writes a
u32 through its word-aligned guest output pointer, 0 for an absent key (output
unchanged), or -1 for invalid ownership/pointer. `erase` returns 1/0 for
removed/absent or -1 for an invalid store; `destroy` returns 1 or -1. A stored
zero is distinguishable from an absent key. These containers provide data
utilities without depending on another game's `recomputil_*` ABI.

## Acceptance coverage

The host fixture exercises register isolation, event ordering, deferred save
notifications, pool resets, RAM restores, stale handles, ownership, allocation
failure, pointer checks and storage limits. The MIPS/native package fixture
exercises import binding, a named data relocation, all three store kinds, and
init/race/frame callbacks with extension memory. See
[the audit](mod-loader-audit.md) for execution evidence and platform limits.
