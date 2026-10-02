# Sunlight ray-query groundwork

Status: experimental developer groundwork, measured on Windows on 2026-10-02.
The GPU builder and angular-shadow kernel run on hardware. **In-game ray-traced
shadows are not enabled.** The later [Circuit 1 evidence](rt-provenance.md)
establishes native world-key direction, stationary camera independence and
car-overlay provenance. Road/caster eligibility and native overlay replacement
coverage in the [implementation plan](ray-tracing-plan.md) remain open.
There is no player shadow setting or new backend selection behavior.

## Delivered boundary

[lambo_rt_shadows.cpp](../src/lambo_rt_shadows.cpp) records native sun-art inputs
on the game producer, keyed by the exact graphics-task arena. The HLE consumer
takes a copy before interpreting that task. Opt-in bounded task snapshots and
emitter spans now support offline provenance checks. It logs provenance and never supplies
a physical light, admits a game material or changes draw selection.

[Patch 0021](../patches/0021-rt64-sun-shadow-groundwork.patch) adds:

- `SunShadowScene`: shadow-only BLAS/TLAS over caller-supplied presented float4
  positions and original uint32 indices. Selected ranges and every referenced
  vertex index are validated against the matching CPU index upload and supplied
  actual GPU allocation capacities. Undersized buffers fail before AS allocation. Only
  explicitly admitted opaque solids qualify; cutouts/translucency stay excluded.
- `SunShadowParams`: a shared 48-byte aligned CPU/HLSL layout, offset assertions
  and finite/unit/bounds validation. Direction points from receiver toward the
  light; angular radius is radians, limited to 0 through 5 degrees.
- `SunShadow.hlsli`: equal-area solid-angle strata using a basis derived only
  from the world light. Each 4/8/16-ray quality spans the emitter; zero radius
  performs one hard query. Every soft ray tests the whole admitted scene.
- Exact originating-face exclusion, rather than excluding the entire receiver
  draw. Geometric bias faces the light, with a canonical world normal at tangency,
  and does not flip with triangle winding.
- Standard-fog composition that attenuates the already shaded surface, preserves
  the existing fog contribution and native alpha, and adds no Lambert relighting.
  The caller must run native material/alpha/depth logic first and prove eligibility.

An unused scene allocates nothing. Readiness belongs to one nonzero presentation
identity; failed preparation invalidates it. The owner must fence all previous
builds and queries before preparing, invalidating, destroying or changing input
buffers. Separate in-flight views/presentations require separate instances.
Allocations are recreated after that fence; reuse needs real game timings.
Budgets are 4096 ranges, 262144 triangles, 128 MiB AS and 128 MiB scratch. There
is no water, guest-memory reader, automatic submission or wait inside the helper.

CMake applies the patch idempotently after the scripts' normal patch set. Normal
builds compile the scene module but no SM6.5 shadow pipeline. The opt-in probe
compiles the shared kernel. No RasterVS varying, receiver pipeline, Workload light
field or live framebuffer integration exists yet. Tests must be interpreted at
this boundary, not as validation of native game materials or depth/coverage.

## Native provenance finding

The fresh USA recompile confirms that `func_80036854` starts at runtime
`0x80035C54` (function names retain the splat offset). Before visibility branches
it subtracts camera heading from the track's authored bearing, then uses a
camera-derived vertical term to place the flare. It reads no physical
three-component world sun or angular radius.

`boot_pad_apply_calibration` loads the bearing at `0x80006904` from
`trackData + 0x1E4 + selection * 28`. Heading is the truncated planar angle from
`func_80037F5C`, whose quadrant constants are 90, 180 and 270 degrees. The vertical
term at `0x80035BEC` is 256 times a normalized camera-vector component. These
facts establish native art provenance. The subsequent [native key measurement](rt-provenance.md#world-light-and-camera-independence)
establishes world axes and a directional key independently of this art term.
Do not turn the vertical screen term into a physical sun elevation.
Any future artistic elevation must be labeled as authored.

The initial `rt-sun-provenance` measurement at `517953a` turned through Circuit 1.
In the logged samples,
bearing was 186 degrees, camera heading ranged from 69 to 316 degrees, and the
vertical term ranged from about 28.264 to 28.354. Logging includes camera records
only for task sequences 1 through 12 and then multiples of 60; tasks with no
captured camera produce no log. This samples task sequences, not every frame or
every camera. These numbers do not establish extrema or constant bearing for
unlogged tasks. They are moving-car observations, not a fixed-position camera
experiment. Both task arenas supplied copied records.
Native camera scratch slot 1 appeared in the one-player run; that index is not
a zero-based viewport ID.

The caller at `0x800053C4..0x800053F0` invokes the flare only with fewer than two
players and skips city circuit index 4. The flare hook alone cannot prove a
shared light for two-player views or city scenery. The camera epilogue hook now
captures camera inputs even when the flare is skipped; only Circuit 1 one/two-player
light captures have been measured. Missing records stay missing on task reuse.

Material lead `C8104A50` is emitted by `func_800165FC` at `0x80015B38`. That
function has an iterative draw path and two frame-rendering call sites. Its
signature and the software renderer's shadow comment do not establish which
RT64 draws are native car shadows. [Task captures](rt-provenance.md#exact-overlay-provenance-and-material-counterexample)
now distinguish that trail emitter from the car-parented overlay and falsify
render-mode-only identity. No draw was suppressed. Receiver/caster eligibility
and overlay replacement coverage still need proof before using the kernel.

## Guest bridge contract

The producer hooks run only with `LAMBO_RT_SUN_PROBE=1`, captured at process
startup. They read low 8 MiB USA RAM through the runtime's word-swapped layout:
u32/float at native word offsets, s16 at offset XOR 2. They never write RAM.
Null/short RAM, unknown task arenas, camera indices outside 0 through 3 and
nonfinite height are rejected. This narrow diagnostic is not a world-light API.

| Field | USA address/layout | Width and units |
| --- | --- | --- |
| Current task | `0x800A2BFC` | u32 guest pointer |
| Task arenas | `0x800BF240`, `0x800C6C90` | task pointers, stride `0x7A50` |
| Root list | task + `0x1C0` | exact physical, KSEG0 or KSEG1 root address |
| Phase | `0x800CE6AC` | s16 game state |
| Circuit | `0x800CE794` | s16 circuit index |
| Players | `0x800CE6A4` | s16 player count |
| Camera scratch slot | `0x800CE6AA` | s16 index used by flare; viewport mapping unproved |
| Art bearing | `0x800A2FB8` | s16 degrees |
| Camera heading | `0x800A2F10 + 2 * camera slot` | s16 degrees |
| Vertical art term | `0x800A2F90` | float camera-derived screen term |

Hooks at `0x8000102C` in `BootLoadInitialAssets` and `0x80002560` in
`func_800030F8` begin records after native slot/list selection. `0x80035C54`
captures before flare visibility culling. The existing task reuse fence protects
producer storage until HLE consumption; see [sky ownership](sky-panorama.md).
A mutex publishes copies between game and graphics threads. Workers never read
guest RAM. Phase/circuit transitions advance the epoch and clear pending records;
save-state restore and renderer shutdown explicitly invalidate them. Records can
be consumed once across all accepted address aliases. The consumer normalizes
physical, KSEG0 and KSEG1 roots to the same arena; other segments and interior
list addresses are rejected without consuming a pending record. Log sampling
happens after consumption and does not change task ownership. The later GPU
Workload bridge still needs implementation.

The additional camera/emitter hooks and `LAMBO_RT_CAPTURE_DIR` capture path are
specified in the [extended diagnostic contract](rt-provenance.md#capture-and-offline-bridge-contract).
The game producer copies low RAM before native queue publication for these
four local snapshots; HLE writes immutable task-owned bytes and workers never
read RAM. The task reuse fence alone does not own mutable object globals.
Offline observations do not publish runtime eligibility.

## Reproduce validation

Use [BUILDING.md](../BUILDING.md) and its MinGW/Python-CMake PATH. Regenerate
configuration after changing hooks. Do not configure while Ninja is running.

~~~powershell
python scripts/gen_syms_toml.py
./build.ps1
cmake -S . -B build -DLAMBO_RT_GPU_TESTS=ON
cmake --build build --target lambo_rt_shadow_gpu lambo_rt_sun_tests -j 4
ctest --test-dir build -R '^lambo_rt_(sun_tasks|shadow_gpu)$' --output-on-failure
ctest --test-dir build -R '^lambo_' --output-on-failure
./build/lambo_rt_shadow_gpu.exe ./build/sun-shadow-probe.dxil
python tools/run_game_scenario.py scenarios/rt-sun-provenance.json
~~~

`LAMBO_RT_DXC` can select a compiler beside its matching dxcompiler.dll/dxil.dll.
The bundled toolchain compiled the kernel at SM6.5: DXC
`1.7.0.4147 (0dc8d9060)`, validator `1.7 (101.7.2212.14)`. Ordinary pixel shaders
keep their targets. The probe rejects software adapters and checks actual DXR1.1
and SM6.5 on the selected D3D12 device. Exit 77 means skipped hardware, not pass.

The probe uploads synthetic indexed geometry, uses the actual patched AS helper
and query/composition kernel, fences execution and reads GPU output. Independent
dense polar integration supplies soft visibility references. When installed, the
D3D12 debug layer reports errors/corruption as test failures. No ROM bytes or
textures are embedded in the fixtures.

Measured on NVIDIA RTX 3080, Windows driver 32.0.16.1088, D3D12: 9166 checks
passed with the debug layer enabled and no reported errors. The two-triangle
scene used 2816 AS bytes and 2304 scratch bytes.

| Rays | Partially shadowed samples of 129 | Mean absolute visibility error |
| --- | --- | --- |
| 4 | 30 | 0.028290 |
| 8 | 34 | 0.012741 |
| 16 | 34 | 0.006783 |

GPU timestamps separately measure AS work and query/composition dispatch. Across
two mixed cold fixture runs, AS averages ranged from about 54 to 193 microseconds
and query/composition averages from 14 to 47 microseconds. These tiny-fixture
numbers do not predict game frame times. Warmed 1080p/4K
game scenes, interpolation cost, p50/p95, failures and a second vendor remain
unmeasured.

The supported build freshly generated RecompiledFuncs and src/aspMain.cpp from
USA ROM SHA-256
`CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.
Generated files were neither edited nor committed. Windows RDRAM allocation,
headless harness-smoke, windowed sky-turning-smoke and the provenance scenario
passed. Windowed swaps/replay/logs establish native task consumption, not RT game
pixels. No swapchain pixel comparison or ray-traced game playthrough is claimed.
All 45 project CTests, 49 Python tests and the documentation check passed.
The unrestricted CTest run also launched unchanged third-party zstd stress tests;
the long-running fuzzer was interrupted after the project tests passed. The
remaining third-party compression stress tests are not validation of this feature.

## Remaining gates

1. Turn the measured Circuit 1 native key into a reviewed world-light policy;
   verify remaining FOV/mode/circuit behavior. Fixed-car camera and two-view
   diagnostics passed. No peer course parameters enter this policy.
2. Prove road/fog receiver coverage and opaque car/scenery admission in actual
   RT64 presented draws. Native car/overlay record provenance is established;
   a swapchain differential and replacement coverage remain open before any
   shadow suppression.
3. Copy physical-light/settings and admitted draws into the matching Workload;
   add receiver vertex/pixel pipelines, descriptors, presented-geometry barriers,
   readiness/resource failure handling and budgets.
4. Add Original/default and opt-in settings with a usable pipeline. Validate game
   swapchain pixels, fallback, task/load/resize transitions, timings and missing
   offscreen casters. This prerequisite milestone does not meet these game gates.

Relighting, offscreen caster submission and wider scenes/backends remain the
[follow-on phases](ray-tracing-plan.md#follow-on-phases).
