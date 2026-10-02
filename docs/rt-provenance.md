# Circuit 1 RT input evidence

Status: experimental diagnostics, measured on Windows on 2026-10-02 after
PR #277 (baseline `e2a44ab`). Native light and car-overlay provenance are
established for the sampled Circuit 1 tasks. The subsequent
[presented-material milestone](rt-material-evidence.md) authenticates opaque
surface candidates and the overlay differential. **Visible receiver ownership
and ready replacement coverage remain unproved. Workload and raster receiver
integration are not implemented.** Relighting and offscreen submission remain follow-on
phases in the [plan](ray-tracing-plan.md).

## World light and camera independence

The native directional key used by lit car vertices is signed-byte
`(-11, 55, -101)`; fill is `(11, 45, 101)`. Normalizing the key gives
`(-0.095214, 0.476070, -0.874238)`. These are directions toward the light,
not ray travel directions. The native model matrix rotates the vertex normal;
the camera look-at matrix is multiplied into the projection stack. Moving that
projection does not rotate the native directional bank. RT64's
`RSP::resetExtended` initializes its extra projection rotation to identity;
the port's matrix-group commands select interpolation groups without setting
an extra view matrix. `RSP::setVertex` and `computeDirLight` in the pinned RT64
source preserve this model/light convention.

The captures separate the car's model and camera's affine projection multiply.
The flat overlay has constant world Y; car height and native camera up use Y.
X/Z are the horizontal plane. Matrices are N64 signed-integer/fractional Mtx
values in row-vector order; model scale is approximately 0.01 (quantized to
`0.0099945068359375`). These are renderer world units, not measured metres.
The key's planar bearing `atan2(x,z)` is about 186.216 degrees and its elevation
about 28.431 degrees. The authored flare bearing is 186 degrees. This agreement
supports selecting the independently observed native world key for a future
Circuit 1 policy; it does not turn the flare height into physical elevation or
establish a celestial sun position, emitter size, or other circuits' policy.

The stationary trace uses idle, C up/down/left/right, L and R without throttle.
Across tasks 60, 300, 420 and 540, player one's heading is 69, 249, 69 and 69
degrees. The corresponding captured view rotation changes; a heading-only
change with an unchanged view matrix is rejected. The entire first lit car-body
model matrix stays identical, including
translation `(-501.5499878, 18.1299896, 126)`. Key/fill vectors stay identical.
The two-player single-race trace produces the same result in both views:
player one turns 180 degrees while player two remains at 69 degrees. Each view
loads its own car light-copy address, with the same direction. This falsifies
a camera-following key for these captures. It is not multiplayer RT validation.

The moving trace additionally samples changing car poses at headings 69, 123,
217 and 230 degrees with the same raw key/fill directions. These sampled
observations do not establish every car orientation, FOV, circuit or game mode.
The first light can have full RGB `(241,254,153)` or a dim car copy
`(155,163,98)` without changing direction. Phase 1 will attenuate native output;
it will not introduce a second diffuse-light calculation.

## Exact overlay provenance and material counterexample

Native `func_80013328` constructs a child object with flags `0x42`, list
`0x8013D3C8`, zero local translation and the just-created car as parent
(`0x80012814..0x8001286C`). The world builder reads flags at `0x80009B1C`;
`flags & 0x44` selects the parent index at record + `0x58`. In the repeatable
scene, object 1 is the physical car (`flags=0x9`) and object 3 is this child.
The existing interpolation producer authenticates object/view groups:
`0x10000000 | ((viewport & 3) << 16) | object`.

The child submits 16 triangles under group `0x10010003` in view one and
`0x10020003` in view two. Its vertices lie on one Y plane just above the car's
base; its model translation follows the car. The captured child uses
render mode `C8104A50`, combiner `FC11FFFF/FFFFF238`, geometry mode `0x12005`
(unlit), and texture pointer `0x8013CED8`. These identify the native
car-parented flat overlay in these tasks. They are observations, not stable
asset addresses or a renderer suppression rule.

Separately, emitter `func_800165FC` (runtime `0x800159FC`) submits repeated
flat trail pieces using **the same `C8104A50` render mode**, another combiner
(`FCFFFFFF/FFFE7638`), primitive alpha and no car object group. Moving captures
show several pieces. Therefore a render-mode-only native-shadow filter would
remove unrelated geometry. Identity must include authenticated emitter,
object/parent, transform and native material state. A swapchain differential
and replacement-coverage check are still needed before suppressing the overlay.

## Receiver/caster findings and the remaining gate

The world object's segment records distinguish road (+4), walls (+8) and far
scenery (+12) child lists. Captured roads use `C8112230` with combiner
`FC26A004/1FFC93F8`, prelit geometry mode `0x12205`. Walls share that material
and also use `FC127FFF/FFFFF238`; scenery includes `CB023038` and geometry
modes `0x12205`/`0x10205`. Physical cars contain lit body/wheel draws as well
as unlit material sections. A physical object or segment slot does not make
all its draws opaque, a closed solid, or an eligible receiver.

The offline inspector enumerates candidate triangles conservatively. It
discloses unevaluated native culling, excludes the port's allocated panorama
extension, and does not implement texture/tile/alpha/depth/coverage, texture
replacement, effective fog composition, RT64 interpolation or final draw
selection. It **cannot admit geometry**. This native-only milestone left material
coverage, matching presented ranges and an overlay differential as prerequisites.
It publishes no receiver/caster list and removes no draw. The subsequent
[presented evidence](rt-material-evidence.md)
supersedes that native-only gate status; visible receiver ownership and ready
replacement coverage still prevent phase 1 integration.

## Capture and offline bridge contract

The producer still requires `LAMBO_RT_SUN_PROBE=1`. The additional camera hook
at the common camera epilogue `0x80035C40` captures after heading storage,
including views where the flare is skipped. Camera scratch slots 1/2 match
views 1/2 in the measured scenarios; this does not establish all modes' mapping.
Emitter entry/common-epilogue hooks bracket these runtime PCs:

| Emitter | Entry | End hook |
| --- | --- | --- |
| World/object builder | `0x80009AC0` | `0x8000E440` |
| Panorama | `0x8000F6D8` | `0x800102C4` |
| Trail lead | `0x800159FC` | `0x800164B4` |
| Particle/effect lead | `0x8000E468` | `0x8000F6C4` |

Cursor `0x800A39CC` is a u32 guest pointer, advanced in 8-byte commands by the
game producer. Spans must stay between task +`0x1C0` and +`0x79C0`; the remaining
arena bytes are scheduler fields, not display-list storage. Out-of-range,
unaligned, reversed, unmatched, duplicate-pending and over-budget spans mark
the record incomplete. At most 64 spans are retained. Task reuse, epoch and
one-time alias consumption follow the [existing bridge](rt-shadows.md#guest-bridge-contract).

`LAMBO_RT_CAPTURE_DIR` additionally writes four paired schema-2 JSON/8-MiB
word-swapped snapshots at task sequences 60/300/420/540. The game producer
copies native RAM at `0x80005728` in `func_80006018`, after the task descriptor
is complete and before `osSendMesg` at `0x80005734` publishes it. Mutable
object/segment globals can change with an older task outstanding; the arena
reuse fence alone would not authenticate those records. The immutable copy
travels with the matching task; HLE only writes owned bytes to disk. At most
two slot-owned copies (16 MiB) can be pending. Publication cannot overwrite an
existing copy. Epoch invalidation releases pending ownership; already consumed
values retain their own bytes. Renderer workers never read guest RAM. Write failures
are logged; a scenario expecting captures fails on missing, short, incomplete,
malformed or unsupported command data. This read-only developer diagnostic
does not change selection or allocate a shadow scene. These are native producer
observations before the HLE fog rewrite, not effective presented material state.
The inspector rejects the old consumer-live-RAM schema rather than accepting
it as task-owned evidence. Dumps contain game data
and must stay local under ignored `artifacts/`.

Offline annotation reads the same snapshot, never live RAM:

| Field | USA layout | Width/meaning |
| --- | --- | --- |
| Native object | `0x800B69A8 + index * 0x10C` | flags u16 +0, list u32 +8, parent s16 +`0x58`; observation budget 128 objects |
| Track header pointer | `0x80098238` | u32; header +4/+8 bound 20-byte PVS rows |
| Segment-record pointer | `0x800BF1D0` | u32; 64-byte records with u32 road/wall/scenery lists |
| Lights | F3DEX MoveMem pointer | RGB u8 +0..2; directional signed bytes +8..10; MoveWord supplies count, followed by ambient |
| Matrices/vertices | F3DEX command pointers | 64-byte Mtx; 16-byte Vtx with signed s16 XYZ |

u32 words are native little-endian; halfwords use offset XOR 2, bytes XOR 3.
All dereferences are bounded to the captured low 8 MiB. Header row count must
be integral and at most 256. List traversal is bounded to depth 16 and 200000
commands. Unknown geometry/state commands fail instead of silently establishing
eligibility. Lights/transforms are associated at vertex load, so later state
changes cannot relabel already loaded vertices as lit. Ambient padding is not
a directional light. Synthetic tests contain no ROM-derived geometry or RAM.

## Reproduce

Use the supported [build sequence](../BUILDING.md) and its MinGW DLL PATH.
Regenerate the TOML and game output after changing hooks. Run:

~~~powershell
python tools/run_game_scenario.py scenarios/rt-stationary-camera.json --timeout 120
python tools/run_game_scenario.py scenarios/rt-stationary-two-views.json --timeout 120
python tools/run_game_scenario.py scenarios/rt-sun-provenance.json --timeout 120
python tools/check_rt_sun_capture.py artifacts/game-scenarios/<stationary-run>/rt-tasks
python tools/inspect_rt_task.py artifacts/game-scenarios/<run>/rt-tasks/task-60.json --output artifacts/task-60-draws.json
python -m unittest discover tests -p test_rt_task_capture.py
~~~

Fresh producer-owned runs were `rt-stationary-camera-1zy1qkey`,
`rt-stationary-two-views-oywpvbbv` and `rt-sun-provenance-d60zgu4l`.
Each passed 600/600 verified guest input frames, 601 native swaps and four
complete task captures. Stationary-series comparisons passed for one and two
players. These prove task consumption and native observations, not RT pixels.
The USA ROM hash and dependency pins remain those in [rt-shadows.md](rt-shadows.md).
The supported build regenerated ignored game/RSP output; none was hand-edited
or committed. No dependency patch or player setting changed in this milestone.

The final build, all 45 project CTests, 58 Python tests, scoped Ruff checks and
documentation/whitespace checks passed. The default headless `harness-smoke`
run (`harness-smoke-ckkcgn8l`) also passed 600/600 frames and 601 swaps. Finish
review found and corrected producer ownership, scheduler-region bounds, full
RDP other-mode replacement and the missing view-rotation assertion. The earlier
schema-1 live-HLE snapshots are superseded and cannot pass the current inspector.
No new production GPU/shader, swapchain visual differential, performance,
Android or Vulkan shadow validation is claimed.
