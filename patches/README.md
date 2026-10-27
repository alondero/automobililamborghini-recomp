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
| 0001 | N64ModernRuntime | Scheduler dispatch, audio/VI runtime behavior, RDP completion pacing (`dp_pacing.hpp`), and port runtime integration. Required on desktop and Android. Separate generic runtime behavior from game policy before proposing a fix. |
| 0004 | Plume inside RT64 | MinGW/D3D12 COM ABI struct-return compatibility. Windows-only. Compare with current Plume before proposing a generic fix. |
| 0005 | RT64 | MinGW compiler compatibility in the texture hasher. Windows-only. Compare with current RT64 before proposing a generic fix. |
| 0006 | RT64 | Frame-interpolation transform matching used by this port. All desktop/Android paths that use the patch series. Needs a generic reproducer before proposing a fix. |
| 0007 | N64ModernRuntime | Save-state thread-context relinking used by the port's developer save-state tool. Not a player quick-save guarantee. |
| 0009 | RT64 | Widescreen split-screen viewport origin used by the Lamborghini HUD path. Project-specific unless another game needs the same API. |
| 0010 | RT64 | Intel automatic backend-selection workaround. Keep tied to a reproducible device and driver case. |
| 0011 | RT64 | Explicit backdrop tag, world-matched horizontal projection and independent vertical coverage. The port extends panorama tiles; see [sky motion evidence](../docs/sky-panorama.md). |
| 0012 | N64ModernRuntime | Lazy RDRAM commitment and the source file required by this CMake build. Required on desktop and Android. |
| 0013 | RT64 | Android cross-build support for host shader/compiler inputs. Its startup check rejects drivers without scalar block layout, which RT64's C++/HLSL shared structures need ([upstream #23](https://github.com/rt64/rt64/issues/23)); drivers without descriptor indexing continue with 0033. Android-only local build integration. |
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
| 0024 | RT64 | Optional D3D12 owner-output pipelines mirror native raster clipping, material/discard and depth behavior, including the generic ubershader fallback, into an R32G32_UINT diagnostic target with cloned depth. On RTX 3080 captures, enabling the diagnostic leaves the actual swapchain byte-identical. It is evidence instrumentation only; it does not implement shadow replacement. The precompiled-owner path is `_WIN32`-guarded because DXIL blobs are only generated on Windows (Linux failed with undeclared `RasterPSOwnerBlobDXIL`). |
| 0025 | RT64 | Adds immutable sunlight and separate caster/receiver admission metadata to the matching Workload through the developer evidence observer. HLE copies projection address identity alongside transform groups. Typed screen/backdrop exclusions retain zero face-index extent for non-indexed draws. The latest 48 model-0/one-player task captures have no unclassified draws, but all fail closed on native texture-alpha coverage and blended car geometry. No production receiver consumes these values and no native overlay is suppressed. |
| 0026 | RT64 | Extracts native texture/combiner/alpha/coverage evaluation into a shared shader helper, with explicit UV derivatives and unchanged native raster arithmetic. Adds the helper to raster shader build dependencies. All 48 fresh game images match the preceding native build byte-for-byte; 30 synthetic hardware material cases pass. Ray-hit resource/attribute integration is still required and alpha admission remains closed. |
| 0027 | RT64 | Adds generated-attribute reconstruction and explicit native-alpha candidate coverage to the shadow helper. Unsupported input invalidates coverage instead of becoming a solid caster. Thirty synthetic candidate cases agree with native RasterVS/RasterPS ownership; three uncertain shade-alpha/subpixel-LOD paths remain unsupported. Game alpha ranges are still rejected. |
| 0028 | Plume inside RT64 | D3D12 graphics/compute pipeline factories return null when native PSO creation fails. A wrapper with no native handle cannot count as ready. Real failure injection returns null for both factories, then native raster/query submissions still pass. |
| 0029 | RT64 | Opt-in D3D12 game alpha evidence borrows matching native descriptors and selected vertex shaders after the Workload fence. Isolated native owner passes and actual pre-clipping vertex outputs expose coverage disagreements; all game alpha admission remains closed. Raw draws retain zero face start and native rendering remains available. |
| 0030 | RT64 | Optional production sunlight shadows. A port-registered provider is told each Workload id as HLE assigns it, before any queue thread can see it, and attaches admitted metadata to that Workload; RT64 builds a per-Workload shadow acceleration structure from presented world positions, re-rasterizes admitted prelit receivers with an SM6.5 ray-query pass and composites `C = min(C, T*C + (1-T)*fog)` once to RGB only, so a shadow never brightens a pixel and native coverage alpha is kept. The exact native overlays of one view are replaced all-or-nothing; every pending, failed or unsupported gate keeps the native command stream. D3D12 with DXR 1.1 only; single-sample and MSAA targets. Adds GPU timestamps for the AS build and receiver pass. Original mode is byte-identical to the previous build. The DXC options hunk keeps PS/VS-only context so it applies with and without Android-only patch 0013's blank line (their overlap broke the Android release configure). |
| 0031 | RecompFrontend | Normalize document sources to RmlUi URL syntax: forward slashes and a Windows drive separator of \|. Prevents malformed-protocol errors and the red debugger beacon while preserving relative asset paths. Applied by CMake after 0020. Compared 2026-10-08 with pinned source and [upstream main](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/core/ui_context.cpp), both of which use native path text. Supported Windows build, native captures and an RmlUi-backed URL regression pass; other platforms remain unverified. |
| 0032 | RecompFrontend | The settings header keeps Quit and Close inside the modal. The tab row of a tabbed modal takes only the width the header's right side leaves and wraps its labels onto further rows, and the right side never shrinks; before this, tab labels wider than the modal laid Quit and Close out past its edge, where they could not be clicked. Focus order and tab switching follow the tab list, not its rows. Applied by CMake after 0031. Compared on 2026-10-09 with [upstream `main`](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/elements/ui_modal.cpp), which is the pinned commit `b1a1477`: `TabbedModal::initialize_tab` puts the tab set in the header's left side with no way to shrink or wrap. Generic frontend behavior, kept local. Checked on native Windows at 1066x580 and 2560x1440 with eight tabs and, in a throwaway build, twelve: the labels wrap to a second row and Quit and Close stay inside the modal. Those captures were made with this hunk inside a larger local frontend patch; the hunk is byte-identical. Other platforms remain unverified. |
| 0033 | RT64 | Drivers without descriptor indexing get a fixed 8192-slot texture table instead of the variable-count, partially bound one, so `vkAllocateDescriptorSets` no longer fails and crashes on older Android system drivers. Pipeline layouts and the bound set take the same choice from the device capabilities. Every fixed slot holds a cleared 1x1 placeholder until a texture owns it; released cache slots and last frame's dynamic views return to it. Drivers with descriptor indexing (D3D12, desktop Vulkan, Turnip) keep the upstream path with no extra writes. Applied by CMake after 0030. Compared 2026-10-10 with RT64 upstream `main` at `8bd2cf1530c17ce8c3489eb47671ee0da2f8319d`, which still requests the variable-size range unconditionally. The shaders declare non-uniform indexing, which texture LOD needs ([upstream #164](https://github.com/rt64/rt64/issues/164)); 0044 removes it on these drivers. See the [Android validation result](../docs/ANDROID.md#gpu-drivers-and-device-validation). ROM-free layout test plus an Android system-driver run under the Khronos validation layer. No upstream issue was opened. |
| 0034 | RecompFrontend | Enables Refresh during play and lets its callback scan for newly added packages. Clears the old selection before rebuilding the list, avoiding invalid indices when it shrinks. Requires runtime patch 0035 to preserve active handles. Configure recognizes 0034 before checking 0017's overlapping reverse context. Compared 2026-10-17 with the pinned source and [upstream main](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/composites/ui_mod_menu.cpp), which disable Refresh and skip scans during play. Extracted production refresh/update methods pass a Windows host test with UI/runtime fakes; native window interaction and other platforms remain unverified. |
| 0035 | N64ModernRuntime | During play, scans append new filenames while retaining active handles, indices, selection and order. New packages start disabled. Packages needing startup validation keep their restart requirement. Stable handle storage preserves config pointers held by the save thread and UI. Existing filenames are not reopened; replacements and removals take effect on restart. Compared 2026-10-17 with the pinned source and [upstream main](https://github.com/N64Recomp/N64ModernRuntime/blob/main/librecomp/src/mods.cpp), whose scan closes every opened handle and reloads config. Applied by CMake before 0034. The Windows real-loader regression covers live RTZ discovery/toggling, state/pointer preservation, repeated scans and startup-only packages without a ROM or renderer. |
| 0036 | N64ModernRuntime | Defines ModHandle and its code-handle interface before ModContext instantiates deque<ModHandle>. Android NDK 28's libc++ requires a complete element type here; GCC accepted the earlier declaration order. The definitions and stable-storage behavior are unchanged. Applied after 0035. Compared with the pinned header, which places ModHandle after ModContext and originally used vector. An Android ARM64 header compile reproduces the incomplete-type error before this patch and passes after it. |
| 0037 | RecompFrontend | Gates RmlUi debugger initialization, F8 visibility, and shutdown on the existing default-off `general.json` `debug_mode` option. Diagnostics still reach the system logger. Applied through shared CMake on all platforms. Headless tests inject a real diagnostic in both modes and inspect the beacon. The tests exercise the host path only: no full game build, top-level configure, or rendered launcher/settings navigation was run, and other platforms remain unverified. |
| 0038 | RT64 | Makes the sunlight shadow scene robust to view and light. Static scenery, moving casters and remembered static scenery build separate bottom-level structures under instance masks `0x1`, `0x2` and `0x4`, each reading its own metadata. The receiver pass fades the sun shadow where scenery hides most of the sky, adds a short-range contact shadow from moving casters, and takes an occluder mask so a light the art already bakes (tunnel lamps) is blocked by moving casters only. `SunShadowStaticCache` remembers triangles whose exact world positions repeat in nearby tasks and traces those the view no longer presents, so shadows no longer change with the camera; drawn copies are left out so no triangle is traced twice. Debug views show transmission, open sky, contact, and what the remembered scenery changes. Applied by CMake after 0033; configure recognizes 0038 first. Generic renderer behaviour, kept local. Compared 2026-10-22 with RT64 upstream `main` at `de343a53c703f2fc7fdd64ac6ff9e5a3f41e66f3`, whose tree has no shadow source (only `src/shared/rt64_raytracing_params.h`), so there is nothing to upstream against. The tuning defaults (`radius=400`, `keepTasks=300`, `maxTriangles=131072`) are in renderer units measured on this game's tracks. Host tests cover the cache, the D3D12 probe covers masks, the static layer and its rebuild on an RTX 3080, and recorded laps on all six circuits stay on the traced path for every race task. |
| 0039 | RT64 | Reduces shadow traversal work without changing quality settings. Builds the small shadow acceleration structures for fast tracing, and evaluates sky fading after the first blocked sunlight sample so enclosed receivers skip the remaining soft-shadow rays. The original visibility-only kernel retains its exact-face and alpha-coverage contract. Applied by shared CMake after 0038; CMake and Windows preserve mode recognize 0039 first. Compared on 2026-10-24 with upstream `main` at `d9817c68ffd7328cd0af383af124971489366e18`, whose tree contains no SunShadow sources. GPU probes compare sky-faded occlusion against the ungated calculation, including a closed tunnel at every ray quality. Measurements and limits are in [RT shadows](../docs/rt-shadows.md). |
| 0040 | RecompFrontend | Extracts the existing config-open event test into a read-only query shared by frontend dispatch and the port's pre-draw settings refresh. Ordinary gameplay presses avoid schema work; Escape and both remapped menu buttons retain opening-frame freshness. Applied by CMake after 0037. Compared 2026-10-25 with the pinned source and [upstream main](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/base/ui_state.cpp), which keep the test inside event dispatch and expose no query in recompui.h. No upstream issue is opened. The native frontend settings fixture covers keyboard events, both controller bindings, remapping, single-player and multiplayer profiles, and unknown devices. |
| 0041 | Plume inside RT64 | Retries variable descriptor-pool exhaustion using the declared layout capacity; stops on persistent allocation failure. Applied by shared CMake. See the comparison and tests below. |
| 0042 | RT64 | On Android, caps automatic resolution at 2x for the exact Adreno 615 system-driver combination `0x5143` / `0x801f6000` / `Adreno (TM) 615`. Manual choices and other drivers keep their existing scaling. Applied by shared CMake after 0039. The SM-T720 loses the device at its automatic 6x scale but reaches an attract race at 2x. This is a measured workaround; the driver's internal GPU fault remains unverified. Compared 2026-10-26 with pinned RT64 and [upstream main at d9817c6](https://github.com/rt64/rt64/blob/d9817c68ffd7328cd0af383af124971489366e18/src/hle/rt64_workload_queue.cpp), which derive automatic scale from the window without a driver limit. The host fixture executes the production scale-selection switch for Android and desktop; see the [device investigation](../docs/ANDROID.md#adreno-615-startup-device-loss). No upstream issue was opened. |
| 0043 | Plume inside RT64 | Clips Vulkan clear rectangles to the render area and skips a clear with no visible rectangle. RT64 converts inclusive N64 fill rectangles into a clear one pixel wider than the target (321x241 on 320x241, 642x482 on 640x482), which `vkCmdClearAttachments` forbids (`VUID-vkCmdClearAttachments-pRects-00016`). Pixels outside the target do not exist, so the visible result is unchanged. Applied by shared CMake after 0041. Compared 2026-10-26 with pinned Plume `51b1ad4` and [upstream main at 0ce184b](https://github.com/renderbag/plume/blob/0ce184b19270e49236afbb74052a6b267494b86b/plume_vulkan.cpp), which pass rectangles through unchanged. A host fixture runs the production clear path against a recording Vulkan boundary. This does not fix the Adreno 615 device loss; see the [Android investigation](../docs/ANDROID.md#adreno-615-startup-device-loss). No upstream issue was opened. |
| 0044 | Plume inside RT64 | On devices without descriptor indexing, removes non-uniform resource indexing declarations from SPIR-V before `vkCreateShaderModule`: the `ShaderNonUniform` and `*ArrayNonUniformIndexing` capabilities, `NonUniform` decorations, and `SPV_EXT_descriptor_indexing` when no other capability needs it. Those drivers cannot accept them (`VUID-VkShaderModuleCreateInfo-pCode-08740`/`-08742`). Without the declarations, a texture index must be the same across each draw. RT64 varies it within a draw only for RDP texture LOD, which this game never enables ([no_lod audit, Axis D](../docs/no_lod_audit.md)). Devices with descriptor indexing receive the original bytes. Malformed or foreign-endian code passes through. Applied by shared CMake after 0043; it takes the same capability switch as 0033. Compared 2026-10-26 with pinned Plume `51b1ad4` and [upstream main at 0ce184b](https://github.com/renderbag/plume/blob/0ce184b19270e49236afbb74052a6b267494b86b/plume_vulkan.cpp), which pass SPIR-V through unchanged. A host fixture runs the production constructor on synthetic modules and on RT64's RasterPS compiled with the bundled DXC. No upstream issue was opened. |
| 0045 | RecompFrontend | On Android the UI pipeline targets R8G8B8A8_UNORM, the format of RT64's Android swap chain from 0013. Before this, the settings overlay drew with a B8G8R8A8_UNORM pipeline into an R8G8B8A8_UNORM swap chain (`VUID-vkCmdDrawIndexed-renderPass-02684`). The port's earlier in-tree UI made the same Android choice; moving to RecompFrontend lost it. Desktop keeps B8G8R8A8_UNORM. Applied by CMake after 0040. Compared 2026-10-26 with the pinned source and [upstream main at 2ddbfc8](https://github.com/N64Recomp/RecompFrontend/blob/2ddbfc8adcfe9ba26520441f14488861fdcefef7/recompui/src/renderer/ui_renderer.cpp), which hard-code B8G8R8A8_UNORM. A host test compiles both production format choices for Android and desktop and requires them to agree. No upstream issue was opened. |
| 0046 | RT64 | The game's render thread sleeps on a condition variable while the present or workload queue slot it needs is still behind the consumer's barrier. Before this it spun on `cursorMutex`. Issue 329's Android 15 / Adreno 730 traces show RT64 Present blocked in Qualcomm gralloc while the render thread spent about 17.5 s of CPU in `advanceToNextPresent`. Ordering is unchanged: the producer still waits for the same barrier, and the barrier's advance wakes it. This removes the busy core; it does not release the gralloc stall. Applied by shared CMake after 0042. Compared 2026-10-26 with pinned RT64 and [upstream main at d9817c6](https://github.com/rt64/rt64/blob/d9817c68ffd7328cd0af383af124971489366e18/src/hle/rt64_present_queue.cpp), which both spin. A host fixture compiles the patched queue methods, holds each barrier for 400 ms, and requires the producer to stay blocked, wake on the barrier advance, and use under 100 ms of CPU. Clearing the queue's running flag also wakes the producer, so a stalled consumer cannot strand it. The destructors' wake-up line is not covered by the fixture; it is checked by reading. No upstream issue was opened. |
| 0047 | Plume inside RT64 | Creates and replaces Vulkan swap chains within the surface's reported capabilities. See [the swap chain section](#swap-chain-surface-capabilities) for the contract, comparison and tests. Applied by shared CMake after 0044. |
| 0048 | SDL dependency | Android SDL leaves a non-OpenGL window to its own graphics API. Pinned SDL 2 tried to create an EGL surface on every surface change, whatever the window type, and on every surface loss made Android's UI thread poll, up to 49 times 10 ms, for the event loop's EGL context backup. On every pause and resume the event loop also backed up and restored a GL context; for a Vulkan window the restore failed and pushed a spurious `SDL_RENDER_DEVICE_RESET`. The port's window is Vulkan. All of this now requires `SDL_WINDOW_OPENGL`, as window creation already does. The UI-thread wait is also skipped under `SDL_HINT_VIDEO_EXTERNAL_CONTEXT`, because the event loop never backs up an external context, so the wait could only time out. OpenGL windows keep everything, and an external-context OpenGL window still gets the EGL surface that window creation gives it. Debug-priority video log lines record each surface decision; see [surface callback logs](../docs/ANDROID.md#sdl-surface-callback-logs). Applied by `scripts/build_android.py` after 0015. Compared 2026-10-27 with pinned SDL `5d24957`, [the SDL2 branch at 8acf7cf](https://github.com/libsdl-org/SDL/blob/8acf7cfc0f88717e5c036f60f0314bfcafe39189/src/core/android/SDL_android.c#L1055), which still does all of it for every window, and SDL3 main at 4c18c09, which gates the [surface callbacks](https://github.com/libsdl-org/SDL/blob/4c18c09ea58408e476b266b34427b868a08e9fbb/src/core/android/SDL_android.c#L1283) and the [context backup and restore](https://github.com/libsdl-org/SDL/blob/4c18c09ea58408e476b266b34427b868a08e9fbb/src/video/android/SDL_androidevents.c#L36) on `SDL_WINDOW_OPENGL` but does not check the external-context hint in the wait. [DKR-R's patch](https://github.com/ThatGuyMcd/DKR-R/blob/d457741b57afff8d2ec96b2307f11c1340a4162f/patches/android/sdl2-vulkan-surface-lifecycle.patch) gates only the two callbacks and also skips surface recreation for an external context; this patch keeps recreation matched to window creation. `tests/test_android_sdl_surface.py` compiles the patched callbacks and context steps against a recording SDL/EGL boundary for Vulkan, OpenGL, timeout and external-context windows, and replays the SDL series on clean, pre-0048 and conflicting checkouts. Whether the old work contributed to issue 329's gralloc stall is unverified. No upstream issue was opened. |

## Application matrix

Patch 0041 (`0041-plume-variable-descriptor-pool-retry.patch`) changes Plume's
Vulkan descriptor allocation on every platform through shared CMake. A
variable-count pool exhaustion gets one retry using the layout's declared
capacity, preserving the requested count. Other allocations keep their small
pools. A persistent allocation failure logs and stops before callers can use
the failed set. See the [Android investigation](../docs/ANDROID.md#adreno-descriptor-pool-investigation)
and `tests/test_vulkan_descriptor_pool.py` for the captured failure and the
recording-driver regression.

Compared on 2026-10-26 with pinned Plume `51b1ad4` and
[upstream main](https://github.com/renderbag/plume/blob/main/plume_vulkan.cpp):
both size variable pools to the requested count and return after allocation
failure. The independent
[Castlevania port patch](https://github.com/sergiomanzur/cvlod_recomp/blob/master/patches/plume.patch)
instead always reserves the layout capacity. This port retries only when the
driver reports pool exhaustion, preserving normal allocation cost. No upstream
issue was opened. The failure model is tested without a GPU; the reporting
Xiaomi device and its separate Turnip ANR remain unverified on the fix.

### Swap chain surface capabilities

Patch 0047 (`0047-plume-vulkan-surface-capabilities.patch`) changes Plume's
Vulkan swap chain on every platform through shared CMake:

- A failed capability query stops creation instead of using undefined values.
  If the query fails while the swap chain is constructed, format and alpha
  selection is retried on the next resize. A transient failure therefore does
  not leave the swap chain permanently unusable.
- A defined current extent is the surface's size and is used as the image
  extent. Only the special value `0xFFFFFFFF` lets the window size choose it.
  The result is then kept inside the surface's minimum and maximum. A zero
  extent, such as a minimized Win32 window, creates nothing. The chain
  remembers the window size it was built for, so a surface size that differs
  from the window does not cause a rebuild on every frame.
- A maximum image count of zero means no upper limit. Replacements request the
  caller's count again, not the larger count a driver may have returned.
- Images request colour-attachment use only. RT64's final pass, its inspector
  and RecompFrontend's UI only render into swap chain images. No Vulkan code
  copies into, copies from or samples them. The port's swapchain readback in
  `src/lambo_rt_evidence.cpp` runs only on D3D12.
- A surface without the identity transform is refused with a logged reason.
  Pre-rotation needs matching viewport, UI and touch changes, not only a
  different `preTransform`. A surface without opaque or inherit alpha is also
  refused: RT64's final pass does not promise an alpha of one.
- A replacement is created into a temporary handle. The old chain is destroyed
  whether or not creation succeeds. Vulkan retires `oldSwapchain` even when the
  replacement fails. Android's loader then rejects it as `oldSwapchain` with
  `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR` and returns `VK_ERROR_OUT_OF_DATE_KHR`
  from acquire. Before this patch, one failed replacement left Plume holding
  the retired chain, so every later attempt failed the same way.
- Without a chain, the refresh rate is reported as unknown (zero) instead of
  being queried. RT64 reads it every frame, even after a failed resize, and
  Android's loader dereferences the swap chain handle in that query.
- Each creation logs the surface capabilities and the chosen extent, window
  size, image count, usage, transform, alpha, format and present mode. RT64
  retries a failed resize on every frame, so each distinct failure reason is
  logged once.

Compared on 2026-10-27 with pinned Plume `51b1ad4` and
[upstream main at 0ce184b](https://github.com/renderbag/plume/blob/0ce184b19270e49236afbb74052a6b267494b86b/plume_vulkan.cpp).
Upstream commit `519e325` clamps the window size to the surface bounds and
ignores a defined current extent. It stores the clamped size in the fields that
`needsResize` compares with the window, so a clamped chain is rebuilt on every
frame. Upstream otherwise matches the pinned code: it ignores
the query result, clamps a zero maximum image count to the minimum, requests
transfer and sampled usage, and writes the replacement into the live handle.
The Android loader behaviour above was read from
[AOSP `swapchain.cpp`](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/vulkan/libvulkan/swapchain.cpp)
on 2026-10-27. That loader reports inherit alpha only, extents from 1x1 to
4096x4096 and a full usage set. The usage and transform changes therefore do
not alter Android's request. Its current extent is the native window's default
buffer size, not SDL's window size. On the tested Adreno 650 phone the two
matched (2400x1080) with both drivers. No upstream issue was opened. Device and
desktop results are in
[Android swap chain checks](../docs/ANDROID.md#swap-chain-checks-issue-357).

`tests/test_vulkan_swapchain.py` compiles the patched swap chain against a
recording Vulkan boundary, without a ROM or GPU. It replaces only the
window-system surface call and the window size query. The fake driver checks
every creation against the reported capabilities and follows Android's
`oldSwapchain` rules. It covers query failure at creation, followed by
recovery, and at resize. It also covers:

- a fixed current extent inside wider bounds;
- fixed bounds that differ from the window;
- variable and zero extents;
- bounded, unbounded and minimum image counts;
- colour-only usage and all four alpha modes;
- transform refusal and present-mode changes;
- too few returned images;
- a failed replacement followed by recovery;
- an abandoned Android surface (Home), where acquire and creation both fail
  and RT64 keeps asking for the refresh rate.

Before the patch, 15 of the first 22 scenarios failed. The failed replacement
reproduced the repeated `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR`. The abandoned
surface scenario was added after the first device run of the patch crashed on
Home; it failed until the refresh-rate guard above was added.

Run `python tools/check_patch_series.py` after changing the patch stack. It
checks that each patch is wired into a build path and that the RT64 replay
matrix matches the Windows, Linux, Android and shared CMake application order.
The matrix and checker live in `tools/check_patch_series.py`; the replay
tests consume that matrix rather than maintaining a separate list.

Patch **0035** derives runtime-toggle eligibility from the manifest when each
handle is constructed, including packages present but disabled at startup.
The public scan keeps discovery and config reload under the same mod-context
lock as game load/unload. Scanning remains synchronous; its duration has not
been measured. The separate dependency callback-identity bug is tracked in
[issue 317](https://github.com/alondero/automobililamborghini-recomp/issues/317).

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

Patches **0022/0023/0024/0025/0026/0027/0028/0029/0030/0038/0039** are applied idempotently by CMake on all supported build
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

Compared on 2026-10-03 with RT64 upstream `main` at
`43373749dac9bbc1b653e6a02aed40a9e1783bed`, its
[RasterPS](https://github.com/rt64/rt64/blob/43373749dac9bbc1b653e6a02aed40a9e1783bed/src/shaders/RasterPS.hlsl)
still evaluates native texture, combiner, alpha compare and coverage inline.
Patch 0026 shares that existing arithmetic with explicit derivatives; it adds
no ray-query calls to the native shader and changes no ordinary shader profile.
Windows builds compile the native DXIL and SPIR-V variants. A separate pinned
patch replay matches all 32 modified/new source files byte-for-byte. The hardware
probe covers cached textures, scaled replacements, IA16 TMEM, dynamic tiles,
native samplers and derivative-selected LOD. This is fixture evidence, not
complete game ray-hit parity. No upstream issue was opened.

Patch 0027 reconstructs UV, shade, displayed position and coarse derivatives
from generated indexed attributes and the matching viewport/scale/offset.
The shadow AS metadata distinguishes opaque solids from caller-authenticated
alpha ranges. Candidate evaluation uses the shared native material code; absent
evaluators and unsupported input invalidate coverage. RT64 upstream at the
same verified revision has no `NativeRayHit.hlsli` or sunlight scene/kernel.
This remains local generic groundwork, with no game admission or RAM reader.
The Windows builder and CMake recognize 0027 before replaying older patches
whose contexts it changes. No upstream issue was opened.

Compared on 2026-10-03 with Plume upstream `main` at
`d72379344dacd3dbf9f810f92ddc87e6de1845b1`, its
[D3D12 factories](https://github.com/renderbag/plume/blob/d72379344dacd3dbf9f810f92ddc87e6de1845b1/plume_d3d12.cpp)
still return wrappers after failed graphics/compute PSO creation. Patch 0028
checks the native handle and returns null. The GPU probe intentionally supplies
a graphics root signature without vertex-input support and a compute root
signature missing required resources. Both failures return null; subsequent
valid raster and ray submissions pass. Only the two expected validation IDs are
filtered during injection; the outer probe still rejects other errors. This
does not validate game fallback on resource failure. No upstream issue was opened.

Patch 0029 captures actual generated UVs, uploaded material bindings, selected
native vertex bytecode and vertex outputs for the opt-in D3D12 alpha comparison.
The six-circuit/mode probe still finds eight interior disagreements and grants
no alpha or overlay-replacement admission. This local diagnostic is absent from
the upstream revision verified above. Its geometry shader only records outputs
in private targets; the coverage reference uses the original native pipeline.
The normal raster path releases temporary vertex shaders as before. The clean
pinned replay matches all 39 modified/new files from twelve RT64 patches.
See [measured game evidence](../docs/rt-material-evidence.md#native-game-alpha-diagnostic).
No upstream issue was opened.

Compared on 2026-10-03 with RT64 upstream `main` at
`43373749dac9bbc1b653e6a02aed40a9e1783bed` (re-verified as the branch head):
upstream has no sun-shadow scene, receiver pass, shadow provider hook or
ray-query receiver shader. Patch 0030 adds a generic provider seam
(`SunShadowProvider`) and pass; the port owns every Lamborghini policy, RAM
address and material rule. RT64 reads no guest RAM for it. The pass
compiles on every platform but only activates on D3D12 with DXR 1.1 and
Shader Model 6.5; other backends report the unsupported reason and stay
native. Evidence and limits are in [rt-shadows.md](../docs/rt-shadows.md).
No upstream issue was opened.

For an initialized Windows checkout with local dependency edits, use
`./build.ps1 -PreserveSubmodules`. It verifies recursive dependency pins and
skips checkout/clean operations. Like CMake, it recognizes 0039, 0038, 0030, 0029, 0027 or 0025 before
revisiting earlier RT64 patches whose contexts have changed. The continuation
replayed the nine Windows RT64 patches in a separate clean pinned worktree:
all 31 modified/new files matched the active patched dependency byte-for-byte.
No patch content or dependency pin changed in that continuation.

| Build path | Applies |
| --- | --- |
| Linux script | 0001, 0007, 0012, 0006, 0009, 0010, 0011, then 0016 through 0039 in CMake. |
| Windows script | The Linux set plus 0005 and 0004, then 0016 through 0039 in CMake. |
| Android script | 0001, 0007, 0012, 0006, 0009, 0010, 0011, 0013, and 0014, then 0016 through 0039 in CMake. The fetched SDL gets 0015, then 0048. |

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
policy (0010), a texture table for drivers without descriptor indexing (0033),
and platform compatibility (0004, 0005, 0013-0015, 0048). `src/stub_renderer.cpp` is a separate port-owned diagnostic
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

### Optional frontend debugger (0037)

Compared on 2026-10-17 with pinned RecompFrontend
`b1a1477c6556aeb7ed45defbfb5924f721efebc1` and upstream HEAD
`2ddbfc8adcfe9ba26520441f14488861fdcefef7`: both initialize the debugger
unconditionally in `recompui/src/base/ui_state.cpp`; only the F8 toggle checks
`general::get_debug_mode_enabled()`. This port finalizes configuration before
installing renderer hooks, so the existing hidden, default-false setting is
available when the UI starts. Patch 0037 snapshots that choice and guards
shutdown and toggles on successful initialization. No logging filter is added.
The upstream comparison is local evidence; no upstream issue was opened.

Limitation: the headless tests drive the wrapper against existing local RmlUi
libraries, so they cover the debugger lifecycle and beacon, not the port. The
hand-edited `general.json` path is covered by the settings host test, not by a
played session. No full game build, top-level configure, or rendered
launcher/settings navigation was run in either mode, and other platforms remain
unverified. F8 is shared with the developer save-state restore in
`src/main.cpp`, so a press reaches both handlers.
