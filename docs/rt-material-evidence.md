# Presented material and receiver-ownership evidence

Status: evidence for the optional ray-traced shadow feature, measured on
Windows through 2026-10-03. The D3D12 raster-owner diagnostic proves visible
receiver ownership for every overlay-affected filter tap in the measured
one-player scenes. The [production caster policy](#production-caster-policy)
now completes admission for the measured scenes, and the
[shadow feature page](rt-shadows.md) records the production pass, setting and
in-game validation. Relighting and offscreen caster submission remain
follow-on phases. The sections below keep the earlier diagnostic history.

Read [native provenance](rt-provenance.md), [GPU groundwork](rt-shadows.md)
and the [plan](ray-tracing-plan.md) with this page.

The latest continuation classifies the procedural world object, explicit sky
backdrop, native HUD projection and screen rectangles. All 48 newly captured
model-0/one-player Workloads have zero unclassified draws. They remain incomplete
because native texture-alpha coverage and blended car geometry are unsupported.
The older counts below describe the earlier admission policy;
[separate material policies](#separate-caster-and-receiver-policies) records the
current gate and verification.

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

### Separate caster and receiver policies

Caster opacity no longer depends on whether a surface's RGB combiner and fog
can receive attenuation. The task metadata has separate caster, receiver and
native-only receiver ranges. Alpha/discard, shader agreement and native depth
restrictions still guard opaque caster admission. The measured receiver
combiner/fog/depth policy remains unchanged. These values cannot enable native
overlay suppression without the later visible-coverage and resource gates.

Classified non-casters are recorded separately instead of silently skipped:

- Screen rectangles require Rectangle projection, the rectangle shader, no
  native depth compare/update, matching shader/native modes and no extended draw.
  Their face-index extent stays zero.
- Panorama draws require the explicit producer `G_EX_ASPECT_BACKDROP` projection
  tag and the same depth/mode/extended checks. Camera transforms and the
  `0xFFFFFFFF` world-group sentinel do not identify sky on their own.
- Native orthographic HUD geometry additionally requires HLE-copied projection
  identity `0x000A2C40` and no native geometry flags. The native task loads that
  static HUD matrix before the needle/minimap arrow quads; the captured local
  vertices, transforms and draw path agree with [HUD evidence](HUD.md).

Patch 0025 copies each projection's physical address alongside its transform
group in HLE. Reset clears both arrays and seeds the same identity slot. Workers
use those values without following guest pointers. Material policy schema 2
records every exclusion and receiver range; the checker verifies the range
against its exact presented draw and rejects exclusions without the required
projection/material evidence.

Twelve fresh replays covered Circuits 1-6 in time trial and single race, each
600/600 frames and 601 swaps, with four forward/rear task pairs. All 48 pairs
passed native/presented authentication and had zero unclassified draws. Their
native swapchain bytes match the previous build exactly. The task-60 world
coverage-alpha ranges still total 334 and use caster rejection bit `0x40`;
the twelve blended car-child ranges use `0xC0` (alpha/blending and depth).
Receiver-only RGB/fog rejections no longer appear as caster rejection reasons.
Every Workload remains `complete=false` and retains native shadows.

A fresh Circuit 1 owner-enabled repeat and exact-overlay omission used the same
metadata build. Enabling ownership still produces a byte-identical native image.
All 349,048 actual filter taps of 87,262 changed RGB pixels resolve to admitted
fog-safe receivers. Older owner files cannot be compared directly with these
records because their call metadata lacks the newly copied projection identity;
the strict comparison correctly rejects that mismatch.

The preserving supported Windows build, all 48 project CTests (including the
real RTX 3080 D3D12 ray-query probe), 88 Python tests, scoped Ruff, documentation
and whitespace checks passed. The pinned patch replay matched all 31 modified/new
RT64 files byte-for-byte. All 150 preserved RecompFrontend files also remain
byte-identical. This measures sampled native output and admission only; it adds
no game AS, RT receiver, setting, performance claim or full-game coverage claim.

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

### Shared native material evaluator

Patch 0026 extracts the existing RasterPS texture, combiner, alpha-compare and
coverage code into `NativeMaterial.hlsli`. RasterPS supplies its native UV
derivatives, pixel position, interpolated shade and random seed. Clipping,
depth, blending, fog and output coverage remain in their original raster path.
The helper samples the active native tile/cache/replacement resources. It
does not read guest RAM or decide which materials may cast.

Twelve fresh `rt-native-material-c<1-6>-m<0,2>` windowed D3D12 replays each
completed 600/600 input frames and 601 swaps. All 48 task-60/300/420/540
swapchain images match the earlier native baseline byte-for-byte. Native
key/camera and strict presented-material checks also pass. The Circuit 1
four-task comparison against the preceding owner-enabled repeat and exact
overlay omission still attributes all 349,048 VI taps of 87,262 changed RGB
pixels. Call, geometry, material, raster and VI identity checks were retained.
Evidence is ignored under `artifacts/rt-caster-stage/native-material-matrix.json`
and `native-material-owner-proof.json`.

On the same RTX 3080/D3D12 adapter, 30 analytic GPU fixtures exercise the
production evaluator through native descriptor layouts. They test transparent
holes, the exact 1/8 coverage boundary, threshold alpha compare, HDR coverage,
wrap/mirror/clamp, native three-point filtering, dynamic tile overrides, native
samplers, a scaled replacement with different alpha, IA16 TMEM decoding and
derivative-selected LOD tiles. Alpha and survival match analytic expectations;
random-seed advancement and input identity also match. The existing 9,189
shadow-kernel checks pass with the D3D12 debug layer and no errors. Logs are
`material-resource-gpu.log` in the same ignored directory.

These fixtures do not supply real ray candidates, reconstruct native UV/shade
at game hits, exercise every texture format or validate game replacement packs.
The geometry builder still admits opaque solids only. Every sampled Workload
remains incomplete and native overlays stay enabled until the full ray-hit
material contract is proved.

### Native material at ray candidates

Patch 0027 adds reconstruction from generated screen positions, UVs and shaded
colors at the candidate's original indexed face. World barycentrics preserve
perspective interpolation; displayed viewport, scale and offset determine
pixel position and coarse 2x2 UV differences. The native DXIL raster binary
uses `DerivCoarseX/Y`, confirmed by DXC disassembly. Behind-eye, degenerate and
non-finite input is unsupported. The caller must bind the matching presented
attributes, material and texture resources and propagate coverage failure to
the view's readiness gate.

The real D3D12 probe now builds 66 textured triangles and traces candidate hits
through the production sunlight kernel and native material evaluator. Thirty
supported cases match both analytic alpha expectations and actual native
RasterVS/RasterPS owner output at matching pixel centres, with native clipping
and depth enabled. Cached textures, scaled replacement alpha, IA16 TMEM,
dynamic tiles, native samplers, filtering and LOD are included. Two additional
shade-alpha cases and a subpixel-LOD case explicitly return invalid coverage.
The probe also rejects behind-eye/degenerate attributes, conflicting material
tags, rejected AS ranges and alpha candidates without an evaluator. The full
shadow probe passes 9,795 checks with the debug layer and no errors. Local log:
`artifacts/rt-caster-stage/material-raster-candidate-gpu.log`.

Two precision limits were measured. At exactly 1/8 alpha with a varying shade
input, hit interpolation survived while the native raster fragment discarded.
A 0.5-pixel LOD footprint also disagreed with native owner output. The helper
therefore rejects alpha-dependent varying shade and subpixel LOD footprints;
it does not infer a safe interpolation tolerance. Stochastic alpha compare is
also unsupported. Native receiver fog continues to use native raster shade.
The fixtures remain in the hardware probe with explicit unsupported results.

These measurements do not authenticate game texture bindings, every format,
replacement packs, view isolation or complete ray-hit coverage. Game admission
still rejects every native alpha range and blended car child. No game AS,
receiver, setting or overlay suppression is enabled by this patch.

The preserving supported build and all 48 project CTests/88 Python tests pass.
A separate replay applies all eleven RT64 patches and matches all 33 changed
source files byte-for-byte. A fresh Circuit 1 native capture completes
600/600 frames and 601 swaps; all four forward/rear task images match the
earlier native baseline. Its material/light checks still pass with zero
unclassified draws and `complete=false`. All 150 preserved RecompFrontend
files remain byte-identical. This is a sampled native regression check, not
full-game RT coverage.

The raster fixture also exposed a native pipeline-failure signal: pinned Plume
returned a graphics wrapper even when its native D3D12 PSO was null. Patch 0028
returns null for failed graphics/compute PSOs. The hardware probe deliberately
fails both creations using incompatible root signatures, then successfully
submits the valid raster and ray paths. Only the expected validation IDs 683
and 882 are filtered within the injection scope. Other D3D12 errors still fail
the probe. Evidence is `artifacts/rt-caster-stage/pipeline-failure-gpu.log`.
This proves the factory signal and continued device use, not a game overlay
restoration path or device-loss recovery.

### Native game alpha diagnostic

Patch 0029 adds an opt-in D3D12 comparison over actual presented cutout triangles.
`diagnostics.rt_native_alpha_check` requires render capture and enables
`LAMBO_RT_NATIVE_ALPHA_CHECK`. After the matching Workload fence, the observer
borrows the displayed framebuffer's native descriptors, generated attributes,
indices, selected shaders, viewport and scissor while the worker and texture
cache remain exclusively owned. Workers read no guest RAM.

Each triangle is isolated into private color/depth targets. The unchanged
native owner pipeline supplies the clipping, culling, material discard and
depth reference. A separate attribute pass uses the selected vertex shader
and a geometry shader to capture its actual outputs before clipping. Compute
evaluates the shared material helper at reconstructed interior points. Pixels
within one pixel of an edge are excluded. This does not authenticate ray
intersection precision, edge coverage, blended car children or view readiness.

Captures retain generated float2 UVs, uploaded render/tile parameters, distinct
render-index and instance-index identity, selected native vertex bytecode,
pre-clipping outputs, counters and a bounded first-disagreement record. Raw
vertex draws retain zero native face start. GPU work is fenced before local
resources are destroyed, including exception paths. Each framebuffer is limited
to 16,384 faces, 4,096 pixels per dimension and 120 MiB of private image targets,
with additional bounded buffers. Unsupported resources/backends reject the
diagnostic and leave native rendering available.

The first real-game reconstructed-hit comparison exposed four forward-view and
five rear-view interior disagreements absent from the synthetic fixtures. UV
differences crossed the native sampler's 1/128 coordinate quantization boundary
and changed survival at 1/8 alpha. Preserving homogeneous-W arithmetic alone did
not fix them. Fused float viewport scale/offset and nearest-even 16.8 snapping
reduced, but did not eliminate, the errors. Direct3D interpolates from snapped
positions; its permitted conversion tolerance does not establish identical
rounding across hardware. [Direct3D specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).

On RTX 3080/D3D12 at 1708x960, all 574,658 tested interior points in the four
Circuit 1 time-trial tasks matched native coverage. In the wider six-circuit,
model-0, one-player time-trial/single-race matrix, the initial reconstructed-hit
domain tested 6,296,058 points over 7,904 face records and found eight
disagreements: one rear-view point per Circuit 2 mode and three forward-view
points per Circuit 3 mode. Circuit 2 included a reconstructed pixel displacement
of roughly 0.00012 pixel; Circuit 3 retained UV/derivative rounding differences.

A follow-up diagnostic adds two explicitly labeled raster-sample domains. The
pixel-center FP32 run removes Circuit 2's errors but retains Circuit 3's. A
separate shader variant computes perspective UV interpolation and coarse UV
derivatives in double precision at the actual raster sample center; conversion
to float occurs before the native texture evaluator. Across all 12
circuit/mode cases and 48 tasks, this domain tested the same 6,296,058 interior
points with zero coverage disagreements. All 48 native images and all 48
presented geometry/material buffer sets match their alpha-diagnostic controls
byte-for-byte. There are still 1,330 unsupported face records and 3,352 records
with no tested interior; those counts overlap. The ignored aggregate is
`artifacts/rt-caster-stage/native-alpha-double-matrix.json`.

This follow-up establishes native material agreement at tested raster pixel
centers on this D3D12 device. It does not establish the UV/depth values that a
hardware ray hit would produce, native coverage on triangle edges, unsupported
face behavior, or another backend. The captured output vertex data and selected
raster pixels validate this diagnostic input domain; they are not ray-query
results. No alpha range is admitted and all game Workloads remain native. The
older failed reconstructed-hit results remain in
`artifacts/rt-caster-stage/native-alpha-matrix.json` for comparison.

All 48 baseline/diagnostic pairs have identical generated geometry/material
buffers. Native swapchain bytes match in 47 initial pairs. The first Circuit 1
time-trial task differs by four bytes/four RGB pixels while the two runs select
different native pipelines during asynchronous compilation. A repeated
native-only control selects the same shader kinds as the diagnostic and matches
all four diagnostic task images byte-for-byte; it also reproduces the original
control's four-pixel difference. The first control pair does not pass byte
identity. The 1,330 unsupported face records and 3,352 records with no tested
interior are explicit partial evidence; counts overlap. No edge pixel or real
game ray candidate is authenticated. Every report retains
`alpha_admitted=false`, and all game Workloads remain native. Ignored evidence
is `artifacts/rt-caster-stage/native-alpha-matrix.json`; the repeated control is
`rt-native-alpha-control-repeat-c1-m0-42o91vr0`.

`tools/check_rt_alpha_capture.py --require-interior-parity` rejects measured
disagreements. Host checks authenticate complete result-face identity, extents,
counters, shader filenames and the presented vertices' clip W. Passing interior
parity alone does not authorize alpha ranges or replacement.

The preserving supported build, all 49 project CTests and 98 Python tests pass,
with scoped Ruff, documentation and whitespace checks. The original
reconstructed-hit strict comparison passes Circuit 1 and rejects the measured
Circuit 2 failure; the raster-pixel double-UV domain has zero interior
disagreements across its measured matrix.
The twelve-patch pinned replay matches all 39 changed/new RT64 files byte-for-byte.
All 150 preserved RecompFrontend files remain byte-identical. Generated game/RSP
code, native shader bytecode, captures and ROM-derived data remain ignored.

## Production caster policy

The earlier strict gate failed every Workload on two material families. The
production policy resolves both with measured, exact signatures and keeps
every other unmeasured material incomplete, so its scene stays native.

**World cutouts are explicit non-casters.** All 1,274 rejected world-builder
ranges in the 48-task matrix share `OtherMode.L=0xCB023038`: coverage times
alpha, opaque depth compare and update, standard fog, caster rejection exactly
`0x40`. The native raster discarded 40,381 of 122,650 tested interior samples
(about 33%) in Circuit 1 task 60 alone, so these are real cutouts. With no
ray-hit coverage proof, they are excluded from the acceleration structure with
non-caster reason 4, restricted to the world-builder and procedural world
roles. Removing a caster can only remove occlusion; the car's own triangles
still block every ray that reaches the car, and native draws no scenery
shadows. A car-owned range with the same material stays rejected.

**The car glass casts as opaque glass (maintainer decision).** Object 2
(`flags=0x26`, parent the physical car) draws 20 translucent triangles with
`OtherMode.L=0x00504A50`, combiner `FC121824/FF33FFFF`, translucent depth mode,
compare without update. Every glass vertex coincides with a body vertex, but the
glass spans openings: projecting dense glass samples along the measured key,
7-17% fall outside the opaque body silhouette on Circuits 1-5 (0% on Circuit
6). Excluding it would cut window-shaped gaps into the car shadow. The
maintainer chose opaque glass. It is an artistic simplification, not measured
transmission. The glass never receives.

**Only prelit surfaces receive.** Phase 1 attenuates native output and never
relights. A vertex-lit surface (F3DEX `G_LIGHTING`) would also lose its fill
and ambient light, so light-averted car faces turned nearly black in the first
rear-view capture. Lit surfaces cast but carry receiver rejection bit 10. In
Circuit 1 task 60, 38 of 116 former receivers were lit car ranges; the 78
prelit road, wall and cabin ranges remain receivers.

**MSAA and high-precision receivers are admitted.** `RenderFlags` bits 30-31
hold the sample count. The receiver pass has a multisample variant using RT64's
averaged-depth decal rule. Bit 29, `usesHDR`, is RT64's high-precision colour
target (`R16G16B16A16_UNORM`, chosen by `hpfb_option` On, or Auto on a device
that prefers it). It only widens the colour channels and the coverage range; the
shared material code already reads that range, and the composite pipeline and
native copy follow the target format. A full six-circuit, two-mode, hard/soft
matrix with `hpfb_option: On` in every control and trace passed: all 364 draws
of each sampled task carried bit 29, every sampled task was ready, no pixel was
brightened, and hard cores stayed within 0.0061 of native. The unshadowed
control omits the owner buffer, because that diagnostic forces standard colour.

**Tyre-trail decals are explicit non-casters.** Menu Arcade races (race mode 1)
accumulate translucent two-triangle quads that the trail emitter lays on the
road: `OtherMode.L=0xC8104A50` (shared with the car overlay), combiner
`FCFFFFFF/FFFE7638`, geometry mode `0x810205`, blend and force-blend on, depth
compare without update, translucent depth mode `0x800`. Each quad can span road
segments, so its vertices belong to several transform groups and the earlier
gate left the scene incomplete from about task 348 onward. The count grew by two
every 120 tasks. Translucent draws never cast, so the exact material and
geometry signature classifies them as non-caster reason 5 without an object
role. They draw after every receiver and before the car overlays, so the
composite darkens them with the road just as the native overlay does. The car
overlay keeps its own identity (combiner `…11FFFF/FFFFF238`, geometry `0x12005`,
48 indices, car-child object).

Host tests: `lambo_rt_material_policy`, `lambo_rt_admission_policy` and
`tests/test_rt_render_capture.py`. The offline checker authenticates each
cutout exclusion against its exact material and world object role.

## Native overlay contrast

The swapchain transmission of the native overlay core is about 0.51 on every
captured circuit and mode: p1 0.510-0.515 over 881,038 changed pixels of the
earlier baseline/omission pairs. That ratio is after the VI. A hard
ray-traced run applies an exactly known framebuffer transmission at full
occlusion; with strength 0.49 (framebuffer 0.51) the displayed core was 0.727,
so the display response is about `T_display = T_fb^0.47`. The same response maps
the native 0.5115 core to framebuffer 0.243 at both p1 and p25. The production
strength is therefore 0.757. A later hard run displays 0.517-0.519 against the
native 0.512. This calibrates contrast on screen; it is not a physical light
measurement.

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
12 circuit/mode cases. The latest owner/material matrix has zero unclassified
draws; procedural world identity and explicit screen/backdrop/HUD exclusions are
authenticated. Alpha and blended car-child ranges still fail admission. Patch
0029 compares actual native material/shader bindings. The initial
reconstructed-hit path found eight disagreements among 6,296,058 interior
points. A separate D3D12 raster-pixel-center path using double-precision
perspective UV/derivative interpolation now has zero disagreements across the
same six circuits, modes 0/2, forward/rear views and 48 tasks. Its 48 native
images and presented buffers are byte-identical to controls. This remains pixel
center evidence, not real ray-hit proof; 1,330 face records are unsupported and
3,352 have no tested interior, and edges remain untested. No alpha range is
admitted. Read the exact limits and artifacts in the game alpha diagnostic
section. The maintainer confirms cars, light records and camera views are shared
across race modes; keep authenticating each scene/task epoch and do not infer
coverage from that confirmation.

Next, determine whether native alpha/coverage behavior can be proven at actual
hardware ray candidates with a conservative failure bound. Preserve native
depth, fog and receiver behavior, and fail closed for unsupported/clipped faces,
edges, blended car children and unknown owners. Do not treat pixel-center
parity, camera-visible geometry or one vendor as broader proof. Maintain native
overlay output for every incomplete/unsupported view. Then continue the
per-view acceleration structure, native receiver, ready-gated suppression and
restoration, and Original-default setting. Test real swapchain output, hard
parity, soft shadows, bias, scene changes, resizing, resource failure and GPU
cost. Expand vehicles, race modes, player counts and real hardware backends.
Relighting and offscreen caster submission remain deferred. Keep PR #279 draft
and issue #278 open until requirements are proven. Use finish before genuinely
completing that larger task; this checkpoint is not feature completion.
~~~
