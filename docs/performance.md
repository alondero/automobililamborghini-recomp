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
| 3 | Skip hidden settings synchronisation | `lambo_frontend.cpp::render` refreshed the whole settings schema on every presentation, including ordinary gameplay. This includes string construction/lookups, variants, and the player-name mutex. | Implemented; refresh before opening and while a context captures input. |
| 4 | Cache synthesized visibility rows | `lambo_no_lod.cpp` builds a row on each viewport walk; the policy scans the track records again even when the camera segment has not changed. | [Issue 266](https://github.com/alondero/automobililamborghini-recomp/issues/266). Track reload, savestate, Track Lab and viewport invalidation are essential. |
| 5 | Remove the separate active-fog display-list walk | `lambo_fog_widescreen.cpp::rewrite` walks commands and nested lists before RT64 walks them again. The normal 1P/2P identity path already skips this. | [Issue 267](https://github.com/alondero/automobililamborghini-recomp/issues/267). Target custom fog and 3P/4P; preserve segmented addressing and avoid cumulative scaling. |
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

Low hardware stages 2x original resolution, one supersampling step, no MSAA,
original presentation rate, and standard framebuffer precision. Apply disables
expanded geometry and multiplayer fog/sky matching, and sets draw distance to
1.0. Discard cancels both renderer and scenery changes. Later renderer edits
are retained at Apply. The selector then resets to Keep current choices; only
normal configuration fields are persisted. Existing defaults, per-circuit
preferences, camera, window, backend, controls and texture packs are preserved.

The nine hot rendering/camera environment overrides are captured when
`load_and_apply_graphics` runs, before game/render threads start. That snapshot
is immutable during play. A test or a subsequent launch captures them again.
Reloading configuration while those threads run is unsupported, just as the
per-circuit arrays are startup-owned. Live setters still update the underlying
stored settings, and an active environment override retains precedence.

Only port settings synchronisation is skipped while hidden. The frontend's
draw/event processing still runs so queued context opens and prompts work.
Refresh happens before a requested open, before queued key/controller presses
that can open settings through a remapped binding, and while a context captures input;
existing protection for pending text/graphics edits is retained.

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
