# Presented material and receiver-ownership evidence

Status: diagnostic milestone after PR #279 (`bd05651`), measured on Windows
on 2026-10-03. The D3D12 raster-owner diagnostic proves visible receiver
ownership for every overlay-affected filter tap in the measured one-player
scenes. **Production shadow replacement is still unimplemented:** no shadow
Workload value, scene acceleration structure, receiver shader, user setting or
native-overlay suppression path is connected. Unsupported cases remain native.
Relighting and offscreen caster submission remain follow-on phases.

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

The earlier candidate inventory authenticates submitted opaque surfaces, not
production admission. The owner proof removes the visible-receiver *evidence*
gate; it does not by itself authorize production suppression. Integration must
carry task light and exact draw/face metadata into Workload, build the shadow
scene from admitted presented geometry, and restore native output on every
unsupported, pending or failed path. No such Workload field, receiver shader,
setting or production fallback is implemented yet.

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

## Next-session prompt

~~~text
Continue PR #279 and issue #278 from the current branch. Read CLAUDE.md,
CONTRIBUTING.md, patches/README.md, docs/rt-provenance.md,
docs/rt-material-evidence.md, docs/rt-shadows.md and docs/ray-tracing-plan.md.
The D3D12 owner-map gate passes for model 0, one-player time trial and single
race on all six circuits, with forward/rear views and exact overlay pixel
attribution. Implement the production path in stages: use a measured
per-circuit task-owned world-light policy (do not hard-code Circuit 1 as
universal), carry scene epoch/task/light plus authenticated draw/face metadata
into RT64 Workload, build shadow AS from admitted presented geometry, preserve
native material/fog/depth behavior in a ready-gated receiver path, then add the
opt-in Original-default setting and native fallback. Validate actual game
swapchain pixels, hard parity, softness, self-shadow bias, transitions, resize,
resource failure and GPU cost. Keep unsupported vehicles, player counts, modes
and backends native until measured; keep relighting and offscreen caster
submission deferred. Do not close #278 until integration and expanded coverage
requirements are complete. Use finish.
~~~
