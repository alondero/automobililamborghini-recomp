# Cheats

Status: **Experimental**. Native hooks and ROM-backed tests cover the supported
USA ROM. Interactive play and Android device behavior need separate verification.

Open settings and select **Cheats**. All names and descriptions are in English.
Changes apply without an Apply button. Cheats start off on each application
launch and are not stored in a settings file.

| Toggle | Effect | When disabled |
| --- | --- | --- |
| Extra vehicles | The next left/right vehicle selection can reach every model, including locked models. Works for every player. | Normal selection restrictions resume. An already selected vehicle remains selected. Earned unlocks are unchanged. |
| Infinite time | Stops the remaining countdown from decreasing at its next tick. It cannot revive an expired race. | Countdown resumes from the remaining time. |
| Freeze lap timers | Stops both time-trial/race and current-lap clocks for all players at their next tick. | Clocks resume. Existing cheated times are not undone. |

Normal game saves can retain results achieved with cheats. Turning a cheat off
does not undo a race result. Toggling while the game is paused takes effect when
the relevant game code next runs.

## Reference review

The references are Libretro database v1.9.13's
[USA list](https://git.libretro.com/libretro-assets/libretro-database/-/blob/v1.9.13/cht/Nintendo%20-%20Nintendo%2064/Automobili%20Lamborghini%20(USA).cht)
and [U list](https://git.libretro.com/libretro-assets/libretro-database/-/blob/v1.9.13/cht/Nintendo%20-%20Nintendo%2064/Automobili%20Lamborghini%20(U).cht).
The GitLab pages were unavailable during implementation; the same tagged files
were read from the [Libretro GitHub mirror](https://github.com/libretro/libretro-database/tree/v1.9.13/cht/Nintendo%20-%20Nintendo%2064).
They are research leads, not validated port addresses or an import format.

| Reference entries | Decision |
| --- | --- |
| Extra Vehicles, including repeat-code syntax in U | Implemented as a native selection-check override. No availability table or saved progress writes. |
| Infinite Time, Stop Timer, Timer Speed Modifier | Consolidated into Infinite time. Cancel the decrement rather than force a magic time or patch executable RAM. |
| Stop Lap Timer, Stop Time Trial/Lap Timer, Lap Timer Speed Modifier | Consolidated into Freeze lap timers, covering both timer banks. |
| Controller and stick activators | Conditional GameShark building blocks, not standalone gameplay cheats. Excluded. |
| Always 1st, Always 1st Place, Immer Erster | Deferred. The lists conflict: 0x800A5F30 is used by floating-point loads/stores in this USA ROM; 0x800A5F70 is read as a HUD halfword. HUD evidence alone does not establish a safe native ranking/result override. |
| 100 Points, Max Points, Points Modifier | Deferred pending score ownership and result/save validation. U supplies FFFF to a byte-write code; do not interpret that as a verified score maximum. |
| Unbegrenzt Zeit, Unendlich Runden, Lap Modifier, Start On Lap Modifier | Conflicting or unverified data targets. Deferred until lap progression and finish behavior are established. |
| No Race Time | Zeroing a countdown may end a race; not used as a substitute for freezing it. |
| Difficulty, speed units, arrows, back-markers, music/SFX volume, car modifiers | These are parameterized settings/editor codes rather than complete toggle cheats; some contain XXXX placeholders. No raw value editor is exposed. |
| Starting Timer Amount Modifier | Empty code. No implementation evidence. |

## Native boundary and evidence

ROM SHA-256: `cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c`.
Addresses below are runtime USA addresses, not the symbol names' historical
0xC00-shifted addresses. Regenerate using scripts/gen_syms_toml.py and N64Recomp;
never edit generated C. Hooks live in src/lambo_cheats_runtime.cpp, with the
generation inputs in scripts/gen_syms_toml.py and lamborghini.us.toml.

The UI publishes independent atomic flags. Hooks run on the game thread at the
original arithmetic/branch sites. No host thread accesses guest RAM. There are
no captured RAM snapshots to restore across races or developer save-state loads.
Disabled hooks leave the original registers and game behavior intact.

| Seam | Confirmed source behavior and override |
| --- | --- |
| BootLoadInitialAssets, before 0x80001558 | 0x8000154C loads signed seconds from 0x800CE76E into t8; 0x80001554 decrements into t3. Restore t3 from t8 before sign extension, the halfword store and warning/zero checks. The ROM still owns countdown mode and pause gates. |
| BootLoadInitialAssets, before 0x800015DC | The four-player loop increments the subsecond halfword at 0x80098768 + player*8 + 6 by two. Restore t3 from t8 before the store and rollover checks. |
| BootLoadInitialAssets, before 0x80001754 | The same loop updates the current-lap bank at 0x80098850 + player*8 + 6. Restore t5 from t7 before store/rollover. The existing per-player race gate remains intact. |
| func_8003F40C, before 0x8003E8AC and 0x8003E954 | Left selection loads the category's signed availability halfword from 0x800985C0 + category*2. Set the loaded branch operand (t4/t3) to one when enabled. |
| func_8003F56C, before 0x8003EA0C and 0x8003EAB4 | Equivalent right-selection operands are t3/t9. The ROM retains cursor bounds, wrapping, descriptor lookup and player selection. |

Guest halfwords use N64 byte order through MEM_H, not native host struct casts.
Clock subsecond fields count in sixtieths; the update adds two and rolls over at
60. These seams override register values instead of introducing new memory
writes. See [vehicle evidence](CAR_DIFFERENCES.md) for category availability.
The source-level replacement would be named timer-update and vehicle-availability
functions once those game routines are decompiled.

## Synthetic-RAM test layout

The host test calls the extracted USA game code with an 8 MiB synthetic RDRAM
array and the production cheat hooks. Its fixed setup addresses follow the
same runtime USA build and N64 big-endian halfword accesses as the seams above.

| Test address | Width and role in the fixture |
| --- | --- |
| `0x800CE6B0`, `0x800CE6B4` | Halfword dispatcher countdown/warning gates. Values 3 and 1 enter the timed-race path while bypassing the warning-sound branch. |
| `0x800CE76E`, `0x800CE772` | Countdown seconds and its 1/60-second accumulator. The accumulator starts at 58 to exercise rollover on resume. |
| `0x800A5FBC + player*0x84` | Per-player active race-clock gate halfword. Set to one so each synthetic player clock is updated. |
| `0x80098768 + player*8` | Eight-byte elapsed race/time-trial record; halfword seconds at `+4`, subsecond ticks at `+6`. |
| `0x80098850 + player*8` | Eight-byte elapsed current-lap record; halfword seconds at `+4`, subsecond ticks at `+6`. |
| `0x800CE808` | Halfword dispatcher pause gate. A paused call must leave the complete fixture unchanged. |
| `0x8013D490 + model*24` | 24-byte vehicle descriptor; category halfword at offset zero. |
| `0x800CE6A6` | One-based selected-player halfword read by the selector handler. |
| `0x800CE7E8 + player*2` | Halfword model cursor for zero-based player index 0-3. |
| `0x800985C0 + category*2` | Eight availability halfwords; categories 0 and 4 start unlocked in the fixture. |
| `0x800A4170` | Halfword progress-bit source for locked categories. A sentinel verifies the selector cheat does not change progression. |

The two dispatcher gate values are fixture inputs to known branches in the
extracted timer body; the test does not assign them broader gameplay meanings.
The selector data shapes and address provenance are also recorded in
[vehicle evidence](CAR_DIFFERENCES.md).

## Verification

`lambo_cheats` extracts the full timer block (including pause, mode, expiry,
rollover and both four-player loops) and both complete selector functions from
fresh generated output. It retains the actual injected hooks. Synthetic RAM
tests enabled/disabled behavior, four players, rollover boundaries, pause,
expired countdown, all 24 model cursors, both directions and preserved unlocks.
Extraction asserts each timer hook remains inside the extracted address region.
Only the countdown warning sound is stubbed. Generated ROM data is not committed.

`lambo_frontend_settings` checks the Cheats page, immediate enable/disable,
external refresh and absence of cheats.json persistence.

The zero-ROM `test_generation_hooks.py` check compares every checked-in hook
and instruction patch with the generator. Adding these hooks exposed stale
generator copies of existing sky, driving-assist, graphics-task-drain and Android
declaration-scope fixes. The generator now preserves the checked-in behavior.

Implementation validation on Windows with MinGW GCC 15.2: the game and both
focused CTest targets built successfully; both CTests and all 31 Python tests
passed. N64Recomp output was freshly generated from the ROM identified above.
The headless harness-smoke scenario passed with 601 swaps and all 600 replay
frames consumed. This scenario ran with cheats off and checks baseline gameplay,
not interactive toggling. Dependencies were reused from an existing patched
checkout for the manual CMake build; the full build.ps1 setup was not rerun.

Interactive follow-up: toggle each option mid-race or while in vehicle selection;
check pause/resume, a lap boundary, race completion and returning to menus.
The tests do not establish record validity or visually verify the menu.
