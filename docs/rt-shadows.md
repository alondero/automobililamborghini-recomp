# Ray-traced sunlight shadows

Status: experimental, and pre-alpha. It is opt-in and off by default, and the
user-facing setting is labelled "(pre-alpha)" because bugs and lighting issues
are still expected. Measured on Windows, D3D12, NVIDIA RTX 3080, through
2026-10-22. "Ray-traced shadows (pre-alpha)" on the Debug tab replaces the
native car shadow with a ray-traced sun shadow in one-player races. It is a
development feature, not a finished one, so it stays off the Enhancements tab.
A frame draws the original shadow only when the device cannot trace it or the
frame has nothing to trace; see [where shadows are used](#where-shadows-are-used).

Earlier groundwork, art provenance and material evidence live in
[RT provenance](rt-provenance.md) and
[presented RT material evidence](rt-material-evidence.md). The renderer side
is [patch 0030](../patches/0030-rt64-sun-shadow-receiver.patch), then
[patch 0038](../patches/0038-rt64-sun-shadow-robust-scene.patch).

## Player settings

All three options are on the **Debug** tab, below Developer mode.

| Key | Default | Effect |
| --- | --- | --- |
| `rt_shadows` | false | Ray-traced sunlight shadows. Off keeps the original car shadow. |
| `rt_shadow_rays` | 8 | Shadow quality: 4, 8 or 16 rays per pixel. |
| `rt_shadow_softness` | 0.5 | Size of the sun in degrees, 0 to 5. 0 gives hard edges. |

`LAMBO_RT_SHADOWS` overrides `rt_shadows` at launch. The Low hardware preset
turns the setting off. See [configuration](configuration.md#graphics-file).

## Where shadows are used

- One player, race state 8, on any of the six circuits, in any race mode:
  time trial, menu Arcade, single race, Championship and attract.
- D3D12 with DXR 1.1 and Shader Model 6.5 inline ray queries.

Standard and high-precision colour (`hpfb_option`) and MSAA are all handled by the code.
This build's runs cover time trial, single race and the menu-to-race path, standard
colour, and MSAA 2x. Championship, attract and high-precision colour have no run on
this build; the 0030 results below are their last evidence.

Each draw gets a robust default. A draw the policy cannot trace (an unknown
projection, a face range it cannot follow) neither casts nor receives, and the
rest of the frame still traces. Earlier builds kept the whole frame native for
one such draw, or whenever the car's key light was not the open-road sun. That
is why shadows came and went with track and viewpoint, and reverted to the
blob in tunnels; see [lap validation](#lap-validation).

The native shadow stays when the scene is not a one-player race, the copied
object table is invalid, the light parameters are invalid, no receiver is
visible, or the device cannot build or trace the scene.

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
   draws and lights to typed inputs, chooses the light and classifies every
   draw (`src/lambo_rt_admission.cpp`). Its gate is `Admitted`,
   `UnsupportedScene`, `InvalidObjectIdentity` or `InvalidParameters`.
4. The pass builds a shadow-only acceleration structure with three layers:
   presented static scenery, moving cars, and remembered static scenery that
   this view no longer presents
   ([casters outside the view](#casters-outside-the-view)).
5. A receiver pass re-draws the admitted receivers. Each pixel traces the
   light, the open sky and nearby cars
   ([light](#light-sun-sky-and-tunnel-lamps)).
6. At the native overlay's position the result is composited instead of the
   overlay. The composite copies the native colour first and writes
   `min(native, shadowed)` to RGB only. A shadow can never brighten a pixel,
   and native alpha (VI coverage) is kept. Overlays in one view are replaced
   all together or not at all.

Any failure before the composite (no receiver, acceleration-structure
rejection, allocation or pipeline failure) leaves the native overlay in place
for that view.

## Light: sun, sky and tunnel lamps

**Sun.** The world key light `(-11, 55, -101)` was measured on all six
circuits and does not move with the camera or car. The shadow strength is
0.757 ([native contrast](#native-contrast)). The in-car camera shows no lit
car, so the sun stays the light there.

**Open sky.** Twelve rays from each receiver measure how much of the upward
sky scenery leaves open within 30 renderer units. The prelit art bakes its own
lamps, so where less than half the sky is open (tunnels, underpasses) the sun
shadow fades out, to nothing at an eighth. The test uses drawn and
remembered scenery, never cars. A receiver facing straight down, such as a
ceiling, sees no sky.

**Tunnel lamps.** In tunnels the game lights the car with a different
directional light, `(-100, 80, 0)`, whose colour pulses as the car passes each
wall lamp. On Circuit 1, lap tasks 460 to 490, its luminance runs from about
0.25 to 0.55, against 0.61 for the open-road key. The car's shadow then
follows that light. Its strength is the sun strength scaled by the lamp's
brightness against the open-road key, and never more than the full strength.
Scenery already carries the lamps' light in its art, so only cars block it,
and the sky test does not apply. In the Circuit 6 cave the shadow falls
beside the car and pulses with the lamps. In the Circuit 1 tunnel it falls
ahead of the car, out of the chase view.

**Contact.** Moving cars also block ambient light within 1.6 units (strength
0.55), which keeps the car grounded where the sun is blocked or faded. The
car measures about 4.35 units long, 1.67 wide and 1.13 tall (Circuit 1 start
line, render capture).

## Casters outside the view

The game only draws what the camera sees. A shadow traced from drawn geometry
alone therefore changed with the camera. In the stationary replay, Circuit 1's
start line traces 1,403 triangles from the default view and 686 after the
camera turns; 484 of each are cars. A CPU test over render captures found
that 85 of 311 shadowed road points became sunlit when only the turned view's
casters were used.

`SunShadowStaticCache` (patch 0038) remembers static scenery across tasks:

- Each task's opaque, non-car casters are transformed to world space on the
  CPU with the game frame's own matrices.
- A triangle whose exact float bits are drawn in two tasks at most 2 apart is
  static. Moving geometry never repeats its bits, and cars are excluded by
  object identity.
- Remembered triangles that this task did not draw form the third layer.
  Drawn ones are traced from the drawn layer, so nothing is traced twice. The
  layer is rebuilt only when that set changes.
- Triangles more than 400 units from the camera and unseen for 300 tasks are
  evicted. At most 131,072 are kept, nearest first. A new scene epoch starts
  empty.

With the cache, the turned view traces 686 drawn plus 752 remembered
triangles, which covers all 919 static triangles of the default view. The CPU
cost is at most 0.7 ms per task (p95). Receiver GPU time is unchanged within
run-to-run spread; see [GPU cost](#gpu-cost). Tracing remembered and drawn
copies together had cost 39% more, which is why drawn triangles are left out
of the layer.

Debug view 4 (`LAMBO_RT_SHADOW_DEBUG=4`) traces every pixel with and without
remembered scenery. Across six laps it added shadow on walls and slopes whose
casters were off screen, for example a building face on Circuit 5. Ignoring
remembered hits closer than 8 units left that shadow in place, so it comes
from a distant caster rather than a copy of the surface itself. No receiver
lost shadow.

`LAMBO_RT_SHADOW_CACHE=0` turns the cache off. `2` traces only remembered
scenery in place of the drawn scenery, to check its coverage. Every Circuit 1
lap task stayed ready that way.

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
- Other translucent or unusual materials neither cast nor receive. Their
  rejection reasons are logged.

## Native contrast

The native overlay core transmits about 0.51 on screen, after the VI. The
VI's display response is about `T_display = T_fb^0.47`, so the shadow
strength is 0.757 (framebuffer transmission 0.243). See
[native overlay contrast](rt-material-evidence.md#native-overlay-contrast).

## Validation

### Lap validation

`tools/check_rt_shadow_lap.py` replays each circuit's recorded time-trial
lap (car 0, soft shadows, 8 rays) in a real window and counts race tasks
from the `lap_summary` log lines. Tunnels, bridges, camera turns and every
material on the lap are covered.

| Circuit | Start of 2026-10-22 | Final build |
| --- | --- | --- |
| 1 | 1,293/1,500 (86.2%) | 1,440/1,440 (100%) |
| 2 | 1,585/1,800 (88.1%) | 1,800/1,800 (100%) |
| 3 | 1,599/1,920 (83.3%) | 1,920/1,920 (100%) |
| 4 | 1,805/2,400 (75.2%) | 2,400/2,400 (100%) |
| 5 | 2,220/2,220 (100%) | 2,160/2,160 (100%) |
| 6 | 1,954/2,160 (90.5%) | 2,640/2,640 (100%) |

The start-of-day build logged no `lap_summary` line, so its counts are
rebuilt from fallback and state-change lines and cover a slightly different
span. Its fallbacks were the old missing-key gate (tunnels and in-car views)
and, on Circuit 4, incomplete admission. The final build also kept every task
ready with debug views 1 and 4 and with `LAMBO_RT_SHADOW_CACHE=2` on
Circuit 1.

### Lifecycle and fallback

Run on the final build with `tools/run_game_scenario.py`:

| Scenario | Result |
| --- | --- |
| `rt-shadows-hard` (stationary replay, hard shadows) | Pass; every logged task ready. |
| `rt-presented-materials` (render captures at tasks 60, 300, 420, 540) | Pass; every presented draw matches its shadow-policy record. |
| `rt-shadows-fault` (`LAMBO_RT_SHADOW_FAULT=as`) | Pass; every task not ready ("acceleration structure rejected an admitted caster range"), so the native shadow is drawn. |
| `rt-shadows-resize` (1280x720 at replay frame 350) | Pass; every logged task ready. |
| `rt-shadows-msaa` (MSAA 2x) | Pass; every logged task ready. |
| `rt-shadows-menu-to-race` (default settings, no developer flags) | Pass; every logged race task ready. |

### Stationary contrast matrix

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
output can differ by a few levels anywhere on screen.

Final build, 2026-10-22, with remembered scenery on:

| Scenes | Colour | Result |
| --- | --- | --- |
| Circuits 1-6, time trial and single race, car 0, hard and soft | standard | 24/24 pass. Circuits 1, 5 and 6 were first inconclusive at task 60 (ubershader draws while pipelines compiled) and passed on a rerun. Hard p1 within 0.0074 over all tasks; no brightened pixel and no ubershader draw in the passing runs. |

The high-precision colour matrix and the menu, warped-mode and car rows
below were not rerun on the final build.

These earlier results were measured on the patch 0030 build, through
2026-10-04:

| Scenes | Colour | Result |
| --- | --- | --- |
| Circuits 1-6, time trial and single race, car 0, hard and soft | standard | 24/24 pass. Hard p1 within 0.0074; no brightened pixel. |
| Same matrix | high precision | 24/24 pass; all 364 draws per task flagged high precision; hard p1 within 0.0061. |
| Menu Arcade race (mode 1), Circuit 1, car 0, menu replay | standard | Hard and soft pass at all four tasks, with skid marks on screen; p1 within 0.0045. |
| Warped modes 1 and 3, Circuit 1, hard and soft | standard | 4/4 pass; p1 within 0.0045; no brightened pixel. |
| Cars 1-23, Circuit 1 time trial, hard | standard | 23/23 pass; p1 within 0.0045. |

Warping into modes 1 and 3 always loads Circuit 1, whatever circuit is
requested, so modes 1 and 3 on other circuits have not been seen.

## GPU cost

Circuit 1 lap, 1600x900, soft shadows, 8 rays, alternating runs on the final
build, first 120-sample window excluded:

| Remembered scenery | Receiver and composite p50, mean of windows | Build p50, median |
| --- | --- | --- |
| Off (`LAMBO_RT_SHADOW_CACHE=0`) | 3.87 ms and 3.94 ms | 0.39 ms and 0.40 ms |
| On (default) | 3.87 ms and 3.85 ms | 0.39 ms and 0.41 ms |

The receiver pass now also traces the open sky and contact rays, and laps
show more receivers than the stationary start line. The start-of-day build
measured 5.65 ms median on the same lap, over the tasks it traced. Remembering
scenery costs at most 0.7 ms of CPU per task (p95, all six laps); the six
laps traced up to 2,730 remembered triangles. Runs with window screenshots
measured higher receiver times (a median near 5 ms in one run); that was not
isolated further, so treat it as unexplained.

The stationary figures below are from the patch 0030 build, before the sky
and contact terms. GPU timestamps from the 24 standard-colour matrix traces,
1600x900, 8 rays, car 0, excluding each run's first 120-sample window:

| Shadow | Build p50 (median, range) | Receiver and composite p50 (median, range) | Worst receiver p95 |
| --- | --- | --- | --- |
| Hard (0 degrees) | 0.49 ms (0.42-0.54) | 0.32 ms (0.24-0.81) | 1.06 ms |
| Soft (0.5 degrees) | 0.43 ms (0.36-0.48) | 1.93 ms (1.22-2.56) | 3.22 ms |

Receiver and composite p50 by resolution and quality, one run each of
`rt-shadows-hard` (Circuit 1 time trial, car 0) after
`LAMBO_TEST_RESIZE`, first window excluded, same build:

| Swapchain | Hard, 8 rays | Soft, 4 rays | Soft, 8 rays | Soft, 16 rays |
| --- | --- | --- | --- | --- |
| 1600x900 | 0.27 ms | 0.97 ms | 2.16 ms | 3.98 ms |
| 1920x1080 | 0.47 ms | 1.91 ms | 2.37 ms | 5.80 ms |
| 2560x1440 | 0.84 ms | 3.07 ms | 4.91 ms | 6.71 ms |
| 3840x2160 | 2.01 ms | 5.05 ms | 6.34 ms | 10.80 ms |

Run-to-run spread is large on this one machine, so treat these as an order of
magnitude. A second GPU vendor is not yet measured.

## Logging

These are debug-level `[rt-shadow]` lines: run with `--verbose` or
`--lambo-debug` (the scenario runner passes `--verbose`).

- `workload=...`: one line when a Workload's state changes, and every 60
  tasks. Task, epoch, ready flag, reason, caster ranges, receiver draws,
  overlays, drawn and remembered triangles, and acceleration-structure bytes.
- `lap_summary tasks=120 ready=N`: every 120 tasks, with a count per
  fallback reason.
- `task=N car_light dir=x,y,z rgb=r,g,b key=0|1`: each change of the light the
  game puts on the player's car. `key=0` with a light means tunnel-lamp mode.
- `gpu_cost`: every 120 ready Workloads, p50/p95 build and receiver
  microseconds, triangles, remembered triangles and their snapshot version,
  and the CPU microseconds spent remembering scenery.
- `task=N native: admission gate G`: a task that could not be admitted.

## Tuning and debug views

These are developer variables, not player settings; see
[configuration](configuration.md#environment-overrides).

| Variable | Default | Effect |
| --- | --- | --- |
| `LAMBO_RT_SHADOW_DEBUG` | 0 | 1 transmission, 2 open sky, 3 contact, 4 what remembered scenery changes. |
| `LAMBO_RT_SHADOW_CONTACT` | 0.55 | Contact shadow strength. |
| `LAMBO_RT_SHADOW_CONTACT_DISTANCE` | 1.6 | Contact reach, renderer units. |
| `LAMBO_RT_SHADOW_SKY_DISTANCE` | 30 | Open-sky reach; 0 turns the sky test off. |
| `LAMBO_RT_SHADOW_SKY_OPEN` | 0.5 | Open-sky fraction that keeps the full sun shadow. |
| `LAMBO_RT_SHADOW_CACHE` | 1 | Remembered scenery: 0 off, 1 on, 2 remembered scenery only. |

In debug view 4, grey receivers are unchanged. Cyan means remembered scenery
added shadow; red means it faded shadow through the sky test.

## Reproduce

Use [BUILDING.md](../BUILDING.md) and its MinGW/Python-CMake PATH.

~~~powershell
./build.ps1 -PreserveSubmodules
ctest --test-dir build -R '^lambo_rt_' --output-on-failure
python tools/check_rt_shadow_lap.py
python tools/check_rt_shadow_lap.py --circuits 5 --shots-every 2 --env LAMBO_RT_SHADOW_DEBUG=4
python tools/run_game_scenario.py scenarios/rt-shadows-hard.json
python tools/run_game_scenario.py scenarios/rt-presented-materials.json
python tools/run_game_scenario.py scenarios/rt-shadows-fault.json
python tools/run_game_scenario.py scenarios/rt-shadows-resize.json
python tools/run_game_scenario.py scenarios/rt-shadows-msaa.json
python tools/run_game_scenario.py scenarios/rt-shadows-menu-to-race.json
python tools/run_rt_shadow_matrix.py
python tools/run_rt_shadow_matrix.py --hpfb On
~~~

The GPU probe needs `-DLAMBO_RT_GPU_TESTS=ON`; see [testing](testing.md).
The lap and matrix tools write reports, logs and screenshots under ignored
`artifacts/`. For a fault proof, pass the native and omitted runs plus the
fault run to `tools/check_rt_shadow_capture.py --expect-native`.

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
| Race mode | `0x800CE6B4` | s16; 0 time trial, 1 menu Arcade, 2 single race, 3 Championship, 4 attract |
| Model cursors | `0x800CE7E8 + player * 2` | four s16 model selectors, 0 through 23 |
| Native objects | `0x80910000 + index * 0x10C` | flags u16 +0, list u32 +8, kind s16 +`0x0E`, parent s16 +`0x58`; 64 copied identities (see [race fields](race-fields.md)) |

Phase, circuit, race mode, player count and model changes advance the scene
epoch and clear pending records. Save-state restore and renderer shutdown
invalidate them. The task-reuse fence protects producer storage until HLE
consumes it; see [sky ownership](sky-panorama.md).

## Not covered

- Two to four players: those views keep the native shadow.
- Cars 1 to 23 on circuits other than Circuit 1, and modes 1 and 3 on
  Circuits 2-6. They take the traced path but have not been measured.
- Scenery no earlier view of the scene has drawn. The cache only remembers
  what the camera has seen, so the first view after a warp, reset or new race
  still misses casters behind it. Course-asset caster submission remains a
  [follow-on phase](ray-tracing-plan.md#follow-on-phases).
- Lamps lighting scenery: tunnel scenery keeps its baked art, and only the
  car's shadow follows the lamp light.
- Relighting, Vulkan, Metal and Android. Only D3D12 has been run.
- A second GPU vendor.
- These are sampled scenes and replays, not a full-game playthrough.
