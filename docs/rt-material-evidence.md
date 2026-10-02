# Presented Circuit 1 material evidence

Status: diagnostic milestone after PR #279 (`d9c6f82`), measured on Windows
on 2026-10-02. Native opaque surface candidates and presented indexed ranges
are authenticated. The exact overlay has a reproducible swapchain differential.
**Visible receiver ownership and ready RT replacement coverage remain unproved.
Workload sunlight fields and raster receiver integration stay gated.** Relighting
and offscreen submission remain follow-on phases.

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

## Overlay differential and the remaining gate

Correct the earlier prose: the flat child uses **`FC11FFFF/FFFFF238`**, not
`FC127FFF/FFFFF238`. Existing immutable dumps and HLE agree. The latter occurs
on physical car/wall materials. Diagnostic omission also requires Circuit 1,
one view, 48 original indices, `C8104A50`, geometry `0x12005`, one native
car-object group, child flags `0x42`, list `0x8013D3C8` and a physical-car parent.
Offline matching authenticates producer/emitter and corners. The trail sharing
`C8104A50` keeps its different combiner/group and is never omitted.

The baseline and native repeat have identical draw inputs, GPU world/screen/
shade bytes and every presented BGRA byte. Omission changes only the overlay
range. Captures include actual VI viewport, video/texture extents, scissor and
filtering. The checker handles nearest, linear and pinned PixelAntialiasing
UV warp/clamp with nonzero sampler taps.

| Task | RGB pixels changed by omission | Inclusive pixel bounds |
| --- | --- | --- |
| 60 | 22958 | (601,536) to (1039,668) |
| 300 | 18388 | (546,533) to (984,679) |
| 420 | 22958 | (601,536) to (1039,668) |
| 540 | 22958 | (601,536) to (1039,668) |

Every changed pixel has a contributing tap in the exact overlay footprint.
Every contributing overlay tap lies in admitted road footprints; projected
uncovered area is zero. Homogeneous clipping matters: rejecting whole
near-plane-crossing triangles falsely removes foreground road. The checker
includes RasterVS clipping, RasterPS's F3D far bound, scissor and culling.
Empty clipped polygons cover nothing.

These are **screen-footprint and native pixel-differential proofs**. The union
does not establish the visible receiver primitive after native depth, raster
edge/sample coverage or later draws. It cannot establish ready shadow AS,
receiver shader or failure restoration: those do not exist yet. The checker
always reports replacement coverage unproved. Next, establish eligible visible
receiver ownership with a GPU material/depth diagnostic while preserving exact
draw/face identity. Only then proceed to Workload/raster integration. Production
suppression must also require that view's valid light, scene, coverage and
pipeline readiness, restoring native output on every fallback.

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
it. GPU objects are callback-local. At most four sampled tasks and bounded vertex/index arrays
are captured. Byte outputs are capped at 128 MiB, image dimensions at 4096.
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

Final captures: `rt-presented-materials-bvni54cp`,
`rt-presented-materials-ww1woxe3`, `rt-overlay-differential-y8zk3d4s`.
Each verified 600/600 input frames, 601 swaps and four native/rendered pairs.
Hardware: RTX 3080, Windows driver 32.0.16.1088, D3D12. The extended ROM-free GPU
probe passed 9189 checks with debug layer enabled and no errors. It exercises
the existing AS/query kernel and texture readback, not a game receiver. No RT
game pixels, performance budget, Vulkan/Android shadows, HDR/MSAA lifecycle or
second-vendor support is claimed.

The supported build, all 47 project CTests (including GPU and queue-scope
checks), 70 Python tests, scoped Ruff, documentation and whitespace checks
passed. The Windows frontend fixture required the physical-controller isolation
documented in [testing](testing.md) and tracked in
[issue 280](https://github.com/alondero/automobililamborghini-recomp/issues/280);
its virtual-controller assertions still
ran. Default headless `harness-smoke-rir310dn` and capture-disabled windowed
`rt-stationary-camera-tarrxa2d` also completed 600/600 frames and 601 swaps.

## Next-session prompt

~~~text
Continue PR #279 and issue #278. Read docs/rt-material-evidence.md,
docs/rt-provenance.md, docs/rt-shadows.md and docs/ray-tracing-plan.md.
Presented opaque surface candidates and the exact overlay differential are
established. Prove visible eligible receiver ownership and replacement coverage;
projected unions alone are insufficient. Preserve exact draw/face mapping and
failure restoration. Only then add the matching Workload sunlight/admission
snapshot and raster receiver path. Keep relighting/offscreen submission deferred.
Use finish.
~~~
