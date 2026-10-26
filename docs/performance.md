# Lower-hardware performance audit

Status: source-confirmed opportunities, with inferred priorities. Audited from
commit `5471d89` on 2026-10-02. The first three changes are tracked by
[issue 264](https://github.com/alondero/automobililamborghini-recomp/issues/264).
This is not a measured minimum hardware specification.

The ranking considers expected benefit, how clearly the source establishes
the cost, and whether a change can be verified without changing game timing or
guest layouts. It does not claim measured game-wide speedups. A CPU saving
cannot improve a GPU-bound frame, and a graphics preset trades visual quality
for less work.

## Ten opportunities

| Rank | Opportunity | Evidence and benefit | Result / follow-up |
| --- | --- | --- | --- |
| 1 | Optional low-hardware preset | Defaults select window-scaled rendering, MSAA2X, display-rate presentation, expanded geometry and 1.5x draw distance. Reduce pixels, samples, presentations and scenery together. | Implemented in Graphics, with Apply/Discard. |
| 2 | Capture drawing/camera environment overrides at startup | `lambo_config.cpp` previously searched the environment at every `no_lod`, draw-distance, fog and camera read, and parsed numeric overrides repeatedly. Drawing invokes several of these getters inside loops. | Implemented; live JSON-backed settings retain their atomics. |
| 3 | Skip hidden settings synchronisation | `lambo_frontend.cpp::render` refreshed the whole settings schema on every presentation, including ordinary gameplay. This includes string construction/lookups, variants, and the player-name mutex. | Implemented; hidden gameplay inputs skip refresh too. Menu-opening events use the frontend's own resolver. |
| 4 | Cache synthesized visibility rows | `lambo_no_lod.cpp` builds a row on each viewport walk; the policy scans the track records again even when the camera segment has not changed. | [Issue 266](https://github.com/alondero/automobililamborghini-recomp/issues/266). Track reload, savestate, Track Lab and viewport invalidation are essential. |
| 5 | Reduce fog preparation work | The earlier renderer-side rewrite is now part of the shared HUD/fog producer walk. Active custom fog still made sparse correction-table lookups for unrelated commands. | A private correction bitmap skips unrelated lookups; authored-word tracking and restoration remain intact. [Issue 267](https://github.com/alondero/automobililamborghini-recomp/issues/267) records the original traversal proposal. |
| 6 | Control rendering against a frame budget | Auto resolution and Display interpolation follow output demands, including high-resolution/high-refresh displays. Original resolutions and Manual refresh already exist. | [Issue 268](https://github.com/alondero/automobililamborghini-recomp/issues/268). Measure CPU/GPU separately before adaptive resolution or a presentation ceiling. |
| 7 | Reduce Windows guest-memory commit further | Patch 0012 already avoids committing the 4 GiB reservation, but the runtime still commits a 512 MiB accessible prefix on Windows. This is commit charge, not a measured physical working set. | [Issue 269](https://github.com/alondero/automobililamborghini-recomp/issues/269). Audit runtime/mod allocations and guards before smaller or demand commitment. |
| 8 | Evaluate PGO and selective LTO | Release optimisation is already enabled. No root PGO/IPO workflow exists. Generated functions deliberately preserve patchable, distinct entrypoints for mods. | [Issue 270](https://github.com/alondero/automobililamborghini-recomp/issues/270). Train representative replays and prove hook identity and floating-point behavior. |
| 9 | Tune worker concurrency on low-core CPUs | RT64 chooses raster compiler workers from half the logical threads, ubershader workers from threads minus two, and texture workers from a quarter. Normal shader workers already use idle priority. | [Issue 271](https://github.com/alondero/automobililamborghini-recomp/issues/271). Measure contention, startup and audio underruns before reducing concurrency. |
| 10 | Tune texture replacement residency for integrated GPUs | RT64 budgets replacements at two-thirds dedicated VRAM with a 512 MiB minimum. Existing LRU eviction, streaming and low-mip behavior should be reused. | [Issue 272](https://github.com/alondero/automobililamborghini-recomp/issues/272). Optional packs need residency/upload measurements; overly small budgets can thrash. |

Compiler experiments should follow the
[GCC optimisation reference](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html).
Avoid enabling fast-math, native CPU instructions, or whole-program folding as
an unmeasured shortcut. Dependency changes remain reviewable patches compared
with upstream, as required by [Contributing](../CONTRIBUTING.md).

## Implemented behavior

Low hardware stages 2x original resolution, supersampling Off (1x), no MSAA,
original presentation rate, and standard framebuffer precision. Resetting
supersampling is an extra low-hardware choice beyond the original preset
specification; it replaces saved X2/X3/X4 choices, and Auto resolution ignores
the setting. Apply disables expanded geometry and multiplayer fog/sky matching,
and sets draw distance to 1.0. These four Enhancements values replace saved
choices and can be restored manually in that tab. Discard cancels both renderer
and scenery changes, including on a later unrelated Graphics save. Later
renderer edits are retained at Apply. The selector then resets to Keep current
choices; the normal configuration fields are persisted. Other Enhancement
settings, per-circuit preferences, camera, window, backend, controls and texture
packs are preserved.

The nine hot rendering/camera environment overrides are captured when
`load_and_apply_graphics` runs, before game/render threads start. That snapshot
is immutable during play. A test or a subsequent launch captures them again.
Reloading configuration while those threads run is unsupported, just as the
per-circuit arrays are startup-owned. Live setters still update the underlying
stored settings, and an active environment override retains precedence.

Only port settings synchronisation is skipped while hidden. The frontend's
draw/event processing still runs so queued context opens and prompts work.
Refresh happens before a requested open, before a queued Escape or mapped
controller menu press, and while a context captures input. Existing protection
for pending text/graphics edits is retained. The host and frontend dispatch use
the same read-only menu-event resolver, supplied by patch 0040. This retains
both controller bindings, the single-player profile and multiplayer device
profiles without copying the binding rules into the port. Repeated keydowns,
releases, axes and unrelated gameplay presses do not request hidden refreshes.
F1 and Android menu requests still use the explicit page-request path.
This addresses [issue 274](https://github.com/alondero/automobililamborghini-recomp/issues/274).

## Measurement and verification

The opt-in `lambo_config_benchmark` links the production getters and a fake
renderer publisher. It times 200,000 iterations of sixteen no-LOD reads plus
draw distance, fog scale and camera FOV: 3.8 million getter calls. The checksum
keeps the results observable. Startup and file writes are outside the timed
region. Its workload represents repeated configuration reads, not a measured
per-frame distribution.

Compare a baseline built from `5471d89:src/lambo_config.cpp` with the current
implementation using this same benchmark source, compiler and `-O3 -DNDEBUG`.
Use isolated matching JSON files, no override variables, and an idle machine.
Report multiple samples. The [testing guide](testing.md) gives the target and
run commands. Frame-time, energy and low-end hardware gains require separate
replay measurements.

The session used Windows, MinGW GCC 15.2.0, an AMD Ryzen 9 3900X
(12 cores / 24 logical processors), and RT64 D3D12. This is a development
workstation, not low-end hardware. Baseline and current benchmark builds used
`-std=c++20 -O3 -DNDEBUG`, the same benchmark source and supporting port files,
matching default JSON and no rendering/camera environment overrides. Samples
were interleaved after compilation and game runs stopped. Earlier samples taken
under compiler contention were discarded.

| Sample | Baseline getter time (ms) | Cached getter time (ms) |
| --- | --- | --- |
| 1 | 8858.19 | 7.4065 |
| 2 | 8467.58 | 8.0987 |
| 3 | 8510.56 | 6.8649 |
| Median | 8510.56 | 7.4065 |

All samples produced checksum `3.7e+06`. The large difference establishes the
cost of repeated environment lookup on this Windows CRT. It does not predict
the game-wide gain: actual getter frequency and the limiting CPU/GPU stage
have not been profiled. Linux/Android libraries and CPUs may behave differently.

Checks completed:

- `./build.ps1`: Release build passed; the supported scripts applied the
  existing dependency patches and freshly generated both ROM-derived outputs.
  A local four-job CMake compile pool kept the cold build within machine
  resources; no build-script or dependency behavior was changed.
- `ctest --test-dir build -R '^lambo_' --output-on-failure`: all 42 project
  tests passed. Dependency test suites were excluded; their existing timeout
  issue is tracked separately.
- `python -m unittest discover tests`: all 47 Python tests passed.
- `python tools/check_docs.py` and `git diff --check`: passed.
- `python tools/run_game_scenario.py scenarios/harness-smoke.json`: passed;
  1251 VIs, 601 swaps, state 8, all 600 replay frames consumed, capture and
  movement assertions passed.
- `python tools/run_game_scenario.py scenarios/frontend-rt64-smoke.json`:
  passed on D3D12; 1247 VIs, 601 swaps, state 8 and 600/600 replay frames.
- The same RT64 scenario with its isolated graphics fixture seeded to the
  Low hardware fields listed above passed: 1274 VIs, 601 swaps, state 8 and
  600/600 replay frames. This validates the resulting runtime configuration;
  preset selection/Apply/Discard and widget notifications are covered by the
  native frontend settings test.
- Independent standards and spec reviews found two UI details, both corrected
  before completion: programmatic preset edits now notify widgets, and queued
  remapped menu presses request refresh before the frontend processes them.
  Both rereviews passed with no remaining findings.

ROM-backed checks used the 4 MiB North American USA input with SHA-256
`CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.
No generated files, ROM data or private settings are committed.

Not verified: minimum hardware requirements, whole-game frame-time/energy
improvement, a played or visually inspected preset/menu session, four-player
performance, Linux, Vulkan or Android hardware. The smoke trace verifies
movement and replay completion, not a completed lap. The seven follow-up
issues require measurements before committing to their proposed optimisations.

## Tunnel ray-tracing follow-up

The October 2026 [tunnel investigation](rt-shadows.md#tunnel-performance-investigation-2026-10-25)
measures the receiver shader as the main cost in a paused 16-car tunnel scene.
It removes sunlight queries whose sky-faded contribution is already zero,
keeps the same sample quality and contact shadows, and compares traversal-first
acceleration structures. RT developer overrides are cached on first use and
disabled RT skips unused settings preparation. The linked evidence separates
GPU shadow-pass savings from whole-frame performance and records validation
and hardware limits.

## Gameplay CPU review (2026-10-25)

The review began at `127e9c5`. Two focused changes preserve the existing
features and their defaults:

- Hidden settings refresh uses the same menu-event query as frontend dispatch.
  The query owns controller-profile resolution and both menu bindings. The port
  keeps ownership of refresh timing, explicit page requests and pending edits.
- Fog preparation keeps its sparse authored-word table, with a private bitmap
  of corrected command slots. Non-fog commands consult the table only when their
  own slot has a correction. The index costs 128 KiB, retains no guest pointers,
  and resets with the table on memory changes or loads. Empty-table gameplay
  retains its original early exit. Fog policy, fixed-point scaling, shared-list
  idempotence, address-reuse invalidation and snapshot normalization are unchanged.

The native settings fixture has an opt-in measurement mode:

~~~text
build/lambo_frontend_settings_tests artifacts/settings-benchmark --benchmark
cmake --build build --target lambo_fog_benchmark
build/lambo_fog_benchmark
~~~

The first command compares the old and new hidden-refresh triggers for 10,000
unrelated gameplay keydowns using the production settings schema. The second
fixture submits 20 million non-fog commands with either an empty correction
table or one persistent fog correction. It isolates correction-table lookup
cost, without display-list traversal or rendering. Neither fixture measures
game-wide FPS. For fog comparisons, compile the same benchmark against the
matching `.h` and `.cpp` from `127e9c5`, with the baseline header first on the
include path, and the same compiler and `-O3 -DNDEBUG` options.

Windows measurements used MinGW GCC 15.2.0 and a Ryzen 9 3900X. Compilation
and game runs had stopped; baseline/current fog samples were interleaved.

| Workload | Baseline median (ms) | Current median (ms) |
| --- | --- | --- |
| 20 million non-fog commands, empty table | 73.55 | 69.90 |
| 20 million non-fog commands, custom fog | 119.47 | 72.27 |
| 10,000 unrelated keydowns, settings trigger | 13.32 | 0.024 |

Each median uses three samples. Custom-fog lookup time fell about 40% in this
fixture, or about 47 microseconds per synthetic 20,000-command frame. The
settings comparison performs 10,000 refreshes before and zero after; it does
not measure the controller-profile lookup or menu-opening frames. These are
small isolated CPU savings, not a measured whole-game speedup or a hardware
minimum. Neither change reduces geometry, fog quality, resolution or effects.

Verification on this checkout:

- `./build.ps1 -PreserveSubmodules`: Release build passed with freshly generated
  USA game and RSP output; no generated inputs or dependency pins changed.
- `cmake --build build --target lambo_host_tests lambo_fog_benchmark -j 4`
  and `ctest --test-dir build --output-on-failure`: all 73 CTest checks passed.
  This includes the frontend resolver, fog walk and fresh-process save/load
  regressions. The added index tests cover both end slots, adjacent slots,
  reused commands and reset.
- `python -m unittest discover tests`: 229 tests ran successfully, with ten
  prerequisite skips (four Japanese-ROM checks and six NumPy-dependent shadow
  capture checks).
- `python tools/check_docs.py`, `python tools/check_patch_series.py`,
  `python tools/check_branch.py --base origin/main --worktree`, and
  `git diff --check`: passed.
- `python tools/run_game_scenario.py scenarios/harness-smoke.json`: headless
  run passed with 600/600 replay frames, 601 swaps, movement and a capture.
- `python tools/run_game_scenario.py scenarios/frontend-rt64-smoke.json`:
  Windows D3D12 on an RTX 3080 passed with 600/600 replay frames and 601 swaps.
  The same scenario with its isolated graphics fixture seeded to
  `fog_scale: 0.5` and `widescreen_fog_match: false` also passed. The runner's
  fixture-staging step supplied these extra fields; the checked-in scenario
  schema does not expose fog controls.
- `python tools/run_game_scenario.py scenarios/repeated-reset.json`: D3D12
  menu/reset acceptance passed, including cancelling a reset and completing two
  resets in the same process. This exercises UI callbacks, not physical inputs.
- Independent standards and behavior reviews found no remaining findings.

ROM-backed checks used the USA input with SHA-256
`CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.
Generated output, ROM data, profiles and captures remain ignored. Logs and
scenario reports are local under `scratch/` and `artifacts/`. Physical
controller menu navigation, pixel equivalence, a full lap, four-player game
performance, Linux, Vulkan and Android were not verified in this review.

A separate synthetic visibility prototype compared uncached row construction
with validation of every segment's renderability and reuse of an unchanged row.
At 55 segments, one million uncached rows took 149 ms and validation/reuse took
72 ms, with equal checksums. That is about 0.15 versus 0.07 microseconds per
row on this workstation. This does not establish the cost across actual tracks,
nor implement all cache invalidations. The small absolute saving did not justify
adding a persistent visibility cache in this session. Full replay profiling
remains the next step for [issue 266](https://github.com/alondero/automobililamborghini-recomp/issues/266).

## Android frame pacing (2026-10-26)

The review began at `d1ab307`. It used a Samsung Galaxy Tab S5e (`SM-T720`,
Snapdragon 670, Adreno 615, Android 11) with Mesa Turnip v25.3.0-R11 and the
tester's own graphics settings. The attract race showed 20 frames per second.
The game targets 30, one frame every two vertical retraces (VIs).

The cause was frame pacing, not a lack of CPU or GPU capacity:

- The runtime reported RDP (N64 graphics processor) completion at the first
  retrace after the renderer finished. On hardware the RDP finishes during the
  retrace after submission, so the game starts its next frame two retraces
  later. On the tablet, RT64's display-list processing (`send_dl`) took about
  21 ms of wall time. It finished just after the next retrace, so completion
  waited for a third retrace: 20 frames per second.
- Only about 8 ms of that time was CPU work. The rest was waiting for the CPU:
  about 14 involuntary pre-emptions per frame. The game's boot thread
  (`BootLoadInitialAssets`) polls two message queues without blocking. On
  hardware nothing lower-priority may run there. Natively, the loop occupied a
  whole fast core and competed with the renderer.

Two changes keep the game's timing:

- Patch 0001's `DpCompletionPacer` still never completes a graphics task within
  the retrace that submitted it, so fast machines remain at 30 frames per
  second. Once that retrace has passed, a busy renderer completes within about a
  millisecond of finishing. Only the VI thread signals completion, so it stays
  after its retrace message; external messages from different threads are not
  delivered in order.
- A hook at the loop's back-edge (`0x80001CA4`) waits up to 1 ms for the next
  external message when both queues are empty. Only an interrupt-style message
  can fill either queue while the boot thread holds the guest CPU.

The idle hook alone made `send_dl` slower: the CPU governor lowered the fast
cores' clock and the renderer moved to the small cores. Pinning the graphics
thread to the fast cores made it worse. The pacing change gave the gain.

| Attract race, original settings | Before | After |
| --- | --- | --- |
| Presented frames per second | 20 | 29.3 to 30.0 |
| Frames taking two VIs | 1 of 105 | 156 of 157 |
| `send_dl` wall time (median) | 21.7 ms | 22.8 ms |
| `send_dl` thread CPU time (median) | 8.3 ms | 13.2 ms |
| Involuntary pre-emptions per `send_dl` (median) | 14 | 1 |
| GPU busy | not recorded | 57% to 77% |

`send_dl` still takes longer than one retrace (16.7 ms). The game reaches 30
frames per second because completion no longer waits for a retrace after the
renderer finishes. Thread CPU time rose because the fast cores now run at a
lower clock.

Measurements used SurfaceFlinger presentation times, temporary log points in
`events.cpp`, `simpleperf` and `atrace`. The log points are not committed.
The second attract track measured 29.3 frames per second; 16 of 127 frames took
three VIs. The GPU was then about 75% busy.

RT64 CPU time in `send_dl` on the tablet, from `simpleperf`:

| Work | Share of `send_dl` |
| --- | --- |
| Framebuffer synchronisation (`fullSync`) | 56% |
| TMEM loads replayed per draw (`loadBlockOperation`) | 19.6% |
| TMEM hashing (XXH3) | 11% |
| Triangle and vertex processing | about 15% |

A word-at-a-time TMEM (texture memory) copy for aligned, non-RGBA32, non-TLUT
loads matched the byte loop on 4,194,304 cases. It ran 2.4 times faster on the
tablet's Cortex-A75 in an isolated benchmark. It is not applied. Texture hashes
define texture-pack identity, so a hashing change must keep every value.

Verification on this checkout:

- Unit tests failed before the changes and passed after:
  `lambo_dp_completion_pacing` covers early, late and repeated completion;
  `lambo_main_loop_idle` covers both queues and the empty-queue wait.
- `./build.ps1` from reset submodules: Release build passed with the
  regenerated patch 0001 and freshly generated USA output. `ctest --test-dir
  build`: all 75 checks passed.
- `python -m unittest discover tests`: 235 tests ran successfully, with four
  Japanese-ROM skips. `python tools/check_docs.py`,
  `python tools/check_patch_series.py`,
  `python tools/check_branch.py --base origin/main --worktree` and
  `git diff --check`: passed.
- Desktop pacing is unchanged. `harness-smoke.json` passed with 1257 VIs for
  601 swaps, and `frontend-rt64-smoke.json` (D3D12) with 1246 VIs for 601
  swaps. Both match earlier runs at two VIs per frame.
- `scripts/build_android.py` from reset submodules built a debug APK without
  the log points. On the tablet the attract race presented 29.5 frames per
  second with the GPU 67% to 74% busy. A screenshot showed it rendering
  correctly.

Not verified: a full 16-car player race, four-player performance, other
Android devices, Linux and a measured energy saving. The Japanese
configuration's USA-input digest was updated with the generator's digest
function, because the Japanese ROM was not available. Hooks are not carried to
the Japanese configuration, so no other generated line changes.
