# Optional ray-traced shadows: implementation plan

Status: source-backed proposal, reviewed on 2026-10-02. No ray-tracing
enhancement was implemented or GPU-validated in this review. The target is
Automobili Lamborghini: Recompiled. RaceWave46 is a peer implementation to
learn from; its courses, RAM addresses and lighting policy are not Lamborghini
inputs.

## Recommendation and scope

Add optional sun-angle soft shadows over the existing native raster output.
Start with one proven one-player race scene on Circuit 1 (circuit index 0),
using the submitted cars and opaque scenery as casters and an explicitly
supported road material as receiver. This is a provisional scene choice:
native sun provenance, draw identity and shadow-overlay replacement must be
proved before enabling it. Use Circuit 5 (index 4, the city track) later to
stress geometry coverage, rather than assuming it has suitable sunlight.

Own the acceleration structure as a shadow resource from the start. Lamborghini
does not need RaceWave46's dependency on ray-traced water to add this effect.
Preserve the original sky and lens flare initially. Keep direct/ambient
relighting, reflective materials, complete offscreen caster coverage and
multiplayer expansion as separate milestones.

The first supported experimental backend should be Windows D3D12 with DXR 1.1
and Shader Model 6.5. Original rendering remains available on all existing
backends. Vulkan support needs its own shader, capability and driver validation;
Android and Linux RT support are not established by a Windows experiment.

## Source verification

The starting Lamborghini revision was
`f1542f04e21894b1f1c315f7a255c1f4a0840eaf` on branch
`platonic-quaggy-clasp`, with a clean working tree. Both remotes identify
`alondero/automobililamborghini-recomp`; the remote main revision matched that
commit when checked. The port's instructions are in
[CLAUDE.md](../CLAUDE.md), [Contributing](../CONTRIBUTING.md), and the
[documentation map](README.md).

The three submodules are uninitialized in this worktree. The RT64 gitlink is
`f0728a2520d5aa735886240de3fee75cc805f6d6`. Its source was inspected through a
local dependency checkout at that exact revision, using Git objects for the
pinned source and the checked-in [patch inventory](../patches/README.md) for
local changes. That local dependency checkout contains applied patches and was
not reset or edited. Initialize this worktree through the supported build
sequence before implementation; inspection of another checkout is not a build
of this one.

The peer checkout was clean and still matched the reviewed RaceWave46 revision
`5c34f3711426978107707a8f06af9c2af4ee5401`; its remote HEAD also matched.
The sun reader, workload capture, scene construction, shadow shader, material
reader, filtering and capability gate were checked against that source. This
corrects the inherited handoff's destination without repeating its game or test
claims as Lamborghini evidence.

Current RT64 upstream HEAD was
`43373749dac9bbc1b653e6a02aed40a9e1783bed`. Its
[shader build rules][rt64-current-build] were inspected for comparison, not
adopted as a dependency upgrade. The eventual renderer patch must compare its
specific changes with upstream again and record them locally.

## What transfers from RaceWave46

| Peer mechanism | Useful here | Game-specific work to replace |
| --- | --- | --- |
| Producer-owned light snapshot copied into Workload | Stable scene/light identity for queued rendering | All Wave Race addresses, phase/course gates and preset tables |
| AS built from presented world positions and admitted index ranges | Rays and raster pixels share interpolation/transforms | Shore, water, buoy, rider and fence classification |
| Native RasterPS evaluated before shadow composition | Preserve texture, alpha, depth and known fog paths | Lamborghini receiver eligibility and native shadow-overlay handling |
| Inline RayQuery with alpha-tested candidates and exact receiver-face exclusion | Cutout holes and self-shadow correctness | Material contracts and proxy geometry for this game's draws |
| Capability checks and pending/failed pipeline fallback | Keep the original frame usable | RT-water prerequisites and Wave Race settings schema |

The peer [sun reader][peer-light] uses an immutable authored bank and special
sun bearings for Dolphin Park and Sunset Bay. The separate
[celestial reader][peer-celestial] uses camera-dependent pixel-height sky art.
Neither supplies a Lamborghini sun direction. Those two courses are reference
cases for provenance and camera independence, not implementation targets here.

The peer's [world shadow shader][peer-world] retains native rendering and
attenuates the surface contribution while protecting standard fog. Its
[soft-shadow filter][peer-shadow] offsets ray origins across a receiver disc;
the actor queries keep parallel light directions, and world scenery gets a
hard query first. That is useful filtering but does not establish an emitter
angle with separation-dependent penumbrae. The proposed cone sampling below
is additional work, not a feature already verified in either port.

RaceWave46's [scene builder][peer-scene] uses submitted geometry, so it does
not solve offscreen native culling. Its [device gate][peer-device] checks
DXR 1.1/SM 6.5 for the D3D12 path. The optional
[GPU probes][peer-gpu] provide a useful test pattern; a disabled suite or a
skip result is not GPU execution evidence.

## Lamborghini's existing seams and unknowns

| Area | Current evidence | Consequence |
| --- | --- | --- |
| Presenter | [rt64_renderer.cpp](../src/rt64_renderer.cpp): RT64Context::send_dl resets the RSP, applies the fog rewrite, loads F3DEX and calls processDisplayLists | Capture the task's metadata before queued rendering and attach it to the matching workload |
| Renderer input | Pinned [Workload/DrawData][rt64-workload] carries positions, indices, material state, light counts and transform data; [RSPWorldCS][rt64-world] writes interpolated world positions | Reuse these buffers and their fences; verify the game's coordinate and transform convention first |
| Raster vertex path | Pinned [RasterVS][rt64-raster-vs] passes screen position, UV and colors, without a world-position varying | A selected receiver pipeline needs presented world position and compatible vertex inputs; adding a pixel shader alone is insufficient |
| Lighting | [stub_renderer.cpp](../src/stub_renderer.cpp) decodes F3DEX light records and has a synthetic lighting self-test; pinned [RSPProcessCS][rt64-process] distinguishes light counts from vertex RGB | Native lights exist, but the documented warm key/cool fill example is not a proven fixed world sun |
| Sun and flare | [lambo_flare_widescreen.c](../src/lambo_flare_widescreen.c) brackets the ghost loop in func_80036854; [gen_syms_toml.py](../scripts/gen_syms_toml.py) owns its hooks | Trace the sun input before projection and visibility tests; projected flare coordinates are not light provenance |
| Sky | [Sky panorama](sky-panorama.md) and [lambo_sky_widescreen.cpp](../src/lambo_sky_widescreen.cpp) describe cylindrical yaw and authored vertical art | A panorama texel does not establish physical elevation or angular radius |
| Fog | [lambo_fog_widescreen.cpp](../src/lambo_fog_widescreen.cpp) rewrites submitted fog for settings and multiplayer | Preserve the effective rewritten material/fog state, not an assumed stock constant |
| Settings | [lambo_config.h](../src/lambo_config.h), [lambo_config.cpp](../src/lambo_config.cpp), [lambo_frontend_settings.cpp](../src/ui/lambo_frontend_settings.cpp) own persistence/live controls | Add a port-owned setting and publish an immutable render snapshot |
| Native car shadow | stub_renderer.cpp records a translucent car-shadow material, including render-mode example C8104A50 | Prove draw identity before replacement; a render-mode signature alone is unsafe |
| Coverage | [Track index](TRACK_INDEX.md), [Track research](TRACK_MODDING_RESEARCH.md), and [lambo_no_lod.cpp](../src/lambo_no_lod.cpp) describe PVS/distance policies | Submitted geometry can still omit relevant casters; increasing draw distance is not full caster coverage |

Pinned RT64 includes conditional `RT_ENABLED` code in its framebuffer renderer,
but the referenced rt64_raytracing_resources and rt64_raytracing_shader_cache
files are absent from its source tree, and the reviewed build does not define
that macro. It is not a supported switch that turns this game into an RT port.
Use a narrow selected-receiver/inline-query integration, drawing on the peer's
custom path, instead of enabling incomplete conditional code or replacing the
whole renderer with the peer's vendored tree.

## Phase 0: prove the game inputs

This is the first implementation prerequisite, with concrete outputs:

1. Initialize the pinned dependencies and build with the supported MinGW
   [Windows sequence](../BUILDING.md). Record the USA ROM identity and freshly
   generated-source state. Never edit RecompiledFuncs or src/aspMain.cpp.
2. Trace the sun/flare emitter back to its source position/direction or authored
   track parameters. Capture those inputs, the native light records and the
   current view/model transforms for the same task. Establish world axes,
   direction sign, units, whether the sun is distant or finite, and whether
   either light bank changes with camera, car orientation or race direction.
   Test fixed car position with several camera headings and two viewports.
3. Record a bridge contract before using any new guest address: field widths,
   byte order, address/layout, owning game phase/thread, capture point, task
   lifetime, validation and failure behavior. Existing sky task addresses are
   leads, not evidence for a new light field. Prefer a checked-in source hook
   before projection/culling when possible.
4. Capture Circuit 1 draws and identify physical car meshes, road receivers,
   solid scenery and the native shadow overlay through emitter identity,
   transform groups and material state. Use repeated camera/car captures to
   falsify each classification. Exclude sky/panorama, flare, HUD, menus,
   minimap, smoke and translucent effects by construction.
5. Select and save a repeatable scene/input trace containing a car, road,
   nearby solid caster and fog. Store capture metadata and small host fixtures
   with clear provenance; keep ROM/RDRAM bytes and copyrighted textures local.

Choose light provenance in this order: a proven native world-space sun;
an independently validated authored world light; an explicitly authored
per-circuit direction if native art provides no physical sun. Label the last
choice as artistic. Do not import the peer's 45-degree elevation or claim a
screen-space flare establishes elevation. Incompatible/unknown circuits stay
in Original mode until their policy is reviewed.

Keep the original sky presentation for the first milestone. Promise correct
shadow direction under the selected world-light policy, and shared visible
bearing only where demonstrated. Exact 3D alignment requires a later optional
sky/sun projection using that same canonical direction and angular extent.

## Phase 1: integrate optional shadows

### Ownership and parameter contract

Keep Lamborghini discovery, circuit/phase gates and caster/receiver identity
in proposed port modules src/lambo_rt_shadows.h/.cpp. Keep reusable buffer,
pipeline, descriptor, AS and shader integration in a reviewable RT64 patch,
listed and applied through the existing patch/build system. Do not give RT64
new hard-coded Lamborghini RAM addresses. Preserve source licenses and
attribution when adapting peer code.

Use one small bridge: a value snapshot plus admitted draw metadata, tied to
the graphics task and copied into its Workload. It should contain validity,
scene/load epoch, task sequence, circuit/phase, light source identity, unit
receiver-to-light direction, angular radius in radians and shadow strength.
Capture settings with it. Native RGB light records may be recorded for future
work, but phase 1 does not introduce new RGB lighting or a second Lambert term.

Capture on the game-owned task-production boundary established in phase 0;
consume on the graphics/HLE producer before publishing the matching workload.
Fence task reuse as the existing sky lists do. Renderer workers use the copied
value and never look up changing guest RAM or a singleton latest sun. Scene
epochs clear stale values at reload, warp, save-state restore, menu transition
and shutdown. Later split-screen uses one world light with separate views.

Declare a named shared CPU/HLSL parameter layout with explicit aligned fields
for direction/strength, cone radius/ray interval, sample count, validity and
debug mode. Assert size, alignment and member offsets on CPU, and exercise the
uploaded layout in the GPU probe. Treat epoch/task identity as CPU ownership
metadata, rather than packing host bools/pointers into a constant buffer.

### Geometry and pipeline

Use RT64's current presented world-position buffer and original validated
face-index ranges after interpolation, with appropriate resource barriers.
Do not build from last simulation pose or inverse-project depth alone. Keep
the exact draw/primitive mapping for receiver exclusion; skip vertex-test/index
rewrite paths until that mapping is established. Include the receiver itself
as a caster where physical self-shadowing needs it; exclude only its originating
face, not every face in the same draw. Geometric normals come from presented
triangles for bias. Vertex RGB bytes are not proof of shading normals.

Create shadow-only BLAS/TLAS resources for admitted geometry. Maintain explicit
metadata linking each candidate to draw, face range and caster/material policy.
Initially rebuild dynamic/presented geometry as needed; defer static/dynamic
partitioning until timestamps show the cost. Prevent previous-frame resources
from masquerading as ready for a new task. Expose triangle count, rejected
ranges, AS bytes, readiness and rejection reason in debug diagnostics.

Patch descriptor layouts, framebuffer selection, vertex inputs/world-position
varying and shader compilation together. Keep the existing raster path for
ineligible draws. Compile the new inline-query shader at SM 6.5 without changing
every native shader target. Verify a matched DXC/compiler/runtime/validator
version; the peer explicitly acquires a newer toolchain, whereas Lamborghini's
pin uses bundled DXC. The [upstream build][rt64-current-build] still targets
ordinary pixel shaders at 6.3; do not assume those flags compile RayQuery.

### Sampling, material and fog behavior

Sample ray directions over the emitter cone around the normalized world
direction using deterministic equal-area strata and a stable world-space basis,
independent of receiver normal, camera and viewport. Use one geometrically biased
receiver origin, finite scale-aware ray bounds and configurable angular radius.
Zero radius uses the exact hard query. Quality levels 4/8/16 cover the whole
cone independently; reducing samples must not shrink it. Treat softness as an
authored angle, not the screen sprite's pixel radius. Start without temporal
history; measure visible banding/noise before adding a denoiser.

Average visibility for all admitted casters across those rays. Do not preserve
the peer's hard scenery early return in soft mode. Any broad-phase rejection
must contain the entire cone and finite ray interval; use no such optimization
until it is proven conservative. Reject only the exact receiver face, with
geometric bias that is stable across triangle winding and camera orientation.

Run the native material/alpha/depth logic first. Restrict receiving to proven
opaque pass-through or standard fog paths with valid finite parameters. For
these paths compose `C = T * C_native + (1 - T) * C_fog`, where `T` is the
bounded artistic shadow transmission and `C_fog` is the fog contribution already
in native output (zero for the supported unfogged path). Fully fogged pixels
therefore remain fog-colored. Preserve alpha, coverage, depth and blending.
Unknown combiners, forced blend, decals, emissive effects and diagnostic
highlight paths retain their native output. This is visibility attenuation of
native shading; separation of direct and ambient light is phase 2.

Initially admit opaque solids. Admit an alpha-tested caster only after native
texture/tile/wrap/combiner/alpha-compare coverage can be evaluated at ray hits,
including the active texture replacement. Unsupported cutouts/translucency stay
excluded with a diagnostic; they must not become solid rectangles. Thin-fence
proxies require separate Lamborghini evidence, not copied Wave Race dimensions.

Suppress the identified native car-shadow overlay only when the matching view
has a valid light, scene, replacement receiver coverage and ready pipeline.
Keep it in Original mode and on every fallback. Do not remove every translucent
draw sharing a render mode. If classification or replacement coverage is
unproven, leave this scene unsupported rather than stacking two car shadows.

### Settings and failure behavior

Proposed settings: Shadows = Original / Ray traced (default Original), quality
= 4/8/16 rays, and softness in degrees bounded to 0 through 5, converted once
to radians in the snapshot. Select the per-circuit initial angle after phase 0;
it is an authored value, not a measured sun size. These are proposed controls, not
current schema. Expose dependent controls only when usable; save preferences
even when the active device cannot run them. Low hardware stages Original with
the existing Apply/Discard flow. Apply the setting snapshot to future tasks;
queued tasks retain their own settings.

Check the actual selected backend after Auto or backend retry, DXR 1.1, SM 6.5,
toolchain availability and pipeline readiness. Keep Original rendering while
compilation is pending or scene/light/resource validation fails; log a concise
reason. Opting in must not force a working Vulkan game onto D3D12. Invalidate
shadow resources on device loss and verify the renderer's actual failure path;
this review confirms setup-time backend retry, not runtime device recovery.
Disabled mode allocates/builds no shadow AS and retains baseline
draw selection. Headless software rendering stays native and remains a CPU
diagnostic; it does not implement or validate hardware shadows.

### Owning files for implementation

| Change | Owner/files |
| --- | --- |
| Sun/task/scene policy and safe guest hook | New src/lambo_rt_shadows.h/.cpp; src/rt64_renderer.cpp; scripts/gen_syms_toml.py only if a new source hook is needed |
| Setting persistence and availability | src/lambo_config.h/.cpp; src/ui/lambo_frontend_settings.cpp; existing live-config/frontend tests |
| Workload value and draw metadata seam | RT64 patch covering hle/rt64_state.cpp, hle/rt64_workload.h and selected draw metadata, without game RAM readers |
| Presented geometry, AS and receiver pipeline | RT64 patch covering render/rt64_framebuffer_renderer.h/.cpp, descriptor/shader library integration and a shadow-scene resource module |
| Native receiver and ray-hit coverage | RT64 patch/new shader includes plus selected world-position vertex path; retain RasterPS and existing native material helpers |
| Shader/toolchain and patch application | CMakeLists.txt, patch inventory, build.ps1/build.sh/Android application rules as applicable; compile out unsupported backend paths cleanly |
| Validation and claims | Host bridge/config tests; new production-shader GPU probe; docs/testing.md and a settled shadows evidence page after implementation |

These are proposed locations and responsibilities. New renderer APIs and the
patch filename are chosen when phase 0 establishes the smallest required seam.

## Required validation and release gate

| Layer | Observable acceptance criterion |
| --- | --- |
| CPU bridge | Fixture-backed provenance, endian/bounds validation, invalid-light fallback and task/epoch isolation; camera/FOV/car orientation/sun visibility cannot rotate a fixed world light |
| CPU settings | Original default; save/reload; quality/angle bounds; backend capability fallback; Low hardware Apply/Discard; queued task settings remain consistent |
| Production GPU probe | Compile and execute the actual shader/material/AS path, not a CPU copy; upload ABI sentinels and assert image/readback results on an RT-capable adapter |
| Hard/soft correctness | Zero-angle hard parity; larger angle broadens penumbra; raising the same caster broadens it; near contact stays tight; evaluate against analytic fixture bounds and a high-sample GPU reference |
| Material correctness | Native texture/alpha/depth/coverage parity; cutout holes transmit; supported fog color remains unchanged; unsupported materials match native pixels; closed solids self-shadow while the exact receiving face does not |
| Native overlay/fallback | One car shadow when ready; original overlay restored when disabled, pending or invalid; Original mode captures match the pre-change build with identical settings |
| Real RT64 game output | Replay the proven Circuit 1 scene in a real D3D12 window and capture the swapchain; stable light under camera changes, FOV/aspect changes, sun offscreen and native/display-rate presentation |
| Lifecycle | Toggle/resize/MSAA/HDR variants, warp/load/menu/restore, missing geometry and shader failure; no stale AS, wrong-view light, validation-layer errors or progressive allocation growth |
| Coverage disclosure | Move a caster outside native submission while the receiver remains visible; record the missing shadow until phase 3, including PVS/draw-distance settings |
| Performance | GPU timestamps for AS build and shadow draws, total GPU p50/p95 frame time, CPU submission, rays/pixel, triangles and memory at 1080p/4K and 4/8/16 samples; include interpolated presentation cost and warmed steady state |

Record the GPU model, driver, selected backend, toolchain, ROM hash, source and
patch revisions, settings, replay and capture frames. Select a reference GPU
and frame-time budget before broadening the feature; no measured budget or
speed claim exists yet. Add regression thresholds only after repeatability is
established. Exercise at least the development adapter and a second vendor
before general support claims. A capability check, shader compilation, software
adapter, skipped probe or CPU headless image does not meet the GPU gate.

The existing [automation harness](automation-harness.md) can supply deterministic
guest input. Its normal headless framebuffer is the software renderer's output;
RT visual assertions require a windowed RT64 swapchain capture/readback with
that same input. Keep scenario timing tied to guest frames, not wall-clock
screenshots.

## Follow-on phases

1. **Direct/ambient relighting:** prove genuine lit normals and unshaded material
   inputs on a small surface set. Preserve authored key, fill and ambient colors;
   shadow the sun contribution while retaining fill/ambient/emissive terms.
   Do not multiply prelit/clamped vertex colors by another Lambert term or
   recover albedo by division. Use the same eligible material evaluator at
   future reflection hits. Sky reprojection, roughness, car paint reflections,
   AO/GI and path tracing require separate decisions and measurements.
2. **Offscreen caster coverage:** add authenticated caster submission independent
   of camera/PVS culling, using a conservative light-space region and explicit
   budgets. Establish course asset ownership, geometry decode and invalidation
   under Track Lab/reloads before retaining meshes. Benchmark static BLAS reuse
   and dynamic cars. Native visibility expansion alone is not complete coverage.
3. **Broader scenes/backends:** prove light/material policies on additional
   circuits and races, then two/four-player views and shared resources. Add
   Vulkan ray-query compilation/capabilities and real Linux driver tests before
   claiming cross-platform RT. Android remains a separate experiment.

## Evidence from this planning session

Read-only source inspection and remote revision checks were performed. The USA
ROM filename exists locally, but its contents/hash were not verified here.
This worktree has no generated game directory or built executable. MinGW,
CMake, Ninja, Python and GitHub CLI were discoverable; DXC was not on PATH.
The sibling dependency checkout has its bundled dxc.exe, but this worktree's
submodules and a matched compiler/runtime/validator still need preparation.

No game build, playthrough, ROM-derived test, shader execution, GPU capture or
performance benchmark was run. The prior peer review reported CPU test passes
and a fixture mismatch; those results were not rerun or transferred to this
port. [RaceWave46 issue 8][peer-fixture] already records that scope-test fixture
excluding supported Dolphin Free Ride. It is peer test evidence, not a
Lamborghini defect, implementation task or GPU regression. No duplicate report
is needed.

For this documentation change, the repository's required checks passed:
documentation validation covered 45 Markdown files, the Python host suite ran
47 tests, and Git found no whitespace errors. Exact commands:

~~~text
python tools/check_docs.py
python -m unittest discover tests
git diff --check
~~~

## Next-session prompt

~~~text
Implement the first bounded ray-tracing milestone in Automobili Lamborghini:
Recompiled, using docs/ray-tracing-plan.md. RaceWave46 is peer source only.
Read CLAUDE.md, CONTRIBUTING.md, BUILDING.md and the patch inventory, verify
the current commit/dependencies, and complete phase 0 first: prove native sun
provenance, task ownership, Circuit 1 receivers/casters and original car-shadow
draw identity from the supported USA ROM and reproducible captures.

Then add opt-in shadow-only D3D12 inline ray queries over presented RT64
geometry, with camera-independent lighting and angular soft-shadow sampling.
Keep native material/fog/alpha/depth behavior, restore original shadows on every
fallback, and keep game policy in the port with reusable renderer changes in a
reviewable patch. Validate using production GPU probes and real RT64 swapchain
captures; CPU/headless tests alone do not prove RT output. Keep relighting,
offscreen caster expansion and wider backend/multiplayer support for follow-ons.
If a provenance or overlay gate cannot be proved, deliver the measured finding
and bounded GPU groundwork without claiming a working game enhancement.
Use finish when done.
~~~

[peer-light]: https://github.com/DomazinUS/RaceWave46/blob/5c34f3711426978107707a8f06af9c2af4ee5401/lib/rt64/src/common/rt64_wr64_rt_shadow.h
[peer-celestial]: https://github.com/DomazinUS/RaceWave46/blob/5c34f3711426978107707a8f06af9c2af4ee5401/lib/rt64/src/common/rt64_wr64_rt_celestial.h
[peer-world]: https://github.com/DomazinUS/RaceWave46/blob/5c34f3711426978107707a8f06af9c2af4ee5401/lib/rt64/src/shaders/Wr64WorldShadowPS.hlsl
[peer-shadow]: https://github.com/DomazinUS/RaceWave46/blob/5c34f3711426978107707a8f06af9c2af4ee5401/lib/rt64/src/shaders/Wr64WaterShadow.hlsli
[peer-scene]: https://github.com/DomazinUS/RaceWave46/blob/5c34f3711426978107707a8f06af9c2af4ee5401/lib/rt64/src/render/rt64_wr64_rt_scene.cpp
[peer-device]: https://github.com/DomazinUS/RaceWave46/blob/5c34f3711426978107707a8f06af9c2af4ee5401/lib/rt64/src/render/rt64_wr64_rt_water.cpp
[peer-gpu]: https://github.com/DomazinUS/RaceWave46/blob/5c34f3711426978107707a8f06af9c2af4ee5401/tests/rt_reflections/CMakeLists.txt
[peer-fixture]: https://github.com/DomazinUS/RaceWave46/issues/8
[rt64-workload]: https://github.com/rt64/rt64/blob/f0728a2520d5aa735886240de3fee75cc805f6d6/src/hle/rt64_workload.h
[rt64-world]: https://github.com/rt64/rt64/blob/f0728a2520d5aa735886240de3fee75cc805f6d6/src/shaders/RSPWorldCS.hlsl
[rt64-raster-vs]: https://github.com/rt64/rt64/blob/f0728a2520d5aa735886240de3fee75cc805f6d6/src/shaders/RasterVS.hlsl
[rt64-process]: https://github.com/rt64/rt64/blob/f0728a2520d5aa735886240de3fee75cc805f6d6/src/shaders/RSPProcessCS.hlsl
[rt64-current-build]: https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/CMakeLists.txt
