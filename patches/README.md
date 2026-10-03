# Dependency patch inventory

These patches are part of the build input. They are applied to pinned public
submodules by the build scripts or by CMake. They are not a private fork.

The patch files are the source of truth for exact hunks. The descriptions below
explain why each patch exists and which changes need comparison with upstream.

A patch file is itself a diff, so a blank context line inside a hunk is a line
holding one space — exactly what `git diff` emits. Never strip that space:
after a CRLF checkout (standard on Windows CI) a stripped blank line becomes
a lone carriage return and `git apply` rejects the whole patch as corrupt.
`.gitattributes` forces LF checkouts for `*.patch` and exempts their
significant single-space lines from `git diff --check`;
`tests/test_patch_line_endings.py` enforces both properties.

| Patch | Dependency | Purpose and current status |
| --- | --- | --- |
| 0001 | N64ModernRuntime | Scheduler dispatch, audio/VI runtime behavior, and port runtime integration. Required on desktop and Android. Separate generic runtime behavior from game policy before proposing a fix. |
| 0004 | Plume inside RT64 | MinGW/D3D12 COM ABI struct-return compatibility. Windows-only. Compare with current Plume before proposing a generic fix. |
| 0005 | RT64 | MinGW compiler compatibility in the texture hasher. Windows-only. Compare with current RT64 before proposing a generic fix. |
| 0006 | RT64 | Frame-interpolation transform matching used by this port. All desktop/Android paths that use the patch series. Needs a generic reproducer before proposing a fix. |
| 0007 | N64ModernRuntime | Save-state thread-context relinking used by the port's developer save-state tool. Not a player quick-save guarantee. |
| 0009 | RT64 | Widescreen split-screen viewport origin used by the Lamborghini HUD path. Project-specific unless another game needs the same API. |
| 0010 | RT64 | Intel automatic backend-selection workaround. Keep tied to a reproducible device and driver case. |
| 0011 | RT64 | Explicit backdrop tag, world-matched horizontal projection and independent vertical coverage. The port extends panorama tiles; see [sky motion evidence](../docs/sky-panorama.md). |
| 0012 | N64ModernRuntime | Lazy RDRAM commitment and the source file required by this CMake build. Required on desktop and Android. |
| 0013 | RT64 | Android cross-build support for host shader/compiler inputs. Android-only local build integration. |
| 0014 | Plume inside RT64 | Android SDL/Vulkan window integration. Android-only. |
| 0015 | SDL dependency | Android USB receiver registration compatibility. Android-only. |
| 0016 | N64ModernRuntime | Host-owned configuration storage for the frontend integration. Applied by CMake; project integration. |
| 0017 | RecompFrontend | Host event handling and Lamborghini frontend integration, including controller menu actions resolved from the pressing device instead of only player one, and restoring mod controls after a rejected load ([upstream proposal](https://github.com/N64Recomp/RecompFrontend/issues/43)). Also stubs the NFD-backed file dialogs on Android, where the build neither fetches nor links nativefiledialog-extended (see 0013); the Activity imports the ROM and GPU driver instead. Applied by CMake; the player-one-only lookup still exists on upstream `main`, so this hunk is a local patch with a recorded upstream comparison. |
| 0018 | N64ModernRuntime | Game presentation behavior used by the launcher and normal game path. Applied by CMake; project integration. |
| 0019 | RecompFrontend | The shared prompt dismisses on the mapped Back action, so a controller's cancel button backs out of the quit confirmation. Applied by CMake after 0017. Compared with upstream `main`, which builds the prompt with no element that listens for menu actions, so no controller action can dismiss it. Generic frontend behavior, kept local. |
| 0020 | RecompFrontend | Enum selections wrap within the options column so the window-size presets remain readable. Applied by CMake after 0019. Compared on 2026-10-01 with [upstream enum rendering](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/config/ui_config_option.cpp), which creates the radio group without wrapping. Keeps the existing values, callbacks, and navigation. Checked on native Windows at 1600x900: all twelve choices fit two rows, and selecting 1920x1200 then Apply saves that size. The frontend settings test and idempotent CMake application pass; other platforms remain unverified. |
| 0021 | RT64 | Shadow-only AS resources, shared parameter ABI and angular-query/fog kernel. Applied by CMake. Developer groundwork only; game receivers remain gated. Upstream comparison and hardware probe evidence are below. |
| 0022 | RT64 | Opt-in queue-owned material/raster evidence and fenced GPU/present observation. Thread-local raster scope excludes HLE's framebuffer renderer. Port-owned capture policy; no sunlight Workload fields or production receiver integration. |
| 0023 | Plume inside RT64 | D3D12 texture-to-buffer readback selects source sample positions when destination is a placed-footprint buffer, avoiding a null texture dereference. ROM-free 7x3 padded-row GPU regression. |
| 0024 | RT64 | Optional D3D12 owner-output pipelines mirror native raster clipping, material/discard and depth behavior, including the generic ubershader fallback, into an R32G32_UINT diagnostic target with cloned depth. On RTX 3080 captures, enabling the diagnostic leaves the actual swapchain byte-identical. It is evidence instrumentation only; it does not implement shadow replacement. |
| 0025 | RT64 | Adds immutable sunlight and separate caster/receiver admission metadata to the matching Workload through the developer evidence observer. HLE copies projection address identity alongside transform groups. Typed screen/backdrop exclusions retain zero face-index extent for non-indexed draws. The latest 48 model-0/one-player task captures have no unclassified draws, but all fail closed on native texture-alpha coverage and blended car geometry. No production receiver consumes these values and no native overlay is suppressed. |

## Application matrix

Patch **0021** adds a shadow-only AS helper, shared aligned sunlight parameters
and an angular-query/fog kernel. CMake applies it after the existing RT64 series
on every build path, idempotently. The game does not instantiate the helper or
select shadow receivers yet; Windows SM6.5 compilation/GPU tests are opt-in.
See [groundwork evidence](../docs/rt-shadows.md) for the hardware checks and open
integration gates. The AS barrier pattern was informed by RaceWave46's MIT
Wr64RTScene; game addresses, presets, materials and water coupling were not copied.

Compared on 2026-10-02 with RT64 upstream
`43373749dac9bbc1b653e6a02aed40a9e1783bed`: its
[framebuffer renderer](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/render/rt64_framebuffer_renderer.cpp)
still references the conditional full RT path; its
[native pixel shader](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/shaders/RasterPS.hlsl)
does not implement this angular-query kernel. This local groundwork is not an
upstream-supported feature or dependency upgrade. No upstream issue was opened.

Patches **0022/0023/0024/0025** are applied idempotently by CMake on all supported build
paths. Readback is an explicit D3D12 developer diagnostic; normal rendering has
no observer or capture GPU allocations. The observer registers before queues
start and is cleared after they join. Its scoped raster activation prevents
HLE calls from entering queue-owned mutable capture state. Ownership, current
measurements and remaining gates are in [presented evidence](../docs/rt-material-evidence.md).
The owner map authenticates the exact draw that won native depth and the
post-omission visible receiver at each VI filtering tap in the measured
one-player matrix. Patch 0025 carries diagnostic metadata into its exact Workload,
but sampled caster coverage is incomplete and no production receiver consumes it.

The continuation separates receiver RGB/fog constraints from opaque caster
coverage and records explicit screen/backdrop/HUD exclusions. The copied
projection address is generic CPU provenance, with no game RAM reader in RT64;
the port owns the USA HUD address and material policy. A preserving Windows
build and all 48 project CTests pass. Twelve fresh circuit/mode replays have
zero unclassified draws in all 48 tasks, with byte-identical native swapchain
output. Texture-alpha and blended car ranges still keep every Workload incomplete.
The current patch series matches all 31 modified/new RT64 source files in a
separate pinned replay. Detailed policy evidence is in
[material admission](../docs/rt-material-evidence.md#separate-caster-and-receiver-policies).

Compared on 2026-10-02 with RT64
`43373749dac9bbc1b653e6a02aed40a9e1783bed`: its
[render hooks](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/rhi/rt64_render_hooks.h)
provide init/draw/deinit callbacks but no queue/fence/present evidence observer.
Compared with Plume `d72379344dacd3dbf9f810f92ddc87e6de1845b1`: its
[D3D12 copy implementation](https://github.com/renderbag/plume/blob/d72379344dacd3dbf9f810f92ddc87e6de1845b1/plume_d3d12.cpp)
still calls `setSamplePositions(dstLocation.texture)` unconditionally, although
a placed-footprint destination has a buffer and no texture. Patch 0023 selects
the source texture in that case. These are local generic changes, not upstream
support claims; no upstream issue was opened.

Compared on 2026-10-03 with current RT64 upstream `main` at
`43373749dac9bbc1b653e6a02aed40a9e1783bed` (verified as the branch head) and
the pinned source at `f0728a2520d5aa735886240de3fee75cc805f6d6`: the current
[framebuffer renderer](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/render/rt64_framebuffer_renderer.cpp)
and [raster shader](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/render/rt64_raster_shader.cpp)
have no owner target allocation/depth clone, mirrored raster pass, or integer
owner-output pipeline. Patch 0024 adds opt-in D3D12 diagnostic instrumentation
for those behaviors; it does not add shadow replacement or upstream support.
The full patch was replayed after 0005/0006/0009/0010/0011/0021/0022 on a
separate worktree from the pin, then matched all 29 changed/new RT64 source
paths byte-for-byte against the live patched checkout (excluding the nested
Plume submodule). No upstream issue was opened.

Compared on 2026-10-03 with RT64 upstream `main`, its [Workload](https://github.com/rt64/rt64/blob/main/src/hle/rt64_workload.h)
has no sunlight metadata field and its [render hooks](https://github.com/rt64/rt64/blob/main/src/rhi/rt64_render_hooks.h)
have no matching Workload callback. Patch 0025 adds that immutable diagnostic
seam only; it does not add a game receiver or suppression path. No upstream
issue was opened. The patch applies cleanly to the pinned RT64 tree after
0005/0006/0009/0010/0011/0021/0022/0024, and the supported CMake build recognizes
0025 as the latest idempotence guard before skipping or replaying the RT64 patch
series.

For an initialized Windows checkout with local dependency edits, use
`./build.ps1 -PreserveSubmodules`. It verifies recursive dependency pins and
skips checkout/clean operations. Like CMake, it recognizes 0025 before
revisiting earlier RT64 patches whose contexts have changed. The continuation
replayed the nine Windows RT64 patches in a separate clean pinned worktree:
all 31 modified/new files matched the active patched dependency byte-for-byte.
No patch content or dependency pin changed in that continuation.

| Build path | Applies |
| --- | --- |
| Linux script | 0001, 0007, 0012, 0006, 0009, 0010, 0011, then 0016 through 0025 in CMake. |
| Windows script | The Linux set plus 0005 and 0004, then 0016 through 0025 in CMake. |
| Android script | 0001, 0007, 0012, 0006, 0009, 0010, 0011, 0013, 0014, and 0015, then 0016 through 0025 in CMake. |

If a patch no longer applies to its pinned submodule, stop and update the
patch or pin as a deliberate change. Do not reset a developer's unrelated
submodule work to make the build pass.

## Renderer boundary

RT64 supplies the general N64 renderer, texture replacement, and standard
F3DEX or extended-GBI handling. The port owns Lamborghini-specific display-list
hooks, projection policy, split-screen policy, texture paths, and defaults.

The project does not maintain a private RT64 fork. The local renderer-related
exceptions are visible in this patch list: interpolation matching (0006),
backdrop behavior (0011), split-screen viewport origin (0009), backend selection
policy (0010), and platform compatibility (0004,
0005, 0013-0015). `src/stub_renderer.cpp` is a separate port-owned diagnostic
renderer, not an RT64 feature.

The current boundary is imperfect where a patch combines reusable runtime
behavior with Lamborghini policy. That is a maintenance risk, not proof that
the patch violates an upstream contract. Separate those concerns before
replacing the patch.

Do not call a local patch upstream-supported without checking the pinned source
and the current upstream project. Record that comparison, the local purpose, the
test, and the limitation here and in the pull request. This project does not open
issues on the upstream projects it patches; their maintainers have said they do
not want proposals raised from this repository, so the local record is the only
proposal record.
