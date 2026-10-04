# Ray-traced sunlight shadows

Status: experimental, opt-in, off by default. Measured on Windows, D3D12, NVIDIA
RTX 3080, through 2026-10-04. "Ray-traced shadows (experimental)" on the
Enhancements tab replaces the native car shadow with a ray-traced sun shadow
in validated one-player scenes. Every other scene, and every frame whose
shadow is pending, failed or unsupported, draws the original shadow instead.

Earlier groundwork, art provenance and material evidence live in
[RT provenance](rt-provenance.md) and
[presented RT material evidence](rt-material-evidence.md). The renderer side
is [patch 0030](../patches/0030-rt64-sun-shadow-receiver.patch).

## Player settings

| Key | Default | Effect |
| --- | --- | --- |
| `rt_shadows` | false | Ray-traced sunlight shadows. Off keeps the original car shadow. |
| `rt_shadow_rays` | 8 | Shadow quality: 4, 8 or 16 rays per pixel. |
| `rt_shadow_softness` | 0.5 | Size of the sun in degrees, 0 to 5. 0 gives hard edges. |

`LAMBO_RT_SHADOWS` overrides `rt_shadows` at launch. The Low hardware preset
turns the setting off. See [configuration](configuration.md#graphics-file).

## Where shadows are used

Ray-traced shadows need all of these. Anything else keeps the native shadow.

- One player, race state 8, in a player race mode on a validated circuit:
  time trial (mode 0) and single race (mode 2) on all six circuits; menu
  Arcade (mode 1) and mode 3 on Circuit 1 only.
- Any player-one car. Car 0 passed the full matrix; cars 1 to 23 passed on
  Circuit 1 time trial.
- The task's own lit-car key light matches the measured world key.
- Every presented draw is classified: caster, receiver, measured non-caster or
  the native overlay. One unclassified or rejected draw keeps the frame native.
- D3D12 with DXR 1.1 and Shader Model 6.5 inline ray queries.

Standard and high-precision colour (`hpfb_option`) and MSAA are all supported.

## How a frame is made

1. The game producer copies the task's scene identity (phase, circuit,
   players, race mode, car selectors and the native object table) before
   publishing the task. Renderer threads never read guest RAM.
2. HLE stages that identity with the settings in force before it processes
   the task. RT64 reports each Workload id as it assigns it, before any queue
   thread can see the Workload, and the staged identity binds to the task's
   first Workload only. A task that publishes no Workload, or a second one,
   lends its identity to nothing, so those Workloads stay native
   (`src/lambo_rt_production.cpp`, `src/lambo_rt_binding.h`).
3. On the queue thread, `admit_sun_shadow` reduces the Workload's presented
   draws and lights to typed inputs and classifies every draw
   (`src/lambo_rt_admission.cpp`). Its gate is `Admitted`, `UnsupportedScene`,
   `InvalidObjectIdentity`, `MissingLitCarKey` or `InvalidParameters`; an
   admitted result can still be incomplete.
4. Patch 0030's pass builds a shadow-only acceleration structure from the
   admitted casters, runs a receiver pass over admitted receivers, and at the
   native overlay's position composites the result instead of drawing the
   overlay. Overlays in one view are replaced all together or not at all.
5. The composite copies the native colour first and writes
   `min(native, shadowed)` to RGB only. A shadow can never brighten a pixel,
   and native alpha (VI coverage) is kept.

Any failure before the composite (incomplete admission, missing receiver,
acceleration-structure rejection, allocation or pipeline failure) leaves the
native overlay in place for that view.

## Caster and receiver policy

Details and measurements are in
[production caster policy](rt-material-evidence.md#production-caster-policy).

- Opaque solids cast: no blending, coverage or alpha test, opaque depth
  compare and update.
- Measured world cutouts (`OtherMode.L=0xCB023038`) are non-casters (reason 4).
  They discard about a third of their samples and have no proven ray coverage.
- The car glass casts as opaque glass. This is a maintainer decision to avoid
  window-shaped gaps in the shadow. The glass never receives.
- Measured tyre-trail decals are non-casters (reason 5). They are translucent
  and appear in Arcade races.
- Only prelit surfaces (road, walls, cabin) receive. Vertex-lit surfaces cast
  but do not receive, because attenuating them would also remove their
  ambient light.

## Native contrast

The native overlay core transmits about 0.51 on screen, after the VI. The
VI's display response is about `T_display = T_fb^0.47`, so the shadow
strength is 0.757 (framebuffer transmission 0.243). See
[native overlay contrast](rt-material-evidence.md#native-overlay-contrast).

## Validation

Each matrix case runs four windowed captures from the same stationary replay:
native, native with the exact overlay omitted, hard (0 degrees) and soft
(0.5 degrees, 8 rays). `tools/check_rt_shadow_capture.py` requires, at tasks
60, 300, 420 and 540:

- a ready replacement in the production log;
- no traced pixel brighter than the omitted control;
- for hard shadows, a darkest-pixel (p1) transmission within 0.03 of native.

No task is exempt. A task where any run drew with RT64's ubershader, while
specialised pipelines were still compiling, is reported as inconclusive and
the trace is rerun once; a second inconclusive result fails. Ubershader
output can differ by a few levels anywhere on screen. Results below were
rechecked with this rule; earlier reports exempted task 60, which hid 13
brightened pixels in one ubershader frame.

| Scenes | Colour | Result |
| --- | --- | --- |
| Circuits 1-6, time trial and single race, car 0, hard and soft | standard | 24/24 pass: 18 at first, and the six Circuit 2-4 single-race checks after a fresh rerun of those cases. Hard p1 within 0.0074; no brightened pixel. |
| Same matrix | high precision | 24/24 pass; all 364 draws per task flagged high precision; hard p1 within 0.0061. |
| Menu Arcade race (mode 1), Circuit 1, car 0, menu replay | standard | Hard and soft pass at all four tasks, with skid marks on screen; p1 within 0.0045. |
| Warped modes 1 and 3, Circuit 1, no sweep flag, hard and soft | standard | 4/4 pass with no ubershader frame; p1 within 0.0045; no brightened pixel. |
| Cars 1-23, Circuit 1 time trial, hard | standard | 23/23 pass; player-one model selector matched each request; p1 within 0.0045. |
| Car 23 after admitting every car, no sweep flag, hard | standard | Pass; every logged task ready with no gate line; p1 within 0.0045. |

The matrix tool (`tools/run_rt_shadow_matrix.py`) also checks the native
fallback and lifecycle on the final build:

| Scenario | Result |
| --- | --- |
| `rt-shadows-fault` (`LAMBO_RT_SHADOW_FAULT=as`) | Every task not ready ("acceleration structure rejected an admitted caster range"); all four sampled frames identical to a fresh native baseline (`--expect-native`). |
| `rt-shadows-resize` (1280x720 at replay frame 350) | Swapchain changes from 1600x900 to 1280x720; every logged task stays ready. |
| `rt-shadows-msaa` (MSAA 2x) | Every logged task ready. |
| `rt-shadows-menu-to-race` (default settings, no developer flags) | All 53 logged race tasks ready, with no gate or incomplete-admission line. Earlier builds stayed native: high-precision receivers were rejected, then mode 1 and trail decals blocked admission. |

These lifecycle runs, the menu run and Circuit 1 time-trial parity were
repeated after the provider began binding identity at Workload publication,
with the same results: every logged task ready (fault: every task native and
identical to a fresh native baseline), hard and soft p1 within 0.0045 and no
brightened pixel.

Warping into modes 1 and 3 always loads Circuit 1, whatever circuit is
requested. Modes 1 and 3 on other circuits stay native until they are
measured.

## GPU cost

GPU timestamps from the 24 standard-colour matrix traces, 1600x900, 8 rays,
car 0, excluding each run's first 120-sample window:

| Shadow | Build p50 (median, range) | Receiver and composite p50 (median, range) | Worst receiver p95 |
| --- | --- | --- | --- |
| Hard (0 degrees) | 0.49 ms (0.42-0.54) | 0.32 ms (0.24-0.81) | 1.06 ms |
| Soft (0.5 degrees) | 0.43 ms (0.36-0.48) | 1.93 ms (1.22-2.56) | 3.22 ms |

Receiver and composite p50 by resolution and quality, one run each of
`rt-shadows-hard` (Circuit 1 time trial, car 0) after
`LAMBO_TEST_RESIZE`, first window excluded:

| Swapchain | Hard, 8 rays | Soft, 4 rays | Soft, 8 rays | Soft, 16 rays |
| --- | --- | --- | --- | --- |
| 1600x900 | 0.27 ms | 0.97 ms | 2.16 ms | 3.98 ms |
| 1920x1080 | 0.47 ms | 1.91 ms | 2.37 ms | 5.80 ms |
| 2560x1440 | 0.84 ms | 3.07 ms | 4.91 ms | 6.71 ms |
| 3840x2160 | 2.01 ms | 5.05 ms | 6.34 ms | 10.80 ms |

The acceleration-structure build stayed between 0.30 and 0.70 ms in every
run; it depends on scene triangles, not resolution. The menu Arcade race
(2,139 shadow triangles against 1,403) measured about 3.1 ms receiver p50
with default soft settings at 1600x900. MSAA 2x hard measured 0.35-0.37 ms
in the earlier matrix and 1.0-1.5 ms on the final build. Run-to-run spread is
large on this one machine, so treat these as an order of magnitude. A second
GPU vendor is not yet measured.

## Logging

Each Workload logs one `[rt-shadow]` line when its state changes, and every
60 tasks: task, epoch, ready flag, reason, caster ranges, receiver draws,
overlays, triangles and acceleration-structure bytes. An incomplete
admission names up to four unclassified draws with reason, projection,
native object and material signature. Every 120 ready Workloads it logs
p50/p95 build and receiver microseconds. These are debug-level lines: run with
`--verbose` or `--lambo-debug` (the scenario runner passes `--verbose`).

## Reproduce

Use [BUILDING.md](../BUILDING.md) and its MinGW/Python-CMake PATH.

~~~powershell
./build.ps1 -PreserveSubmodules
ctest --test-dir build -R '^lambo_rt_' --output-on-failure
python tools/run_game_scenario.py scenarios/rt-shadows-hard.json
python tools/run_game_scenario.py scenarios/rt-shadows-fault.json
python tools/run_game_scenario.py scenarios/rt-shadows-resize.json
python tools/run_game_scenario.py scenarios/rt-shadows-msaa.json
python tools/run_game_scenario.py scenarios/rt-shadows-menu-to-race.json
python tools/run_rt_shadow_matrix.py
python tools/run_rt_shadow_matrix.py --hpfb On
~~~

The matrix writes reports and change maps under ignored `artifacts/`. For a
fault proof, pass the native and omitted runs plus the fault run to
`tools/check_rt_shadow_capture.py --expect-native`. `--sweep` (or
`LAMBO_RT_SHADOW_SWEEP=1`) lets unvalidated race modes reach the
ray-traced path for measurement; it is never a player setting.

## Native provenance and producer boundary

`func_80036854` (runtime `0x80035C54`) places the lens flare from the track's
authored bearing minus camera heading and a camera-derived vertical term. It
reads no physical sun. The world key light and its camera independence were
measured separately; see
[world light and camera independence](rt-provenance.md#world-light-and-camera-independence).
The vertical screen term is never used as a sun elevation.

The producer reads USA RAM through the runtime's word-swapped layout and never
writes it:

| Field | USA address/layout | Width and units |
| --- | --- | --- |
| Current task | `0x800A2BFC` | u32 guest pointer |
| Task arenas | `0x800BF240`, `0x800C6C90` | task pointers, stride `0x7A50` |
| Root list | task + `0x1C0` | exact physical, KSEG0 or KSEG1 root address |
| Phase | `0x800CE6AC` | s16 game state |
| Circuit | `0x800CE794` | s16 circuit index |
| Players | `0x800CE6A4` | s16 player count |
| Race mode | `0x800CE6B4` | s16; 0 time trial, 1 menu Arcade, 2 single race, 3 unidentified, 4 attract |
| Model cursors | `0x800CE7E8 + player * 2` | four s16 model selectors, 0 through 23 |
| Native objects | `0x800B69A8 + index * 0x10C` | flags u16 +0, list u32 +8, kind s16 +`0x0E`, parent s16 +`0x58`; 128 copied identities |

Phase, circuit, race mode, player count and model changes advance the scene
epoch and clear pending records. Save-state restore and renderer shutdown
invalidate them. The task-reuse fence protects producer storage until HLE
consumes it; see [sky ownership](sky-panorama.md).

## Not covered

- Cars 1 to 23 on circuits other than Circuit 1, two to four players, and
  modes 1 and 3 on Circuits 2-6.
- Vulkan, Metal and Android. Only D3D12 has been run.
- A second GPU vendor.
- Relighting and casters outside the camera view remain
  [follow-on phases](ray-tracing-plan.md#follow-on-phases).
- These are sampled scenes and replays, not a full-game playthrough.
