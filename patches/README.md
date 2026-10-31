# Dependency patch inventory

These patches are part of the build input. They are applied to pinned public
submodules by the build scripts or by CMake. They are not a private fork.

The patch files are the source of truth for exact hunks.
[series.txt](series.txt) is the only list of which patch applies to which
dependency, in which order, on which platforms. Every build path applies it
through `tools/apply_patches.py`: CMake configure, `build.sh`, `build.ps1` and
`scripts/build_android.py`. The descriptions below explain why each patch
exists and which changes need comparison with upstream.

The tool builds each cumulative state of a series in a temporary Git index and
finds the longest one a checkout already contains, then applies the rest as
one diff. A later patch may therefore rewrite lines an earlier patch added,
and an existing checkout is completed without resetting unrelated edits. A
checkout that matches no point of its series, because of a conflicting local
edit, another platform's series or an edited patch, stops the build; the
build scripts reset dependencies first unless `-PreserveSubmodules` is given.
Run `python tools/apply_patches.py` after changing the patch stack: it checks
that `series.txt` lists every patch file once. `tests/test_patch_composability.py`
replays every platform's series against the pinned submodules.

A patch file is itself a diff, so a blank context line inside a hunk is a line
holding one space â€” exactly what `git diff` emits. Never strip that space:
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
| 0016 | N64ModernRuntime | Host-owned configuration storage for the frontend integration. Project integration. |
| 0017 | RecompFrontend | Host event handling and Lamborghini frontend integration, including controller menu actions resolved from the pressing device instead of only player one, and restoring mod controls after a rejected load ([upstream proposal](https://github.com/N64Recomp/RecompFrontend/issues/43)). Also stubs the NFD-backed file dialogs on Android, where the build neither fetches nor links nativefiledialog-extended (see 0013); the Activity imports the ROM and GPU driver instead. The player-one-only lookup still exists on upstream `main`, so this hunk is a local patch with a recorded upstream comparison. |
| 0018 | N64ModernRuntime | Game presentation behavior used by the launcher and normal game path. Project integration. |
| 0019 | RecompFrontend | The shared prompt dismisses on the mapped Back action, so a controller's cancel button backs out of the quit confirmation. Applied after 0017. Compared with upstream `main`, which builds the prompt with no element that listens for menu actions, so no controller action can dismiss it. Generic frontend behavior, kept local. |
| 0020 | RecompFrontend | Enum selections wrap within the options column so the window-size presets remain readable. Applied after 0019. Compared on 2026-10-01 with [upstream enum rendering](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/config/ui_config_option.cpp), which creates the radio group without wrapping. Keeps the existing values, callbacks, and navigation. Checked on native Windows at 1600x900: all twelve choices fit two rows, and selecting 1920x1200 then Apply saves that size. The frontend settings test and idempotent CMake application pass; other platforms remain unverified. |
| 0021 | RT64 | Shadow-only AS resources, shared parameter ABI and angular-query/fog kernel. Developer groundwork only; game receivers remain gated. Upstream comparison and hardware probe evidence are below. |
| 0022 | RT64 | Opt-in queue-owned material/raster evidence and fenced GPU/present observation. Thread-local raster scope excludes HLE's framebuffer renderer. Port-owned capture policy; no sunlight Workload fields or production receiver integration. |
| 0023 | Plume inside RT64 | D3D12 texture-to-buffer readback selects source sample positions when destination is a placed-footprint buffer, avoiding a null texture dereference. ROM-free 7x3 padded-row GPU regression. |
| 0024 | RT64 | Optional D3D12 owner-output pipelines mirror native raster clipping, material/discard and depth behavior, including the generic ubershader fallback, into an R32G32_UINT diagnostic target with cloned depth. On RTX 3080 captures, enabling the diagnostic leaves the actual swapchain byte-identical. It is evidence instrumentation only; it does not implement shadow replacement. The precompiled-owner path is `_WIN32`-guarded because DXIL blobs are only generated on Windows (Linux failed with undeclared `RasterPSOwnerBlobDXIL`). |
| 0025 | RT64 | Adds immutable sunlight and separate caster/receiver admission metadata to the matching Workload through the developer evidence observer. HLE copies projection address identity alongside transform groups. Typed screen/backdrop exclusions retain zero face-index extent for non-indexed draws. The latest 48 model-0/one-player task captures have no unclassified draws, but all fail closed on native texture-alpha coverage and blended car geometry. No production receiver consumes these values and no native overlay is suppressed. |
| 0026 | RT64 | Extracts native texture/combiner/alpha/coverage evaluation into a shared shader helper, with explicit UV derivatives and unchanged native raster arithmetic. Adds the helper to raster shader build dependencies. All 48 fresh game images match the preceding native build byte-for-byte; 30 synthetic hardware material cases pass. Ray-hit resource/attribute integration is still required and alpha admission remains closed. |
| 0027 | RT64 | Adds generated-attribute reconstruction and explicit native-alpha candidate coverage to the shadow helper. Unsupported input invalidates coverage instead of becoming a solid caster. Thirty synthetic candidate cases agree with native RasterVS/RasterPS ownership; three uncertain shade-alpha/subpixel-LOD paths remain unsupported. Game alpha ranges are still rejected. |
| 0028 | Plume inside RT64 | D3D12 graphics/compute pipeline factories return null when native PSO creation fails. A wrapper with no native handle cannot count as ready. Real failure injection returns null for both factories, then native raster/query submissions still pass. |
| 0029 | RT64 | Opt-in D3D12 game alpha evidence borrows matching native descriptors and selected vertex shaders after the Workload fence. Isolated native owner passes and actual pre-clipping vertex outputs expose coverage disagreements; all game alpha admission remains closed. Raw draws retain zero face start and native rendering remains available. |
| 0030 | RT64 | Optional production sunlight shadows. A port-registered provider is told each Workload id as HLE assigns it, before any queue thread can see it, and attaches admitted metadata to that Workload; RT64 builds a per-Workload shadow acceleration structure from presented world positions, re-rasterizes admitted prelit receivers with an SM6.5 ray-query pass and composites `C = min(C, T*C + (1-T)*fog)` once to RGB only, so a shadow never brightens a pixel and native coverage alpha is kept. The exact native overlays of one view are replaced all-or-nothing; every pending, failed or unsupported gate keeps the native command stream. D3D12 with DXR 1.1 only; single-sample and MSAA targets. Adds GPU timestamps for the AS build and receiver pass. Original mode is byte-identical to the previous build. The DXC options hunk keeps PS/VS-only context so it applies with and without Android-only patch 0013's blank line (their overlap broke the Android release configure). |
| 0031 | RecompFrontend | Normalize document sources to RmlUi URL syntax: forward slashes and a Windows drive separator of \|. Prevents malformed-protocol errors and the red debugger beacon while preserving relative asset paths. Applied after 0020. Compared 2026-10-08 with pinned source and [upstream main](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/core/ui_context.cpp), both of which use native path text. Supported Windows build, native captures and an RmlUi-backed URL regression pass; other platforms remain unverified. |
| 0032 | RecompFrontend | The settings header keeps Quit and Close inside the modal. The tab row of a tabbed modal takes only the width the header's right side leaves and wraps its labels onto further rows, and the right side never shrinks; before this, tab labels wider than the modal laid Quit and Close out past its edge, where they could not be clicked. Focus order and tab switching follow the tab list, not its rows. Applied after 0031. Compared on 2026-10-09 with [upstream `main`](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/elements/ui_modal.cpp), which is the pinned commit `b1a1477`: `TabbedModal::initialize_tab` puts the tab set in the header's left side with no way to shrink or wrap. Generic frontend behavior, kept local. Checked on native Windows at 1066x580 and 2560x1440 with eight tabs and, in a throwaway build, twelve: the labels wrap to a second row and Quit and Close stay inside the modal. Those captures were made with this hunk inside a larger local frontend patch; the hunk is byte-identical. Other platforms remain unverified. |
| 0033 | RT64 | Drivers without descriptor indexing get a fixed 8192-slot texture table instead of the variable-count, partially bound one, so `vkAllocateDescriptorSets` no longer fails and crashes on older Android system drivers. Pipeline layouts and the bound set take the same choice from the device capabilities. Every fixed slot holds a cleared 1x1 placeholder until a texture owns it; released cache slots and last frame's dynamic views return to it. Drivers with descriptor indexing (D3D12, desktop Vulkan, Turnip) keep the upstream path with no extra writes. Applied after 0030. Compared 2026-10-10 with RT64 upstream `main` at `8bd2cf1530c17ce8c3489eb47671ee0da2f8319d`, which still requests the variable-size range unconditionally. The shaders declare non-uniform indexing, which texture LOD needs ([upstream #164](https://github.com/rt64/rt64/issues/164)); 0044 removes it on these drivers. See the [Android validation result](../docs/ANDROID.md#gpu-drivers-and-device-validation). ROM-free layout test plus an Android system-driver run under the Khronos validation layer. No upstream issue was opened. |
| 0034 | RecompFrontend | Enables Refresh during play and lets its callback scan for newly added packages. Clears the old selection before rebuilding the list, avoiding invalid indices when it shrinks. Requires runtime patch 0035 to preserve active handles. Compared 2026-10-17 with the pinned source and [upstream main](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/composites/ui_mod_menu.cpp), which disable Refresh and skip scans during play. Extracted production refresh/update methods pass a Windows host test with UI/runtime fakes; native window interaction and other platforms remain unverified. |
| 0035 | N64ModernRuntime | During play, scans append new filenames while retaining active handles, indices, selection and order. New packages start disabled. Packages needing startup validation keep their restart requirement. Stable handle storage preserves config pointers held by the save thread and UI. Existing filenames are not reopened; replacements and removals take effect on restart. Compared 2026-10-17 with the pinned source and [upstream main](https://github.com/N64Recomp/N64ModernRuntime/blob/main/librecomp/src/mods.cpp), whose scan closes every opened handle and reloads config. Applied before 0034. The Windows real-loader regression covers live RTZ discovery/toggling, state/pointer preservation, repeated scans and startup-only packages without a ROM or renderer. |
| 0036 | N64ModernRuntime | Defines ModHandle and its code-handle interface before ModContext instantiates deque<ModHandle>. Android NDK 28's libc++ requires a complete element type here; GCC accepted the earlier declaration order. The definitions and stable-storage behavior are unchanged. Applied after 0035. Compared with the pinned header, which places ModHandle after ModContext and originally used vector. An Android ARM64 header compile reproduces the incomplete-type error before this patch and passes after it. |
| 0037 | RecompFrontend | Gates RmlUi debugger initialization, F8 visibility, and shutdown on the existing default-off `general.json` `debug_mode` option. Diagnostics still reach the system logger. Applied on every platform. Headless tests inject a real diagnostic in both modes and inspect the beacon. The tests exercise the host path only: no full game build, top-level configure, or rendered launcher/settings navigation was run, and other platforms remain unverified. |
| 0038 | RT64 | Makes the sunlight shadow scene robust to view and light. Static scenery, moving casters and remembered static scenery build separate bottom-level structures under instance masks `0x1`, `0x2` and `0x4`, each reading its own metadata. The receiver pass fades the sun shadow where scenery hides most of the sky, adds a short-range contact shadow from moving casters, and takes an occluder mask so a light the art already bakes (tunnel lamps) is blocked by moving casters only. `SunShadowStaticCache` remembers triangles whose exact world positions repeat in nearby tasks and traces those the view no longer presents, so shadows no longer change with the camera; drawn copies are left out so no triangle is traced twice. Debug views show transmission, open sky, contact, and what the remembered scenery changes. Applied after 0033. Generic renderer behaviour, kept local. Compared 2026-10-22 with RT64 upstream `main` at `de343a53c703f2fc7fdd64ac6ff9e5a3f41e66f3`, whose tree has no shadow source (only `src/shared/rt64_raytracing_params.h`), so there is nothing to upstream against. The tuning defaults (`radius=400`, `keepTasks=300`, `maxTriangles=131072`) are in renderer units measured on this game's tracks. Host tests cover the cache, the D3D12 probe covers masks, the static layer and its rebuild on an RTX 3080, and recorded laps on all six circuits stay on the traced path for every race task. |
| 0039 | RT64 | Reduces shadow traversal work without changing quality settings. Builds the small shadow acceleration structures for fast tracing, and evaluates sky fading after the first blocked sunlight sample so enclosed receivers skip the remaining soft-shadow rays. The original visibility-only kernel retains its exact-face and alpha-coverage contract. Applied after 0038. Compared on 2026-10-24 with upstream `main` at `d9817c68ffd7328cd0af383af124971489366e18`, whose tree contains no SunShadow sources. GPU probes compare sky-faded occlusion against the ungated calculation, including a closed tunnel at every ray quality. Measurements and limits are in [RT shadows](../docs/rt-shadows.md). |
| 0040 | RecompFrontend | Extracts the existing config-open event test into a read-only query shared by frontend dispatch and the port's pre-draw settings refresh. Ordinary gameplay presses avoid schema work; Escape and both remapped menu buttons retain opening-frame freshness. Applied after 0037. Compared 2026-10-25 with the pinned source and [upstream main](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/base/ui_state.cpp), which keep the test inside event dispatch and expose no query in recompui.h. No upstream issue is opened. The native frontend settings fixture covers keyboard events, both controller bindings, remapping, single-player and multiplayer profiles, and unknown devices. |
| 0041 | Plume inside RT64 | Retries variable descriptor-pool exhaustion using the declared layout capacity; stops on persistent allocation failure. See the comparison and tests below. |
| 0042 | RT64 | On Android, caps automatic resolution at 2x for the exact Adreno 615 system-driver combination `0x5143` / `0x801f6000` / `Adreno (TM) 615`. Manual choices keep their existing scaling; 0059 bounds every Android device's automatic scale by a pixel rate. Applied after 0039. The SM-T720 loses the device at its automatic 6x scale but reaches an attract race at 2x. This is a measured workaround; the driver's internal GPU fault remains unverified. Compared 2026-10-26 with pinned RT64 and [upstream main at d9817c6](https://github.com/rt64/rt64/blob/d9817c68ffd7328cd0af383af124971489366e18/src/hle/rt64_workload_queue.cpp), which derive automatic scale from the window without a driver limit. The host fixture executes the production scale-selection switch for Android and desktop; see the [device investigation](../docs/ANDROID.md#adreno-615-startup-device-loss). No upstream issue was opened. |
| 0043 | Plume inside RT64 | Clips Vulkan clear rectangles to the render area and skips a clear with no visible rectangle. RT64 converts inclusive N64 fill rectangles into a clear one pixel wider than the target (321x241 on 320x241, 642x482 on 640x482), which `vkCmdClearAttachments` forbids (`VUID-vkCmdClearAttachments-pRects-00016`). Pixels outside the target do not exist, so the visible result is unchanged. Applied after 0041. Compared 2026-10-26 with pinned Plume `51b1ad4` and [upstream main at 0ce184b](https://github.com/renderbag/plume/blob/0ce184b19270e49236afbb74052a6b267494b86b/plume_vulkan.cpp), which pass rectangles through unchanged. A host fixture runs the production clear path against a recording Vulkan boundary. This does not fix the Adreno 615 device loss; see the [Android investigation](../docs/ANDROID.md#adreno-615-startup-device-loss). No upstream issue was opened. |
| 0044 | Plume inside RT64 | On devices without descriptor indexing, removes non-uniform resource indexing declarations from SPIR-V before `vkCreateShaderModule`: the `ShaderNonUniform` and `*ArrayNonUniformIndexing` capabilities, `NonUniform` decorations, and `SPV_EXT_descriptor_indexing` when no other capability needs it. Those drivers cannot accept them (`VUID-VkShaderModuleCreateInfo-pCode-08740`/`-08742`). Without the declarations, a texture index must be the same across each draw. RT64 varies it within a draw only for RDP texture LOD, which this game never enables ([no_lod audit, Axis D](../docs/no_lod_audit.md)). Devices with descriptor indexing receive the original bytes. Malformed or foreign-endian code passes through. Applied after 0043; it takes the same capability switch as 0033. Compared 2026-10-26 with pinned Plume `51b1ad4` and [upstream main at 0ce184b](https://github.com/renderbag/plume/blob/0ce184b19270e49236afbb74052a6b267494b86b/plume_vulkan.cpp), which pass SPIR-V through unchanged. A host fixture runs the production constructor on synthetic modules and on RT64's RasterPS compiled with the bundled DXC. No upstream issue was opened. |
| 0045 | RecompFrontend | On Android the UI pipeline targets R8G8B8A8_UNORM, the format of RT64's Android swap chain from 0013. Before this, the settings overlay drew with a B8G8R8A8_UNORM pipeline into an R8G8B8A8_UNORM swap chain (`VUID-vkCmdDrawIndexed-renderPass-02684`). The port's earlier in-tree UI made the same Android choice; moving to RecompFrontend lost it. Desktop keeps B8G8R8A8_UNORM. Applied after 0040. Compared 2026-10-26 with the pinned source and [upstream main at 2ddbfc8](https://github.com/N64Recomp/RecompFrontend/blob/2ddbfc8adcfe9ba26520441f14488861fdcefef7/recompui/src/renderer/ui_renderer.cpp), which hard-code B8G8R8A8_UNORM. A host test compiles both production format choices for Android and desktop and requires them to agree. No upstream issue was opened. |
| 0046 | RT64 | The game's render thread sleeps on a condition variable while the present or workload queue slot it needs is still behind the consumer's barrier. Before this it spun on `cursorMutex`. Issue 329's Android 15 / Adreno 730 traces show RT64 Present blocked in Qualcomm gralloc while the render thread spent about 17.5 s of CPU in `advanceToNextPresent`. Ordering is unchanged: the producer still waits for the same barrier, and the barrier's advance wakes it. This removes the busy core; it does not release the gralloc stall. Applied after 0042. Compared 2026-10-26 with pinned RT64 and [upstream main at d9817c6](https://github.com/rt64/rt64/blob/d9817c68ffd7328cd0af383af124971489366e18/src/hle/rt64_present_queue.cpp), which both spin. A host fixture compiles the patched queue methods, holds each barrier for 400 ms, and requires the producer to stay blocked, wake on the barrier advance, and use under 100 ms of CPU. Clearing the queue's running flag also wakes the producer, so a stalled consumer cannot strand it. The destructors' wake-up line is not covered by the fixture; it is checked by reading. No upstream issue was opened. |
| 0047 | Plume inside RT64 | Creates and replaces Vulkan swap chains within the surface's reported capabilities. See [the swap chain section](#swap-chain-surface-capabilities) for the contract, comparison and tests. Applied after 0044. |
| 0048 | SDL dependency | Android SDL leaves a non-OpenGL window to its own graphics API. Pinned SDL 2 tried to create an EGL surface on every surface change, whatever the window type, and on every surface loss made Android's UI thread poll, up to 49 times 10 ms, for the event loop's EGL context backup. On every pause and resume the event loop also backed up and restored a GL context; for a Vulkan window the restore failed and pushed a spurious `SDL_RENDER_DEVICE_RESET`. The port's window is Vulkan. All of this now requires `SDL_WINDOW_OPENGL`, as window creation already does. The UI-thread wait is also skipped under `SDL_HINT_VIDEO_EXTERNAL_CONTEXT`, because the event loop never backs up an external context, so the wait could only time out. OpenGL windows keep everything, and an external-context OpenGL window still gets the EGL surface that window creation gives it. Debug-priority video log lines record each surface decision; see [surface callback logs](../docs/ANDROID.md#sdl-surface-callback-logs). Applied after 0015. Compared 2026-10-27 with pinned SDL `5d24957`, [the SDL2 branch at 8acf7cf](https://github.com/libsdl-org/SDL/blob/8acf7cfc0f88717e5c036f60f0314bfcafe39189/src/core/android/SDL_android.c#L1055), which still does all of it for every window, and SDL3 main at 4c18c09, which gates the [surface callbacks](https://github.com/libsdl-org/SDL/blob/4c18c09ea58408e476b266b34427b868a08e9fbb/src/core/android/SDL_android.c#L1283) and the [context backup and restore](https://github.com/libsdl-org/SDL/blob/4c18c09ea58408e476b266b34427b868a08e9fbb/src/video/android/SDL_androidevents.c#L36) on `SDL_WINDOW_OPENGL` but does not check the external-context hint in the wait. [DKR-R's patch](https://github.com/ThatGuyMcd/DKR-R/blob/d457741b57afff8d2ec96b2307f11c1340a4162f/patches/android/sdl2-vulkan-surface-lifecycle.patch) gates only the two callbacks and also skips surface recreation for an external context; this patch keeps recreation matched to window creation. `tests/test_android_sdl_surface.py` compiles the patched callbacks and context steps against a recording SDL/EGL boundary for Vulkan, OpenGL, timeout and external-context windows, and replays the SDL series on clean, pre-0048 and conflicting checkouts. Whether the old work contributed to issue 329's gralloc stall is unverified. No upstream issue was opened. |
| 0049 | Plume inside RT64 | On Android, a swap chain that requests `R8G8B8A8_UNORM` uses it with `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR` without calling `vkGetPhysicalDeviceSurfaceFormatsKHR`. Android's loader always lists that pair first without asking gralloc, so the chosen format is unchanged. It answers the rest of the query by asking gralloc about five more formats. In issue 329's Android 15 / Adreno 730 exports, gralloc's lock was held by the graphics thread, which runs this query, and RT64 Present blocked on it creating the first chain. Other formats and other platforms still query. Applied after 0047, whose neighbouring lines it changes. Compared 2026-10-28 with pinned Plume `51b1ad4` and [upstream main at 0ce184b](https://github.com/renderbag/plume/blob/0ce184b19270e49236afbb74052a6b267494b86b/plume_vulkan.cpp), which query in the constructor on every platform. The swap chain fixture compiles the production code a second time with its Android branch and models the held lock: construction on one thread, creation and presentation on another. It fails before the patch and passes after. That this query is what leaves gralloc's lock held is an inference; see the [Android investigation](../docs/ANDROID.md#gralloc-lock-holder). No upstream issue was opened. |
| 0050 | Plume inside RT64 | Invalidates mapped reads and flushes mapped writes through VMA, with exclusive byte ranges, heap defaults, release-build validation and balanced failure cleanup. Applied on every platform. See [mapped buffer memory](#mapped-buffer-memory) for caller audit, upstream comparison and measured limits. |
| 0051 | Plume inside RT64 | Documents Vulkan's read-range defaults and dirty-atom rule beside the mapping declaration. Applied after 0050. No runtime change; see [mapped buffer memory](#mapped-buffer-memory). |
| 0052 | Plume inside RT64 | Returns null after failed mapping or invalidation, preserving recoverable callers and nested references. Applies after stable patches 0050/0051. Flush failures remain fatal; see [mapped buffer memory](#mapped-buffer-memory). |
| 0053 | Plume inside RT64 | Rejects invalidating read maps while another mapping is active, before acquiring a VMA reference. Omitted reads invalidate all non-upload heaps, including host-visible DEFAULT. Updates the contract comment without rewriting 0050/0051/0052. |
| 0054 | SDL dependency | Retained Android native-window snapshots and generation publication. Applied after 0048. See ownership below. |
| 0055 | Plume inside RT64 | Recover Android Vulkan surfaces on RT64 Present, bound acquisition and drain presentation work. Applied after 0053. See ownership below. |
| 0056 | RT64 | Retire presentation framebuffers and synchronization before resizing; drain on shutdown without a surface. Applied after 0046. |
| 0057 | N64ModernRuntime | Host application suspension freezes guest time, timer deadlines and VI production. Android opts in through an SDL event watch. |
| 0058 | RT64 | Prints one `[render-cost]` line to standard output every 5 seconds: surface and VI sizes and rates, refresh target, internal target and scale, MSAA, game and presented frame rates, rendered and requested display frames, and per-frame wall and GPU times, plus per-game-frame workload, setup and matching times. On by default on Android, where standard output is `native.log` and the diagnostics ZIP carries it; desktop prints it when `RT64_RENDER_COST_REPORT` is set to a value other than `0`. A shared atomic counter counts successful presents. It adds counters and one formatted line per window, with no GPU work or extra synchronisation. Applied after 0056. Compared 2026-10-29 with pinned RT64 and [upstream main at d9817c6](https://github.com/rt64/rt64/blob/d9817c68ffd7328cd0af383af124971489366e18/src/hle/rt64_workload_queue.cpp), which feed the same timers only to the debugger overlay. There is no host test; the fields were checked against device runs. See [render cost](../docs/ANDROID.md#render-cost-and-automatic-resolution). No upstream issue was opened. |
| 0059 | RT64 | On Android, bounds the automatic (window) scale by a presented pixel rate of 80 million pixels per second for every GPU and driver. The scale is the largest integer whose internal target, times the presentation rate, fits: the display refresh target when it is above the game's rate, otherwise the game's rate, and 30 Hz until that is known. Never below 1x and never above the window scale. 0042's Adreno 615 limit still applies. Manual and native scales are unchanged, and desktop keeps the window scale. The refresh-target selection moves ahead of the resolution switch so the budget can read it; its result is unchanged. Applied after 0058; it changes 0042's and 0058's context. Compared 2026-10-29 with pinned RT64 and [upstream main at d9817c6](https://github.com/rt64/rt64/blob/d9817c68ffd7328cd0af383af124971489366e18/src/hle/rt64_workload_queue.cpp), which use the window scale without a bound. The resolution fixture executes the production switch for Android and desktop. See [render cost](../docs/ANDROID.md#render-cost-and-automatic-resolution). No upstream issue was opened. |
| 0060 | RT64 | Frame matching (`GameFrame::matchScene`) reuses sorted vectors instead of rebuilding `std::multimap` and `std::set` containers every frame. Call hashes sort by hash, then projection and call index, the multimap's insertion order for equal hashes. Check pairs are sorted and made unique, the set's order. Iteration and matching results are unchanged; after the first frames no allocation is needed. On an Adreno 650 phone, Android's allocator was about half of the RT64 workload thread, and matching fell from about 12.8 to 3.5 ms per game frame. Applied after 0059. Compared 2026-10-29 with pinned RT64 and [upstream main at d9817c6](https://github.com/rt64/rt64/blob/d9817c68ffd7328cd0af383af124971489366e18/src/hle/rt64_game_frame.cpp), whose matching code is the same. Patch 0061 replaces the sorted call-hash vectors; the sorted, unique check pairs remain. No upstream issue was opened. |
| 0061 | RT64 | Frame matching pairs draw calls of equal hash through joined keys instead of visiting every pair of calls. Each call adds keys holding only what decides its check pairs: for transforms the hash, world-matrix count, workload and first world matrix; for tiles the hash, tile count and slot, whether the call matches tiles, TMEM hash and tile index; for look-ats the hash, workload and first face index. Equal keys are merged, then the current and previous keys of each hash are joined. Repeated geometry drawn under one matrix therefore adds its transform pairs once. The sorted, unique check pairs are the same as before, so every transform, tile and look-at match is unchanged; look-at indices are still read only for calls with a partner, because rectangle calls have no face indices. On the desktop, matching fell from about 1.6-2.1 to 0.44-0.64 ms per game frame on all six tracks, and from 7.5 to 1.4 ms in two-player split screen. Applied after 0060. Compared 2026-10-29 with pinned RT64 and [upstream main at d9817c6](https://github.com/rt64/rt64/blob/d9817c68ffd7328cd0af383af124971489366e18/src/hle/rt64_game_frame.cpp), whose `rt64_game_frame.cpp` is identical to the pinned file. `tests/test_rt64_frame_matching.py` compiles the new `rt64_game_call_keys.h` against the call-by-call `std::multimap`/`std::set` loop on random frames, and requires equal check pairs, look-at reads only for calls with a partner, and no allocation after warm-up. No upstream issue was opened. |
| 0062 | Plume inside RT64 | Vulkan `copyTextureRegion` handles a placed-footprint destination with a subresource source and issues `vkCmdCopyImageToBuffer`. Pinned Plume falls through to the image-to-image branch for this direction and dereferences a null destination texture, so the production readback path is unusable on Vulkan. The branch is a backport of [Plume `0ce184b`](https://github.com/renderbag/plume/commit/0ce184b19270e49236afbb74052a6b267494b86b) (`Support texture readback through copyTextureRegion`, #114); the pinned tree has only the buffer-to-image direction. It honors destination X/Y/Z within the footprint, row pitch and height, the source subresource mip level and array layer, the image layout, and a source box's offset and extent. Applied after 0055. `tests/test_vulkan_texture_readback.py` compiles the production method against a recording Vulkan boundary, and the ROM-free GPU probe uses the production copy with a padded, offset footprint before reading pixels after the completion fence. On Windows with an RTX 3080 the probe passed; the Khronos validation layer was unavailable and Android was not rerun. No upstream issue was opened. |

## Mapped buffer memory

Patch 0050 changes only Vulkan buffer mapping. Compared on 2026-10-28 with
pinned Plume `51b1ad443b9f202c5cfc930ae25345d3f2ba7716` and verified upstream
`main` at [0ce184b19270e49236afbb74052a6b267494b86b](https://github.com/renderbag/plume/blob/0ce184b19270e49236afbb74052a6b267494b86b/plume_vulkan.cpp):
both map/unmap through VMA without cache maintenance. Automatic allocation
and host-access hints do not require coherent memory. No upstream issue was
opened. Queue-family ownership remains separate (issue 361).

Rechecked patches 0052 and 0053 on 2026-10-28 against upstream `main`, still
at [0ce184b19270e49236afbb74052a6b267494b86b](https://github.com/renderbag/plume/blob/0ce184b19270e49236afbb74052a6b267494b86b/plume_vulkan.cpp).
Upstream `VulkanBuffer::map` logs and returns `nullptr` when `vmaMapMemory`
fails; 0052 restores that upstream failure contract. Upstream has no
invalidation call, so 0052's invalidation-failure cleanup and null return are
local policy, releasing only the newly acquired VMA reference. Upstream
ignores read/written ranges and only calls `vmaUnmapMemory` on unmap: it has
neither cache maintenance nor a nested-map guard. Thus 0053's rejection of
nested invalidating reads and its whole-buffer omitted-read policy for
non-upload heaps remain local behavior, supported by the recording tests.
No upstream issue was opened.

Patch 0050 keeps its first published contents. 0051 (the mapping declaration
comment, with no behavior change), 0052 (the map/invalidation failure policy
below) and 0053 (the nested-read guard and conservative DEFAULT-heap reads)
were published as separate suffixes so that checkouts already holding 0050
could upgrade in place under the earlier per-patch recognition. The applier
now completes a checkout stopped after any of them, keeping unrelated
dependency edits and the Git index; conflicting edits still stop
configuration. The upgrade regression starts from checkouts stopped after 0050
and after 0052, with staged, unstaged and untracked developer files, applies
the series twice, and compares both Plume source files with fixture replay.

The series applies 0050 after 0049. There is no requirement for 0050 to precede 0047/0049: its mapping hunks and
their swap-chain hunks do not overlap.

`RenderRange` describes `[begin, end)` bytes relative to the buffer. Mapping
returns the allocation base, including for partial ranges; callers add their
own offsets. VMA maps the allocation, so the range declares CPU access rather
than limiting the mapping itself.

The omitted read range deliberately differs between backends. D3D12 passes
`nullptr` to `ID3D12Resource::Map`, which permits CPU reads of the whole
resource. Vulkan treats omitted UPLOAD/GPU_UPLOAD read ranges as write-only
and does not invalidate them. Portable callers that read these heaps must
provide an explicit read range; relying on D3D12's null-range behavior can
read stale data on noncoherent Vulkan memory.

| Range | Map | Unmap |
| --- | --- | --- |
| Omitted | No reads on UPLOAD/GPU_UPLOAD; invalidate the whole buffer on other host-visible heaps, including DEFAULT and READBACK. | Flush the whole buffer except READBACK, which defaults to no writes. |
| Explicit nonempty | Invalidate those bytes on any heap. | Flush those bytes on any heap, including READBACK. |
| Explicit empty, including `[size, size)` | Map without invalidation. | Unmap without flushing. |
| Reversed or beyond the buffer size | Stop before mapping. | Release the mapping, then stop. |

Only subresource zero is valid. Failed maps return `nullptr`, matching D3D12
and preserving existing callers that handle an unavailable upload. Failed
invalidation releases the new VMA reference before returning `nullptr`.
Neither failure exposes a data pointer. A failed write-only nested map leaves
earlier successful references intact.
Flush failure releases the mapping and stops before a GPU consumer is submitted.
Unmatched unmaps stop without calling VMA. Nested maps retain VMA's reference
counting for write-only access. A map that would invalidate any nonempty read
range while another mapping is active aborts before `vmaMapMemory`, on coherent
and noncoherent memory alike. These checks run in release builds. VMA handles coherent-memory
no-ops and `nonCoherentAtomSize` alignment; ranges use the buffer's size rather
than allocation padding. A host-visible DEFAULT allocation with an omitted
range is conservatively treated as readable. DEFAULT does not guarantee
host-visible memory: this policy applies only when its allocation can be
mapped; a failed VMA map still returns null. No current caller maps DEFAULT;
the whole-buffer policy is tested rather than relying on that caller inventory.

Atom alignment also affects ownership of dirty bytes. VMA expands a cache
range to whole `nonCoherentAtomSize` atoms. Invalidating a read range can
discard unflushed CPU writes elsewhere in the same atom, even outside the
requested byte range. Finish every active mapping with the appropriate written
range before starting a read map. The nested-read guard prevents this pattern
from silently discarding host writes while another mapping remains active.
After the last unmap, the API cannot detect dirty bytes deliberately omitted
from written ranges: an empty written range does not flush those bytes, and
a later invalidation may discard them. That caller duty remains a caveat,
not an automatic guarantee. The audited callers do not mix unflushed writes
and invalidation this way.

Contract violations and flush failures use `abort()` after flushing the
operation and any VMA error code to stderr. In particular, `VK_ERROR_DEVICE_LOST`
from a flush terminates the game in release builds. This is an explicit policy:
`unmap()` has no error return, and continuing could submit stale data. The port
has no in-process device-loss recovery; the startup GPU advisory is not recovery.
Map and invalidate failures instead log and return null, so callers can recover.
Callers that do not check null can still fault, as they could before this patch.
On POSIX, including Android,
this raises SIGABRT, which the port's crash handler deliberately excludes
(`src/lambo_crash.cpp`). These failures therefore do not produce the project's
symbolised crash dump. For flush failure, controlled termination trades a
later failure for a deterministic diagnostic at the failing operation and
prevents stale data reaching a consumer.
The dependency does not synthesize a memory fault or change the port's global
SIGABRT policy. Include stderr/native logs in field reports when available.
Capture of this abort path in Android app diagnostics or platform crash
reports has not been verified here.

The caller audit covered pinned RT64, RecompFrontend and this port's patches
and hand-written sources. Texture-cache uploads, native-target uploads,
framebuffer parameters and UI texture/screen vertices use omitted ranges.
`BufferUploader::threadUpload` writes `srcOffset` through `srcOffset + srcSize`
and adds its own offset to the mapped base. Native-target readbacks use
explicit exclusive ranges; change-count reads follow `worker->wait()`, and
the workload path waits before copying native readbacks to RAM. The port's
evidence captures and patched RT64 alpha/owner/shadow probes also wait on
their workers before reading. Shadow-scene uploads declare an empty read
range and an explicit written range. Specifically,
`rt64_sun_shadow_scene.cpp`'s shared `upload()` helper checks a null map and
returns false. Returning null on map/invalidation failure preserves that path
on Vulkan as well as D3D12; it does not force optional shadow uploads to abort.

RecompFrontend maps its dynamic vertex/index/upload buffers in `begin`, then
unmaps in `end` before the caller submits the command list; resizing also
unmaps the old buffer. Its texture upload path unmaps before submitting its
copy list. No active caller retains a mapped pointer across GPU submission,
and Plume does not request `VMA_ALLOCATION_CREATE_MAPPED_BIT`. An API user that
keeps a mapping across GPU use must add explicit cache maintenance at the
access boundary; unmap alone cannot support that pattern. Completion fences,
barriers and caller synchronization are still required.

The recording VMA regression compiles the production declaration and mapping
methods, with assertions disabled. Before the fix it observes stale bytes in
both directions on its noncoherent model. It now checks ordering, coherent
and noncoherent bytes, omitted/empty/partial/full ranges, invalid extents and
subresources, write-only nested mapping, failures and cleanup. A 32-byte-atom
model rejects nested invalidation before a second VMA reference is acquired,
and verifies retention after flushing and closing mappings before a read.
The same contract checks run for coherent and noncoherent allocations.
CI requires the module.
See [testing commands](../docs/testing.md#vulkan-mapped-memory).

The original #372 snapshot `97b4644`, replayed without 0050, fails 39 of 46
`unittest` subtests (individual scenario/coherence combinations), with zero
setup errors. The earlier count of 34 came from an intermediate test version
and is superseded. The current tests build on #371's fixture and add recoverable
map failures and nested-reference preservation, so their totals differ.

The ROM-free GPU probe uses production Plume allocation, mapping, drawing,
submission and, after 0062, the production `copyTextureRegion` readback path.
It waits for a successful Vulkan fence before mapping and then checks 16 red
pixels in a padded, offset footprint, including that the row padding and the
bytes before the buffer offset were not overwritten. The earlier probe used a
native Vulkan image-to-buffer copy and transfer-to-host barrier because pinned
Plume's image-to-buffer copy dereferenced a null `dstTexture`;
[issue 366](https://github.com/alondero/automobililamborghini-recomp/issues/366)
tracked that separate defect, which 0050 did not change. It prints all memory
types, actual selected properties per heap, atom size and queue families. These
diagnostics are confined to the developer probe; production constructors do
not add an unconditional release log. It
also attempts compatible noncoherent allocations without changing production
allocation policy. `--require-noncoherent` makes unavailable physical coverage
exit with code 2; specific upload/readback/both requirements are supported.
Physical results and validation limits are recorded in
the [Android notes](../docs/ANDROID.md#mapped-buffer-memory).

Whole-buffer flush defaults remain conservative. Known written ranges can
reduce noncoherent work, but this follow-up does not change callers without
an appropriate measurement. Neither available device offers a compatible
noncoherent upload allocation for that performance comparison.

## Android presentation ownership (0054-0057)

Patch 0054 extends the pinned SDL2 Android integration with a retained native-window
snapshot and a generation query, including dynamic API exports. SDL publishes
creation, replacement and destruction under `Android_ActivityMutex`. Each callback
advances the generation even when the native pointer is reused. SDL releases its
own reference; Plume independently retains its snapshot until GPU resources and
`VkSurfaceKHR` retire. No Vulkan call or presentation wait occurs under that lock.

Patch 0055 keeps `RenderWindow` as `SDL_Window*` and uses the SDL-selected Vulkan
loader from 0014. Android surface creation and property queries move to RT64
Present. Loss, replacement and recoverable driver results recreate the surface
before the swap chain. Failed creation releases the snapshot and retries on a
later frame. Recovery failures report the unavailable window, surface creation,
presentation-support query or generation race, including a Vulkan result when
one exists. Identical reason/result pairs are logged once until the reason/result
changes or a resize succeeds. Present modes are reselected from the new surface while retaining
the requested vsync setting. Android acquire uses a 16 ms timeout. Timeout and
not-ready results skip the frame while preserving the healthy chain and its
synchronization objects; they do not drain or recreate it. Real acquisition errors
still invalidate the chain. Success and suboptimal acquisition proceed through
submission to consume the semaphore.

Drain and display-timing errors are reported once per distinct Vulkan result;
success resets suppression. After a failed drain, refresh-rate polling returns
unknown without another driver query, so display-rate retries do not flood logs.

Patch 0056 completes the present worker's rendering fence, drains pending Vulkan
submitted queue work, then destroys external framebuffers and synchronization objects
before resizing. Fresh semaphores replace any signalled but unconsumed objects
after failed presentation. Shutdown drains and retires references even without an
available surface. A failed drain marks the chain invalid and retains attachments
and semaphores for a later retry. Drain results distinguish completion, device
loss and other failures. Swap-chain teardown releases its resources after completion
or device loss; other errors abandon its GPU handles, framebuffer/semaphore wrappers
and retained native-window leases without allocating. Their automatic member
destruction cannot release those abandoned objects. This is a local ownership
policy: `Application::end()` still destroys uploaders, the texture cache, workers
and the device, whose release destroys the allocator/device without another wait.
Safe application-wide shutdown after unknown completion remains unverified; queued
work could still cause a quit-time crash or Vulkan validation errors. A lost device
does not cause the swap-chain destructor to abort. A hung driver can still
block the native present thread. Android's UI thread never
waits for that drain. The frontend uses the same presentation attachments.
Unextended Vulkan queue idle is a compatibility fallback, not a formal guarantee
that the presentation engine released all WSI references. The pinned backend does
not enable `swapchain_maintenance1` presentation fences. Stronger WSI retirement
and live driver transition coverage remain open validation limits; the fixture's
recording driver cannot establish those guarantees. See the
[Khronos guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).

Patch 0057 adds a host suspension API to the pinned runtime, which has no existing
application pause API. It freezes guest count/time, timer deadlines and retraces.
Foreground timers retain their deadline waits; the added 1 ms sleep applies only
while suspended. A wait interrupted by a pause rechecks the shifted guest deadline
without adding foreground polling or a sleep.
An SDL event watch invokes it before SDL blocks the main pump. The port's existing
audio boundary clears queued PCM and converter history and drops in-flight audio
while suspended. Java's Activity-pause and surface-loss handlers release touch controls; the existing
main-thread sensor event path closes and recenters gyro state.

Compared on 2026-10-28 with pinned Plume `51b1ad4` and
[current Plume `0ce184b`](https://github.com/renderbag/plume/blob/0ce184b19270e49236afbb74052a6b267494b86b/plume_vulkan.cpp):
both create a surface once in the constructor and acquire with `UINT64_MAX`.
Pinned RT64 `f0728a2` and
[current RT64 `d9817c6`](https://github.com/rt64/rt64/blob/d9817c68ffd7328cd0af383af124971489366e18/src/hle/rt64_present_queue.cpp)
both resize before clearing presentation framebuffers. Pinned runtime `cdf5abb`
is also its current upstream HEAD; its clock runs continuously and VI has no host
suspension gate. Pinned SDL `5d24957` and
[SDL2 `8acf7cf`](https://github.com/libsdl-org/SDL/blob/8acf7cfc0f88717e5c036f60f0314bfcafe39189/src/core/android/SDL_android.c)
publish neither a generation nor a retained renderer snapshot. DKR's
[retirement change](https://github.com/ThatGuyMcd/DKR-R/blob/d457741b57afff8d2ec96b2307f11c1340a4162f/patches/android/present-surface-ownership.patch)
was reviewed before implementing this order; its ANativeWindow-only surface patch
was not copied over this port's SDL-window contract. No upstream issue was opened.

The series applies 0054 after 0048, 0055 after 0053, 0056 after 0046, and 0057
after the runtime integration patches. 0055 changes the constructor context of
0047 and 0049. Production callback, swap-chain, retirement and clock fixtures are listed in
[testing](../docs/testing.md#android-presentation-lifecycle).
Device limits are recorded in [Android](../docs/ANDROID.md#native-surface-recovery).

## Further patch notes

Patch 0041 (`0041-plume-variable-descriptor-pool-retry.patch`) changes Plume's
Vulkan descriptor allocation on every platform. A
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
Vulkan swap chain on every platform:

- A failed capability query stops creation instead of using undefined values.
  If the query fails while the swap chain is constructed, format and alpha
  selection is retried on the next resize. A transient failure therefore does
  not leave the swap chain permanently unusable.
- A defined current extent is the surface's size and is used as the image
  extent. Only the special value `0xFFFFFFFF` lets the window size choose it.
  The result is then kept inside the surface's minimum and maximum. A zero
  extent, such as a minimized Win32 window, creates nothing. The chain
  remembers the window size it was built for, so a surface size that differs
  from the window does not cause a rebuild on every frame. While the chain's
  size differs from the window, each frame reads the surface again and
  rebuilds once its extent changes. Android can report a new window size a
  frame before the surface follows.
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
- a surface extent that follows the window a frame late;
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
0005/0006/0009/0010/0011/0021/0022/0024.

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
It changes the context of earlier RT64 patches. No upstream issue was opened.

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
skips checkout/clean operations and applies only the missing part of each
series. The continuation
replayed the nine Windows RT64 patches in a separate clean pinned worktree:
all 31 modified/new files matched the active patched dependency byte-for-byte.
No patch content or dependency pin changed in that continuation.

Which patches each platform applies, and in what order, is in
[series.txt](series.txt): Windows adds 0004 and 0005, Android adds 0013, 0014
and the fetched SDL's 0015, 0048 and 0054.

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
