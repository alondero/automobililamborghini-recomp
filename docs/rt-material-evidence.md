# Presented material and receiver-ownership evidence

Status: diagnostic milestone after PR #279 (`bd05651`), measured on Windows
through 2026-10-03. The D3D12 raster-owner diagnostic proves visible receiver
ownership for every overlay-affected filter tap in the measured one-player
scenes. Patch 0025 now attaches capture-only light and draw/face metadata to the
matching RT64 Workload. The sampled Workload is incomplete, so there is still no
production acceleration-structure, receiver or overlay-suppression path and no
player setting. Every game scene continues to use native shadows. Relighting and
offscreen caster submission remain follow-on phases.

Read [native provenance](rt-provenance.md), [GPU groundwork](rt-shadows.md)
and the [plan](ray-tracing-plan.md) with this page.

## Presented geometry and material evidence

Producer-owned schema-2 task snapshots supply native object, segment, emitter
and geometry identity. HLE installs value metadata before matching `fullSync`.
An audit requires exactly one published Workload per sampled task. The worker
requires native presentation weight 1; the present tuple requires that Workload,
one frame, successful swap and its native color target. Absolute counters can
differ between runs; each run authenticates its own tuple.

Patch 0022 records ranges after empty-scissor rejection and before the actual
draw: draw ID, original indices, test-Z status, effective material/shader state,
viewport and framebuffer uniforms. After the GPU fence, the port reads actual
world-position, screen-position, shaded-color and index buffers. GPU indices
must equal CPU indices. Native local corners, group, combiner, geometry and
OtherMode match the immutable task; GPU world corners agree within 0.005
renderer units. Observed maximum error: **0.0000457763671875**. OtherModeH's
unused low six bits differ because RT64 initializes them to ones. Test-Z
rewrites, raw rectangles, mixed/ambiguous groups and unauthenticated faces stay
excluded.

Native car/course paths disable alpha compare, coverage-times-alpha, framebuffer
alpha blending and force blending. They use pixel depth, opaque Z mode, depth
compare and depth writes. In pinned `RasterPS`/`Blender`, texture alpha cannot
discard or blend away their interiors, including replacement texture alpha
under these states. Raster edges and depth occlusion still apply. Cutouts and
translucent effects fail this rule.

Receiver candidates additionally require a measured two-cycle standard-fog
material, matching shader OtherMode, finite unit-range native fog color and
finite GPU shade/fog alpha. HDR, MSAA and raw-rectangle shader paths are excluded
from this bounded check. Observed supported command-word pairs:
`FC127FFF/FFFFF238`, `FC26A004/1FFC93F8`, `FC327FFF/FFFFF838` and
`FCFFFFFF/FFFE7838`. No albedo, Lambert term or direct/ambient separation is
inferred.

| Presented surface | Forward tasks 60/420/540 | Rear task 300 |
| --- | --- | --- |
| Car, opaque candidate triangles | 464 | 464 |
| Road, opaque candidate triangles | 269 | 82 |
| Walls, opaque candidate triangles | 600 | 112 |
| Scenery, opaque candidate triangles | 2 | 8 |
| Scenery excluded for coverage | 142 | 64 |
| Walls excluded for coverage | 36 | 16 |
| Native overlay, excluded from physical geometry | 16 | 16 |

All opaque rows pass the bounded receiver checks in these runs. Caster policy
is authenticated **submitted opaque physical triangles**, including receivers
for self-shadowing. It fills no omitted geometry and creates no solid proxy.
The body has 352 faces; four wheels have 28 each. A 0.0001-unit weld finds 32
body boundary edges and 25 nonmanifold edges; wheel edge incidence is closed
and consistently oriented. Seams, T-junctions and overlapping details can
explain the body result. Convexity is not a ray-query admission prerequisite;
edge incidence is diagnostic, not proof of holes or a disqualification rule.
No runtime AS is constructed from the report.

## Overlay differential and owner-map proof

Correct the earlier prose: the flat child uses **`FC11FFFF/FFFFF238`**, not
`FC127FFF/FFFFF238`. Existing immutable dumps and HLE agree. The diagnostic
identifies the exact child by authenticated emitter, object/parent, transform
and material state; it does not filter by render mode. Its suppression predicate
remains evidence-only code.

The measured matrix is Circuit 1 through 6, vehicle model 0, one player, time
trial and single race. Each of the 12 scene/mode combinations was captured at
tasks 60, 300, 420 and 540 in a plain baseline, owner-enabled native repeat and
exact-overlay omission run. Each scenario consumed 600/600 replay frames and
made 601 swaps. The single-race captures use one human player with native race
opponents.

The owner pipeline is a second D3D12 raster pass into an `R32G32_UINT` target
with cloned depth. It mirrors the displayed pass's clipping, draw selection,
shader/material/discard and depth decisions. The no-owner baseline and
owner-enabled repeat are byte-identical on the real swapchain for every task.
The repeat's owner ID must identify the exact native overlay for each changed
swapchain pixel. After omission, every nonzero nearest/linear/PixelAA VI tap of
every changed pixel must map to an indexed presented primitive admitted for
opaque coverage and the measured fog-safe receiver path. Unknown, raw, test-Z,
overlay, unowned, wrong-material and unsupported-fog owners fail closed. No
changed-pixel tap had one of those owners, and every omission owner target was
complete.

Across the 48 task/view captures, omission changed **1,092,611 RGB pixels** and
the checker validated **4,370,444 VI taps**. It found zero changed pixels
without an overlay-owned tap in the native repeat and zero unsupported receiver
taps after omission. These are repeated per-frame pixel counts, not unique
screen locations. The visible-receiver evidence gate passes for this sampled
matrix. The diagnostic does not build a shadow scene or change player rendering.
The checker indexes VI tap coordinates directly as source texels (`floor(tx)` /
`floor(ty)`); allocation padding does not stretch those coordinates. All twelve
archived circuit/mode triples were rerun after correcting that lookup, and an
unequal-allocation fixture guards the distinction.

Projected polygons are retained only as diagnostics. Two affected pixels
(Circuit 6, rear task 300 in time trial and single race) fall outside the old
projected overlay polygon, while their native owner IDs still identify the
overlay and every post-omission filter tap resolves to an admitted receiver.
This confirms projected footprints cannot decide this gate. Actual owner IDs
identify the draw before omission and the visible primitive after omission; the
checker then maps actual VI filtering taps to the presented texture.

The exact child has 16 triangles in the Circuit 1 task. Other scenes can submit
multiple exact children, so identity is checked per child rather than assuming
one overlay per view. The trail sharing `C8104A50` keeps its different
combiner/group and is never omitted.

All six circuits and both measured modes have a valid light/material/overlay
record for this model and player count. This is bounded evidence only: no other
car model, two-to-four-player receiver-owner matrix, menu, other race mode,
offscreen caster, HDR, MSAA, interpolated presentation, or non-D3D12 backend has
passed. A sampled owner map is not runtime replacement readiness; unsupported,
pending and failed cases must continue to use native output.

The owner proof removes the visible-receiver *evidence* gate; it does not by
itself authorize production suppression. Patch 0025 attaches an immutable
capture-only `SunShadowWorkload` to its matching RT64 Workload. It carries the
task epoch and sequence, circuit and phase, normalized key direction, and
admitted, overlay and rejected face ranges. The HLE producer supplies copied
task values; renderer workers do not read guest RAM. We ran task sequence 60
for all 12 circuit/mode combinations: model 0, one player, time trial and single
race on Circuits 1-6. Each replay consumed 600/600 frames and made 601 swaps.
The scenario harness accepts car indices 0-23, but this matrix covers model 0
only; it does not establish runtime support for the other vehicles.
Every Workload authenticated its task and exact overlay ranges, but every one
reported `complete=false`. Counts below are admitted caster ranges, material
rejections, and unclassified draws. Each race case contains six authenticated
overlay ranges; each time-trial case contains one.

| Circuit | World-builder display list | Time trial admitted / rejected / unclassified | Single race admitted / rejected / unclassified |
| --- | --- | ---: | ---: |
| 1 | `0x80288160` | 92 / 34 / 342 | 157 / 34 / 312 |
| 2 | `0x80295BF0` | 67 / 25 / 342 | 132 / 25 / 312 |
| 3 | `0x802944B0` | 81 / 30 / 342 | 146 / 30 / 312 |
| 4 | `0x802FF790` | 46 / 25 / 342 | 111 / 25 / 312 |
| 5 | `0x802DCCB0` | 120 / 21 / 342 | 185 / 21 / 312 |
| 6 | `0x80303D40` | 80 / 38 / 342 | 145 / 38 / 312 |

Across the 12 Workloads, 334 rejected ranges map through the presented
vertex/index and transform tables to ordinary indexed draws in native object 0
(`flags=0x601`), the per-circuit world builder. All 334 ranges have rejection mask
`0x44` and `OtherMode.L=0xCB023038`: coverage-times-alpha is enabled and the
mode falls outside the admitted opaque set. The captured combiner selects
textured alpha; alpha compare, conventional alpha blend and force blend are
off, while alpha-coverage-select is on. These are identified track draws, not
unknown owners, but the existing ray-hit kernel has no per-sample RDP
coverage/texture-alpha evaluation for them. The admitted ranges therefore do
not form a complete caster set. A further 12 ranges map to child object 2
(`flags=0x26`, parent object 1 with physical-car flag `0x9`). The bounded parent
walk correctly classifies that child as car geometry, but rejects its material
with mask `0xEE` because its combiner, depth/fog state and blending behavior
fall outside the admitted policy. No game AS was prepared and no receiver
consumed any Workload; `complete=false` keeps every native overlay.

The material rejection mask is cumulative. Bit 0 marks an extended draw; bit 1
an unmeasured combiner; bit 2 an `OtherMode.L` outside the measured opaque
allowlist; bit 3 an unsupported depth-compare mode; bit 4 a mismatch between
native and shader RDP modes; bit 5 an unsupported fog cycle; bit 6 unsupported
alpha compare, coverage-times-alpha, blending or force-blend behavior; bit 7
unsupported depth compare/update/mode/source behavior; bit 8 an unsupported
shader flag; and bit 9 invalid or out-of-range fog color. Thus `0x44`
identifies bits 2 and 6 for the measured world-builder draws.

The Workload is incomplete if a nonempty draw uses an unsupported projection,
has an invalid face-index range, or cannot be mapped through one uniform
presented transform group to a copied native object identity, or maps to an
object role other than the measured world builder and physical cars. These
ranges are listed separately as `unclassified`; they cannot be silently
omitted from a future receiver's completeness decision. Reasons 1-6 mean
unsupported projection, invalid face range, unsupported transform group,
unknown copied object identity, triangle-count overflow, and unrecognized
object role, respectively. An overflow entry has index count zero because the
true count cannot fit in the metadata field. This is conservative because
some such draws may be non-casters, so the implementation keeps native
shadows.

Across the same matrix, 3,924 ranges remain unclassified: 912 use the
unsupported Rectangle projection; 2,724 perspective/orthographic draws map to
the `0xFFFFFFFF` transform sentinel rather than a presented object matrix; and
288 map to a parentless object with flags `0xC01` and display-list address zero
(object 4 in time trial, object 19 in single race). Its role is not established,
so it stays unknown. No invalid face range, out-of-table object identity, or
triangle-count overflow occurred in these captures. The capture validator
requires all rejected and unclassified lists to be empty before accepting a
`complete=true` Workload.

Physical-car classification follows the copied native parent chain with a
table-size traversal bound, so child meshes inherit the authenticated car
identity. A self-parent or cycle cannot loop indefinitely. The remaining
unrecognized root object was subsequently traced as described below.

### Procedural world-object identity

The parentless `flags=0xC01`, list-zero object takes the native world builder's
procedural path. At runtime `0x8000C30C`, flag `0x800` selects `0x8000C370`;
this branch emits tiled vertices and triangles through `0x80044FDC`, instead of
loading the object list at `0x8000C360`. The native constructor stores its
signed kind at record +`0x0E` (`0x80011680`); it is 13 for this object. A zero
list pointer is therefore expected for this measured kind.

All 48 producer-owned task snapshots in the stationary six-circuit by two-mode
matrix were checked. Where this object is present, flags, list, kind and parent
are `0xC01`, zero, 13 and -1. It occupies object 4 in time trial and object 19 in
single race. Each such presented task has 24 ranges containing 48 triangles.
The Circuit 1 owner map assigns 2,390 source pixels to these draws; their
presented world transforms and native source geometry agree. The object is
absent from the rear-view task 300 on Circuits 1, 2 and 6. No role is inferred
for an absent object or a different kind/flag/parent/list combination.

The compact producer snapshot now carries kind alongside flags, list and
parent. HLE uses that task-owned value to classify this procedural physical
world role; workers do not read guest RAM. This classification does not bypass
material admission. Its measured opaque/fog material passes the existing gate;
unsupported materials and the other unclassified draws still keep the Workload
incomplete. Geometry, RAM, owner maps and the matrix details remain under ignored
artifact paths. This is evidence about submitted geometry, with no offscreen
caster-coverage claim.

A fresh Circuit 1 time-trial replay completed 600/600 frames and 601 swaps.
All four task pairs passed native/presented validation. Task 60 now has 116
admitted ranges, 34 rejected material ranges and 318 unclassified draws, with no
unknown object-role range. The updated baseline is byte-identical to the prior
native image; the four owner differentials still attribute all 349,048 VI taps
of 87,262 changed RGB pixels. The preserving supported Windows build, two
focused CTests, 86 Python tests, scoped Ruff, documentation and whitespace checks
passed. No production ray-query draw was added by this classification change.

Capture metadata keeps indexed face ranges separate from non-indexed
projections. Perspective/orthographic calls report their face-index start and
count; Rectangle/None calls report zero face-index extent and retain their
projection type. Raw Triangle calls also report zero face-index extent and
retain a separate raw-vertex start and count. Unclassified projection records
therefore identify draws without claiming
indices from `triangleCount` or `faceIndicesStart`.
The changed render record uses schema 2; presentation records retain schema 1.

The 2026-10-03 regression capture reran `rt-presented-materials` for 600/600
frames and 601 swaps, then validated task sequences 60, 300, 420, and 540. At
task 60 it recorded 375 Perspective, 3 Orthographic, and 91 Rectangle calls;
Rectangle calls had `indexed=false` and zero face-index extent. The checker
accepted all four reports, including their admitted-native-overlay records.
This validates capture metadata only; it does not enable shadow replacement.

The metadata direction is checked against the native key in the current
Workload and kept separate from car and camera transforms. Its diagnostic
strength and angular radius are not measured game-light policy. A valid
parameter ABI and a matched direction do not prove either value. The production
path still needs calibrated per-circuit material/light policy, complete caster
admission or a conservative scene fallback, a material/fog/depth-preserving
receiver and exact ready-gated overlay restoration. There is no player setting
until those paths are usable.

## Ownership and diagnostic failures

`LAMBO_RT_RENDER_CAPTURE_DIR` requires the existing sunlight probe and native
capture directory. The port reads object fields only from the immutable 8-MiB
copy on HLE; renderer/present workers never read RAM. The observer is installed
before setup and cleared after queues join. Shutdown is idempotent so frontend
teardown runs once. Patch 0022 contains no game addresses or sunlight Workload
fields. Its raster activation is thread-local to scoped queue rendering: HLE
uses the same framebuffer renderer and must not enter the mutable observer.
Shared task/completion maps use a mutex. The queue-thread Workload borrow spans
begin/raster/completed within that render scope; completion or rejection clears
it. `ownerBufferRendered` retains non-owning texture pointers in the queue-owned
`PendingOwner` list until `completed` reads them after the workload GPU fence.
The list is cleared at the next `begin`, on raster rejection, and after
completion whether the capture succeeds or fails, so the pointers are not used
after that fenced callback. At most four sampled tasks and bounded vertex/index
arrays are captured. Byte outputs are capped at 128 MiB, image dimensions at 4096.
GPU copies fence before mapping and restore buffer/texture usage. Missing,
short, duplicate, nonfinite or mismatched evidence fails the scenario/checker.

Patch 0023 fixes pinned Plume's null texture dereference for a placed-footprint
buffer destination. A ROM-free regression clears a 7x3 BGRA texture, copies
through 256-byte padded rows and checks all 21 pixels on real D3D12. Generic
patches/upstream comparisons are in the [inventory](../patches/README.md).

Explicit capture mode uses Manual 30 Hz (one native frame with world processing),
no MSAA and Standard internal color; VI filtering remains native. Overrides
are diagnostic and not saved. Only selected D3D12 registers a capture observer.
`LAMBO_RT_EVIDENCE_DROP_OVERLAY=1` omits the exact overlay only on four captured
tasks. Default rendering has no observer, GPU readback allocation, shadow scene
or new draw selection. RAM, decoded geometry and images stay local and ignored.

Early captures exposed cross-thread observer access: duplicate raster records
and two `0xC0000374` exits in three runs. The scoped thread-local observer fixes
that concrete race; six subsequent captures completed without duplicates/crashes.
A ROM-free concurrent queue/HLE test guards the boundary. Earlier captures and
whole-triangle clipping results are superseded. No debugger stack was obtained
for the heap exits; the exact corruption site is not claimed.

## Reproduce

Use [BUILDING.md](../BUILDING.md) and its MinGW DLL PATH. Native inputs and
dependency pins are unchanged from [rt-shadows.md](rt-shadows.md). The USA
ROM SHA-256 was rechecked. Supported builds regenerate ignored output; no
generated source is edited or committed.

~~~powershell
./build.ps1
python tools/run_game_scenario.py scenarios/rt-presented-materials.json --timeout 120
python tools/run_game_scenario.py scenarios/rt-presented-materials.json --timeout 120
python tools/run_game_scenario.py scenarios/rt-overlay-differential.json --timeout 120
python tools/check_rt_render_capture.py artifacts/game-scenarios/<baseline> --output artifacts/presented.json
python tools/check_rt_overlay_capture.py artifacts/game-scenarios/<baseline> artifacts/game-scenarios/<repeat> artifacts/game-scenarios/<omission> --output artifacts/overlay.json
python -m unittest discover tests -p test_rt_render_capture.py
~~~

Final Circuit 1 presented-material captures: `rt-presented-materials-bvni54cp`,
`rt-presented-materials-ww1woxe3`, `rt-overlay-differential-y8zk3d4s`. Cross-circuit
owner proofs are summarized in ignored `artifacts/rt-light-matrix/*overlay-proof.json`.
Each scenario verified 600/600 input frames, 601 swaps and four native/rendered pairs.
Hardware: RTX 3080, Windows driver 32.0.16.1088, D3D12. The extended ROM-free GPU
probe passed 9189 checks with debug layer enabled and no errors. It exercises
the existing AS/query kernel and texture readback, not a game receiver. No RT
game pixels, performance budget, Vulkan/Android shadows, HDR/MSAA lifecycle or
second-vendor support is claimed.

The supported build, all 47 project CTests (including GPU and queue-scope
checks), 79 Python tests, scoped Ruff, documentation and whitespace checks
passed. The Windows frontend fixture required the physical-controller isolation
documented in [testing](testing.md) and tracked in
[issue 280](https://github.com/alondero/automobililamborghini-recomp/issues/280);
its virtual-controller assertions still
ran. Default headless `harness-smoke-rir310dn` and capture-disabled windowed
`rt-stationary-camera-tarrxa2d` also completed 600/600 frames and 601 swaps.

The 2026-10-03 Workload admission run rebuilt `lamborghini_modern`,
`lambo_rt_shadow_gpu` and `lambo_rt_sun_tests`; all 47 project CTests passed,
including the D3D12 GPU probe. The Python host suite passed 83 tests. Twelve
windowed D3D12 game captures covered model 0, one player, time trial/single
race and Circuits 1-6. Each consumed 600/600 replay frames and made 601 swaps;
all 12 authenticated their task and overlay but remained `complete=false` for
the material reason above. The capture outputs and scenario inputs remain
ignored under `artifacts/`. The documentation checker now skips that ignored
directory and passes on tracked project documentation. These runs do not show
ray-traced game pixels because no production receiver is connected.

## Continuation verification

The complete independent reviewer report was recovered locally before this
round. Its three correctness findings and three hygiene suggestions are
addressed in verified `8a4329a`: non-indexed projection/raw-vertex metadata is
separate from indexed face ranges; light authentication follows physical-car
ancestry; overlay identity uses the shared group prefix and extracted object;
raster lookup terminates on a match; per-circuit measured-key repetition is
explained; and task RAM fields have named, documented constants. No finding
was rejected, and no additional independent review was launched.

The continuation adds producer-owned race-mode/model selectors, invalidation
on scene selection changes, publication-time consistency checks and offline
authentication against the owned RAM. The render checker requires native and
presented records to retain the same selectors. The diagnostic remains scoped
to model 0, one player and documented player modes 0-3. Each task must contain
the circuit's matching native key; mode does not alter world-light direction.
Mode 4 (attract) stays gated. Pixel evidence covers time trial/single race;
unmeasured modes gain no replacement eligibility from this diagnostic scope.

Twelve fresh stationary captures independently checked forward/rear camera
rotation and unchanged native key/fill on all six circuits in both measured
modes. All 48 native/rendered task pairs passed the geometry/material checker.
All twelve task-60 Workloads still report `complete=false`, with the same
334 rejected world-builder ranges, 12 rejected car-child ranges and 3,924
unclassified draws documented above. These are sampled totals, not full-course
asset coverage.

A fresh Circuit 1 time-trial baseline/owner-repeat/exact-omission triple
passed at tasks 60/300/420/540. The owner-enabled native repeat was byte-identical
to the baseline. Exact omission changed 87,262 RGB pixels; all 349,048 actual VI
filtering taps resolved to admitted fog-safe receivers. No changed pixel lacked
native-overlay ownership and no receiver tap was unknown or unsupported. This
fresh four-task proof is additional evidence; the earlier 48-task cross-circuit
owner matrix was not recaptured in this continuation. Outputs are ignored under
`artifacts/rt-continuation/owner-differential.json`.

Model 3 time trial on Circuit 1 passed all four native/presented task checks and
the stationary light comparison. Its shadow Workload was explicitly gated by
the model policy. A two-player single-race render scenario exited successfully
and completed 600/600 replay frames and 601 swaps, but failed strict evidence:
present records for tasks 60/420/540 were missing. Queue/presentation logs show
native tasks can be skipped by the diagnostic presentation path. A separate
native-only repeat captured all four tasks and passed the two-view stationary
comparison. The failed render capture is preserved as
`rt-continuation-two-views-1pnc7vmd`; two-player pixel ownership remains unproved.
Neither result authorizes two-player overlay suppression.

The supported MinGW build passed using `./build.ps1 -PreserveSubmodules`, which
verifies initialized dependency pins and avoids checkout/clean operations.
The pre-existing RecompFrontend files were byte-identical after the builds.
A separate pinned RT64 worktree replayed all nine Windows RT64 patches; all 31
changed/new files matched the active patched source byte-for-byte. The replay
did not reset the active dependency checkouts. ROM-derived captures, ROM data,
generated game/RSP output and local review records stay ignored.

All 47 project CTests and 84 Python tests passed, together with scoped Ruff,
documentation and whitespace checks. The real RTX 3080 D3D12 probe passed
9,189 checks with the debug layer enabled and no errors. The two-triangle AS
used 2,816 bytes plus 2,304 scratch bytes; its synthetic mean GPU costs were
53.862 microseconds for AS work and 13.773 microseconds for the query/composition
kernel. These tiny-fixture measurements are not game performance evidence.
The default headless `harness-smoke-pxuf3noq` and render-capture-disabled
windowed `rt-stationary-camera-81_3l5px` also passed 600/600 input frames and
601 swaps each. The host frontend tests used physical-controller isolation
from [testing](testing.md); gameplay captures retained normal input handling.

After applying the shared-mode clarification, `1523d79` admits diagnostic light
authentication for documented player modes 0-3, requires each task's matching
native key, and excludes attract mode 4. The preservation build and four
relevant RT CTests passed again. A final Circuit 1 capture
(`rt-continuation-final-mode-policy-k17r_ov9`) completed 600/600 frames and
601 swaps, then rechecked all four tasks against the earlier owner repeat and
exact omission. Native pixels remained byte-identical, and all 349,048 taps of
87,262 changed pixels still passed. Mode 1/3 playthroughs and ownership were
not measured; this diagnostic scope does not grant replacement eligibility.

There is still no production game AS, receiver, setting or overlay replacement.
Hard/soft game shadows, self-shadow bias, resource failure, resize/lifecycle
and representative-resolution GPU cost remain unvalidated. All game scenes
retain native shadows. Unvalidated backends remain native; visible submitted
geometry is not offscreen caster coverage. PR #279 remains draft and #278 open.

## Next-session prompt

~~~text
Continue PR #279 and issue #278 from the current branch. Read CLAUDE.md,
CONTRIBUTING.md, patches/README.md, docs/rt-provenance.md,
docs/rt-material-evidence.md, docs/rt-shadows.md and docs/ray-tracing-plan.md.
The complete reviewer report was recovered and all six findings verified
addressed. The maintainer confirms shared cars/light records/views across race
modes; the diagnostic authenticates player modes 0-3 against each task's key.
Race mode/model selectors and player count now invalidate pending scene epochs.
The D3D12 owner-map proof passes for model 0, one-player time trial and single
race on Circuits 1-6 in forward/rear views. Workload admission now fails in all
12 circuit/mode cases. The evidence records 334 world-builder ranges rejected
for textured coverage-times-alpha, 12 car-child ranges rejected for their
combiner/depth/fog/blend behavior, and 3,924 unclassified draws: 912 rectangle
projections, 2,724 transform-sentinel ranges, and 288 parentless objects with
unknown roles. First prove ray-hit policies that preserve native texture-alpha,
coverage, depth, fog and blend behavior, and classify or conservatively retain
the unclassified draws without omitting required casters. Keep native overlays
for every incomplete or unsupported Workload. Only after complete presented
caster and receiver admission, integrate a per-view AS and native-equivalent
receiver; then add an Original-default setting, exact ready-gated suppression
and restoration. Test game swapchain pixels, hard parity, softening,
self-shadow bias, lifecycle, resize, failure and GPU cost. Expand supported
vehicles, modes, players and validated backends. Keep relighting and offscreen
caster submission deferred; do not close #278 until its requirements are
complete. Use finish.
~~~
