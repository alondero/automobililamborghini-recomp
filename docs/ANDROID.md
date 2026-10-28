# Android

Status: buildable ARM64 path with device-specific limits. Device validation is
not part of the current support claim; use the checks below before promising a
device or driver combination.

The Android build is an ARM64 APK using SDL2, RT64/Vulkan, and the same
recompiled game/runtime as the desktop port. Minimum OS: Android 9 (API 28).
The GPU driver must support Vulkan 1.1 and scalar block layout. Descriptor
indexing is optional. A Vulkan version number alone does not guarantee
compatibility. The APK contains no ROM. Import your own USA cartridge dump using
the launcher; `.z64`, `.v64`, and `.n64` byte orders are accepted and the complete
ROM is validated before replacing an existing import.

Touch controls provide a steering stick, A and B, a small C pad for the camera, Start, and Menu (the port settings, top right). Z is not shown. It is a second accelerator button in the game, so A covers it. SDL gamepads and
keyboards use the desktop control mappings. Importing a ROM does not replace
your saves. Configuration, Controller Pak, and logs live in app-private storage;
uninstalling or clearing the app's data removes them. An APK update signed with
the same key retains this data.

In **Driving**, enable **Gyro steering** to steer by tilting the phone, or
**Auto-accelerate** to drive without holding the accelerator. Both default to
off and apply only to player one during active racing. Braking suspends
automatic acceleration. Gyro requires a gyroscope and accelerometer; hold the
screen upright when starting or resuming to center steering. See
[controller help](controllers.md) for tuning and current validation limits.

## GPU drivers and device validation

The game uses the phone's own GPU driver by default. Older drivers, such as
the 2020 Adreno driver on the tested phone, do not support descriptor indexing.
On those drivers the renderer uses a fixed-size texture table
([patch 0033](../patches/0033-rt64-fixed-texture-table-without-descriptor-indexing.patch))
instead of the variable-size table RT64 normally uses.

The launcher can also import an AdrenoTools-compatible Mesa Turnip ZIP on
Android 9+ without root. Try this if graphics fail to start or look wrong.
Select **Import GPU driver ZIP**, choose a driver for your GPU, then select
**Play**. **Use system GPU driver** restores the default for subsequent game
launches. Driver files remain private to this app; they are not bundled in the
APK. Custom Adreno drivers are specific to Qualcomm hardware; they are not a
compatibility solution for Mali GPUs.

The tested phone reports itself as a Pixel 5, but its vendor build is a Redmi
K30 Pro / POCO F2 Pro (`lmi`, Snapdragon 865, Adreno 650) on Android 13. Its
system Vulkan driver is Qualcomm build `4783c89` (dated 11/30/20, Vulkan
1.1.128) without `VK_EXT_descriptor_indexing`. With that driver, the title
screen and attract races on three tracks rendered correctly. With
[Mesa Turnip v25.3.0-R11](https://github.com/K11MCH1/AdrenoToolsDrivers/releases/tag/v25.3.0-rc.11),
the title, attract race, and player/car selection have been reached. Both
drivers presented roughly 24 to 30 frames per second in the attract race.
A Galaxy Tab S5e (Snapdragon 670, Adreno 615) on Android 11 with that Turnip
driver presented 29 to 30 frames per second in the attract race. See
[Android frame pacing](performance.md#android-frame-pacing-2026-10-26).
Other GPUs and driver versions have not been validated.

On the SM-T720's Adreno 615 system driver `0x801f6000`, automatic resolution
is limited to 2x native resolution. The normal window-sized scale loses the
device during startup on this tablet. A compatible Turnip driver keeps the
normal automatic scale. See the [startup investigation](#adreno-615-startup-device-loss).

RT64's shaders declare non-uniform texture indexing, which these older drivers
do not support. On drivers without descriptor indexing,
[patch 0044](../patches/0044-plume-vulkan-shader-nonuniform-without-descriptor-indexing.patch)
removes those declarations before the driver sees them. This is safe for this
game: each draw uses the same texture index for every pixel, because the game
never enables N64 texture LOD ([audit](no_lod_audit.md)). To check a device, see
[Validate a driver without descriptor indexing](#validate-a-driver-without-descriptor-indexing).

SDL and the renderer share an app-local Vulkan loader so they use the same driver
and surface. Native libraries must be extracted by Android (`useLegacyPackaging`)
for AdrenoTools hooks. NDK 28 supplies 16 KiB-aligned native libraries.

## Packaged assets and startup checks

The nested APK paths `assets/assets/...` are intentional. Android's
AssetManager exposes the ZIP's outer `assets/` directory as its root. The
launcher copies its `assets` subdirectory to app-private `files/assets/`,
and copies `lamborghini.syms.toml` beside that directory. Native startup uses
app-private storage as its working directory. Flattening the APK paths breaks
fresh extraction. A direct GameActivity launch skips extraction and cannot
validate this contract.

The packaging script validates all four UI fonts and `recomp.rcss`, plus the
symbols file. See [Testing](testing.md) for the ROM-free `assetcheck` and the
launcher-driven `gameboot` soak. PR 287's original flattening diagnosis was
contradicted by this staging contract, and its polling loop did not implement
the claimed 45-second soak. The original missing-font cause remains unverified;
missing staged files, including after direct GameActivity startup, are a
possible explanation. Preserve the failed launch's diagnostics before retrying.

## Export diagnostics after a failed launch

Return to the launcher and select **Export diagnostics ZIP**. Choose where to
save it, then attach the ZIP to your bug report. If Android shows an app-not-
responding dialog, close the game and reopen the launcher first. Export before
pressing **Play** again: the next game launch replaces the native logs. Do not
clear app data or reinstall before exporting.

The export works in release APKs without root or `run-as`. It includes:

- Native output (`native.log` and `native-errors.log`). If no frame is
  presented for 3 seconds, `native-errors.log` also gets a
  [present stall report](#present-stall-report).
- Up to ten recent session logs and three crash reports.
- Graphics settings and device, Android, app-version and selected-driver details.
- On Android 11+, retained game-process exit records and up to three available
  traces. Android may no longer have a trace. Native crash traces use protobuf;
  ANR traces use text.

Each file is limited to 512 KiB. Logs retain their last 512 KiB; system traces
retain their first 512 KiB. `report.txt` records missing files and truncation.
The ZIP excludes ROMs, saves, imported driver libraries and texture packs.
Older logs and exit records may come from a different app version or driver
selection; compare their timestamps with the failed launch.

### Black screen or exit during startup

[Issue 329](https://github.com/alondero/automobililamborghini-recomp/issues/329)
reports system-driver exits with status 1 and Turnip black screens or an ANR on
an Adreno 730 device running Android 15. The v0.10.0 diagnostic exports identify
descriptor-pool exhaustion followed by a native crash with the system driver.
Patch 0041 retries the variable descriptor allocation with a larger pool; see
the [investigation below](#adreno-descriptor-pool-investigation). The v0.10.1
exports show that retry working: the system driver no longer crashes. All three
tested drivers now end in the same ANR, with RT64 Present and Android's UI
renderer waiting on a Qualcomm gralloc lock. See the
[present stall investigation](#present-stall-report). The v0.10.2 exports name
the game's graphics thread as that lock's holder; see the
[gralloc lock holder](#gralloc-lock-holder). The shared
`AHardwareBuffer` format-allocation errors also occur during successful startup
on the older Adreno 650 test phone; they do not establish a missing Vulkan
feature or the cause of the hang.

On 2026-10-23, a fresh ARM64 build based on `2046bb5` reached the title and
attract race with both drivers on the Android 13 / Adreno 650 test phone,
using its existing configuration. The system driver also responded to the
touch Menu button. The export checks and Save dialog passed with a
non-debuggable package, where `run-as` was rejected. No retained ANR trace was
available to verify trace copying. Selection of an older available trace after
newer trace-free exits was code-reviewed, without a deterministic device test.
The Adreno 730 / Android 15 failure and a clean-configuration run remain untested.

For a useful report, export immediately after each driver test and name the
driver and time of the attempt. A system-driver exit and a Turnip ANR need
separate captures. A UI-thread stack in `HardwareRenderer` identifies where
the UI waited; it does not by itself identify the underlying driver fault.

### Adreno descriptor-pool investigation

The two ZIPs attached to issue 329 were reviewed on 2026-10-26. Both report
v0.10.0 on the Xiaomi `diting` device, Android 15. Match the logs to their
timestamps: historical exits and crashes are also included.

- The first export selects Turnip v25.3.0-R11. Its native output names
  `Turnip Adreno (TM) 725`, vendor `0x5143`, version `0x6402063`. Process
  14108 starts at 23:33:41 UTC and ends in an ANR. Its retained trace shows
  Android's UI thread waiting for HWUI. HWUI's RenderThread and RT64 Present
  both wait on priority-inheritance mutexes in Qualcomm gralloc. Present's
  call chain includes `AHardwareBuffer_isSupported` and Turnip's
  `vk_android_get_ahb_image_properties`. RT64 Idle is inside
  `kgsl_queue_submit`. This locates the graphics-buffer stall but does not
  identify the lock owner or prove a fix. It is not evidence of a missing
  scalar-layout feature.
- The second export selects the system driver. Process 14830 starts at
  23:34:27 UTC. Its output names `Adreno (TM) 730`, vendor `0x5143`, version
  `0x8026705f`. Three allocations fail with `0xC4642878`, signed decimal
  `-1000069000`: `VK_ERROR_OUT_OF_POOL_MEMORY`. The subsequent SIGSEGV is
  captured by the native handler, which exits with status 1. Thus Android's
  `EXIT_SELF` record does not mean the launch ended without a native fault.

Plume sizes a variable descriptor binding's pool to the requested count, while
its layout declares a larger upper bound. Some Adreno drivers account the
upper bound against the pool. An independent
[Castlevania port patch](https://github.com/sergiomanzur/cvlod_recomp/blob/master/patches/plume.patch)
documents the same allocation failure. The original Plume code also returns a
set object after allocation failure, allowing callers to pass a null set to
the driver.

Patch 0041 keeps the small pool on the first attempt. Only a variable-count
allocation returning `VK_ERROR_OUT_OF_POOL_MEMORY`, with a requested count
below the layout capacity, gets a fresh pool sized for that capacity. The
retry preserves the layout and requested count and accumulates other bindings
of the same descriptor type. A persistent failure stops immediately with a
flushed diagnostic instead of continuing with the failed set.

The ROM-free regression compiles Plume's real constructor and pool builder
against a recording Vulkan boundary. The driver model charges layout capacity
and reproduces the same hexadecimal error before the fix; the retry passes.
It also covers ordinary driver allocation, fixed tables, zero and nonzero
variable counts, persistent failure, and failure at full capacity. Run:

```sh
python -m unittest tests.test_vulkan_descriptor_pool -v
```

This establishes the allocation workaround in isolation. The Xiaomi's system
driver still needs a device retest, and the Turnip gralloc stall is not fixed
by this patch. A separate SM-T720 / Android 11 / Adreno 615 baseline check with
the unmodified v0.10.0 APK stayed black and repeatedly logged
`VK_ERROR_DEVICE_LOST`; it did not reproduce descriptor-pool exhaustion.
The locally rebuilt APK with 0041 displayed the opening screen, then also
lost the device and produced incomplete title graphics on that system driver
(`0x801f6000`, Qualcomm build `4fb6cde`, dated 2020-12-15). This is a separate
failure tracked in [issue 345](https://github.com/alondero/automobililamborghini-recomp/issues/345),
not a successful validation of the pool retry.

On the same tablet with Turnip v25.3.0-R11 (`0x6402063`), the patched APK
reached an attract race and its touch Menu button opened the settings overlay.
No Vulkan allocation, submission or fence error appeared in the captured
native output. The driver library and selection were staged in the debug
app's private files using ADB; this check does not validate the ZIP importer.
Neither driver logged the pool-retry message, so the Xiaomi's allocation
workaround remains validated by the host fixture rather than this tablet.
The APK was built on Windows with the supported Android build script, NDK
28.2.13676358, CMake 3.22.1, MinGW host tools and JBR 21.0.9; generated USA
and RSP output was freshly created. No modified generated output was committed.

### Present stall report

The three v0.10.1 exports attached to issue 329 were reviewed on 2026-10-27.
Each reports app 0.10.1 on the Xiaomi `diting`, Android 15:

| Driver | Process | Native output | Result |
| --- | --- | --- | --- |
| System, `0x8026705f` | 25122 | Three `Vulkan descriptor pool exhausted; retrying` lines, no crash | ANR |
| Turnip T30, `0x6802063` | 25410 | No error | ANR |
| Turnip v25.3.0-R11, `0x6402063` | 25641 | No error | ANR |

Patch 0041's retry therefore worked on the device. The retained ANR traces
then show the same stall for every driver:

- HWUI's RenderThread waits on a priority-inheritance mutex in
  `gralloc::BufferManager::SetMetadata`, during `eglSwapBuffers`. Android's
  UI thread waits for that frame, so touch input times out.
- RT64 Present waits on a priority-inheritance mutex in Qualcomm gralloc while
  RT64 rebuilds the swap chain. With the system driver it is in
  `vkCreateSwapchainKHR`, then `BufferManager::RetainBuffer`. With Turnip it is
  in `AHardwareBuffer_isSupported`, then `BufferManager::AllocateBuffer`.
- The graphics thread (`Thread-7`, `gfx_thread_func`) is runnable and had used
  about 17.5 s of CPU. Its frames in the stripped v0.10.1 `libmain.so`
  (build ID `995e24d2`) disassemble to RT64's
  `PresentQueue::advanceToNextPresent`. That loop locked and unlocked
  `cursorMutex` until Present released a queue slot.
- No dumped thread has gralloc on its stack, so the trace does not show which
  thread holds the lock.

Two changes follow from this. Neither is shown to release the gralloc stall.

[Patch 0046](../patches/0046-rt64-queue-barrier-wait.patch) makes RT64's
present and workload producers sleep on a condition variable instead of
spinning. A host fixture compiles the patched methods, holds each barrier for
400 ms, and requires the producer to use under 100 ms of CPU. Before the patch
it used 375 to 422 ms.

`src/lambo_stall_report.cpp` writes a thread report to `native-errors.log`
when no frame has been presented for 3 seconds. It writes at most three
reports per launch. For each thread it lists name, state, CPU ticks and kernel
wait channel. For a futex wait it also gives the lock word. For a
priority-inheritance lock it names the holder from the lock word:

```text
Present stall: no frame for 3000 ms after 1 frames. Thread waits:
  tid 23709 'PI Waiter' S cpu=0 wchan=rt_mutex_timed_futex_lock futex LOCK_PI 0x5ea89d5e10 word=0x80005c94 owner=23700 holder: tid 23700 'PI Holder' S cpu=0 wchan=hrtimer_nanosleep
End of present stall report.
```

Match the holder's thread ID against `sysTid` in the ANR trace for its stack.
`holder: not a thread of this process` means the lock word names a thread
outside the game process. `holder: none` means the lock word has no owner, so
the lock is free or being released. Moving the app to the background also stops
frames, so a report then is expected.

The example above is from the on-device probe on the SM-T720 (Android 11),
where one thread holds a priority-inheritance mutex and another blocks on it.
Run it with a single device attached:

```sh
python -m unittest tests.test_android_stall_report -v
```

The Xiaomi stall itself has not been reproduced locally.

The three v0.10.2 exports attached to issue 329 were reviewed on 2026-10-27.
The system driver no longer exits: it logs the descriptor-pool retry, presents
133 frames, and then stalls. Both Turnip drivers stall after 98 frames. Every
ANR trace shows the same wait as v0.10.1. RT64 Present is rebuilding the swap
chain and waits on the gralloc `BufferManager` lock. HWUI's RenderThread waits
on the same lock. No other thread has gralloc on its stack.

None of the three reports gave a lock word. Each line had only the thread
name, state, CPU and wait channel. A release app is not dumpable. The kernel
then gives root the owner-only files under `/proc/<pid>/`, such as `syscall`
and `mem`, so the app cannot read them. The SM-T720 probe above ran as a
dumpable `adb shell` binary and did not show this. The report now makes the
process dumpable while it reads these files, then restores the previous
setting. A thread whose `syscall` file still cannot be read is marked
`syscall=unreadable`. The device test above runs the probe a second time
without the dumpable flag. Before the fix that case reproduced the Xiaomi
output; it now names the holder. Verified on a Pixel 5, Android 13.

### Gralloc lock holder

The three exports attached to issue 329 on 2026-10-28 report app 0.10.2
(10002) on the Xiaomi `diting`, Android 15. Each stall report now gives the
lock word:

| Driver | Process | Report frame count | Lock holder |
| --- | --- | --- | --- |
| System, `0x8026705f` | 15705 | 170 | `Thread-7`, tid 16991 |
| Turnip T30, `0x6802063` | 16882 | 106 | `Thread-7`, tid 19390 |
| Turnip v25.3.0-R11, `0x6402063` | 17096 | 106 | `Thread-7`, tid 20001 |

- RT64 Present waits on the priority-inheritance lock in gralloc's
  `BufferManager`. With the system driver, HWUI's RenderThread waits on the
  same lock. The lock word names `Thread-7` as its owner.
- `Thread-7` is the graphics thread: its ANR stack starts in
  `gfx_thread_func`. Its innermost frame in the stripped `libmain.so` (build
  ID `4f349a70`) has the shape of patch 0046's barrier wait, where RT64 waits
  for a free queue slot. It has no gralloc frame. It holds gralloc's lock while
  it is idle.
- No export has a `Vulkan swap chain created` line. Present blocks in the
  first `vkCreateSwapchainKHR`, so no chain was ever made.

So the graphics thread took gralloc's lock earlier and kept it. RT64 constructs
the swap chain on the graphics thread, in `Application::setup`, and Present
creates the chain later. Android 15's loader answers
`vkGetPhysicalDeviceSurfaceFormatsKHR` by asking gralloc about five formats
with `AHardwareBuffer_isSupported`
([`swapchain.cpp`](https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android15-release/vulkan/libvulkan/swapchain.cpp),
read 2026-10-28). Two of them are `R8_UNORM` (`0x38`) and
`R10G10B10A10_UNORM` (`0x3b`), the formats this gralloc logs as
unrecognized. The loader's capability and present-mode queries only read the
window. No port code on the graphics thread calls gralloc. The format query is
therefore the graphics thread's only known path into gralloc. That gralloc
keeps its lock after these probes is an inference; its source is not public.

One report argues against this being the whole cause. DKR-R's RT64 makes the
same calls on the same two threads, and the reporter says DKR-R runs on this
phone.

[Patch 0049](../patches/0049-plume-android-surface-format-without-gralloc-probe.patch)
skips the format query on Android when the swap chain requests
`R8G8B8A8_UNORM`, as the port does. The loader always lists that format with
sRGB first, without asking gralloc, so the chosen format is unchanged. The
swap chain fixture models the held lock and fails without the patch:

```sh
python -m unittest tests.test_vulkan_swapchain -v
```

The game also checks gralloc before and after renderer setup. A helper thread
asks `AHardwareBuffer_isSupported` about a 1x1 RGBA8 buffer, the question
Turnip's swap chain asked when Present blocked. Each check writes one line to
`native-errors.log`. A debug build with patch 0049 on a Galaxy Z Fold6
(Android 16, Adreno 750, system driver) wrote:

```text
Graphics buffer check before renderer setup (tid 7365): answered in 0 ms.
Graphics buffer check after renderer setup (tid 7365): answered in 0 ms.
```

The tid is the graphics thread; Android may rename it `Thread-N`. If gralloc
does not answer within 1 second, the line ends `no answer after 1000 ms. Probe
wait:` and the helper's wait follows, with the lock holder, as in the stall
report. A held lock before setup points to another thread. A held lock only
after setup points to setup itself. The device test above also runs a blocked
check and a real one.

On the Fold6, 0.10.2's graphics thread logged gralloc's rejection of format
`0x3b` during setup; with patch 0049 it logged none. That gralloc released
its lock after the probes in a standalone test, so this device does not
reproduce the 329 hang. Its system driver then fails to build five compute
pipelines and the game crashes, with or without the patch. That crash is a
separate problem.

### SDL surface callback logs

The game window is a Vulkan window. Pinned SDL 2 still ran OpenGL work for it.
On Android's UI thread, each surface change tried to create an EGL surface,
and each surface loss waited, up to 490 ms, for the game thread to release an
EGL context. On the game thread, each pause and resume backed up and restored
a GL context. For this window the restore failed and reported a spurious
`SDL_RENDER_DEVICE_RESET`. An EGL surface would stop Vulkan from using the
window.
[Patch 0048](../patches/0048-sdl-android-vulkan-surface-lifecycle.patch)
limits all of this to OpenGL windows, as upstream SDL 3 does. See the
[patch notes](../patches/README.md) for the upstream comparison.

SDL's external-context hint is therefore not needed. It is process-wide and
would change OpenGL handling too; the window flag scopes the change to the
one window that is not OpenGL.

To log each surface decision, set a debug property before starting the game:

```sh
adb shell setprop debug.lambo.sdl_logging video=debug
adb logcat -s SDL:V SDL/VIDEO:D
```

The value is passed to SDL as `SDL_LOGGING`. The lines also go to
`native-errors.log`, so a diagnostics ZIP contains them. Clear the property
with `adb shell setprop debug.lambo.sdl_logging "''"`, or reboot.

```text
DEBUG: onNativeSurfaceChanged: window flags 0x10001625, EGL surface skipped
DEBUG: onNativeSurfaceDestroyed: window flags 0x10001465, EGL backup wait skipped, 0 ms
```

`0x10000000` in the flags is `SDL_WINDOW_VULKAN`; an OpenGL window has `0x2`.

On 2026-10-27 the Android 13 / Adreno 650 test phone with Turnip ran a cold
start, Home and resume, and both landscape rotations. Debug APKs with and
without the patch differed only in SDL. The window never had the OpenGL flag.
Without the patch every surface change tried EGL and failed, because SDL had
not loaded EGL, and every surface loss waited at most 40 ms. With the patch
both were skipped. The game looked the same in both builds. The run details
are in [issue 358](https://github.com/alondero/automobililamborghini-recomp/issues/358).

The final patched build then ran the same steps plus screen off and on, in
one game process. The screen-off surface loss and the surface change after
unlocking were both skipped, like the earlier ones. The game did not crash.

With the system driver, both builds crashed on the first surface loss (Home
or screen off) with the same native backtrace. The patched build logged
"skipped" before each crash, so the crash is not caused by this patch. A
fix for that crash was then in review in
[pull request 363](https://github.com/alondero/automobililamborghini-recomp/pull/363).

In both builds the game area stayed black after Home and resume, while RT64
repeated `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR` from `vkCreateSwapchainKHR`.
Recovering the renderer after a surface loss is
[issue 359](https://github.com/alondero/automobililamborghini-recomp/issues/359).

The game thread's pause and resume steps have no log line; the host test
covers them. Not yet tested: the issue 329 Xiaomi phone, a system-driver run
past the first surface loss, and a release APK reading the property. Whether the old work played any part in
issue 329's gralloc stall is unknown.

## Swap chain checks (issue 357)

[Patch 0047](../patches/0047-plume-vulkan-surface-capabilities.patch) makes
the Vulkan swap chain follow what the surface reports. These checks ran on
2026-10-27 on the Adreno 650 phone above (Android 13).

The APK was built with `scripts/build_android.py`. The USA ROM had SHA-256
`cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c`. The build
regenerated `RecompiledFuncs/` and `src/aspMain.cpp` from it. The baseline APK
was the same build with patch 0047 left out of Plume.

Two kinds of run were used. A Home run did a cold start, waited 30 s in the
attract race, then pressed Home and resumed twice. A resize run did a cold
start, then shrank the display with `adb shell wm size 900x2000` and restored
it with `adb shell wm size reset`, twice, then opened the Menu. Android resizes
the game's surface in place, so each change replaces the swap chain on the
same surface. A cold start logged this on both drivers:

```text
Vulkan surface capabilities: extent current 2400x1080 min 1x1 max 4096x4096, images min 3 max 64, transforms 0x1FF current 0x2, alpha 0x8, usage 0x9F.
Vulkan swap chain created: extent 2400x1080 (window 2400x1080), images 3 (requested 3), usage 0x10, transform 0x1, alpha 0x8, format 37, color space 0, present mode 2.
```

The surface's current extent matched SDL's window size. The current transform
was a 90 degree rotation (`0x2`) when the launcher had been in portrait, and
identity (`0x1`) otherwise. The chain uses the identity transform in both
cases, and the race rendered upright in every cold start. With 0047, the
Menu overlay also drew over the race on both drivers.

| Build | Driver | Runs | Cold start | After Home and resume |
| --- | --- | --- | --- | --- |
| Baseline | System, `0x801ea000` | 1 | Race renders | Exits after a failed `vkCreateSwapchainKHR` (`0xC4653600`) on Home; the relaunched game exited the same way on resume, logging `SIGSEGV` |
| Baseline | Turnip v25.3.0-R11 | 1 | Race renders | Running, game area black; 134 `vkCreateSwapchainKHR` failures with `0xC46535FF` |
| 0047 | System, `0x801ea000` | 2 | Race renders | Running, game area black; one logged `0xC4653600` refusal |
| 0047 | Turnip v25.3.0-R11 | 2 | Race renders | Running, game area black; one logged `0xC4653600` refusal |

`0xC46535FF` is `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR`: the baseline kept
passing the retired chain as `oldSwapchain`. `0xC4653600` is
`VK_ERROR_SURFACE_LOST_KHR`. With 0047 every retry fails because the surface
is gone; the message is written once, but RT64 still retries every frame.

The first device build of 0047 crashed on every Home with Turnip. After a
failed replacement Plume holds no chain. RT64 still asked for the refresh rate
each frame, and Android's loader dereferenced the empty handle. Plume now
reports an unknown refresh rate without a chain. The host fixture's abandoned
surface scenario reproduces this.

### Replacing the chain on a live surface

| Build | Driver | Resize runs | Result |
| --- | --- | --- | --- |
| 0047 | System, `0x801ea000` | 3 | Every change replaced the chain (4 or 5 replacements per run, see below); game content after each change; Menu overlay drawn |
| 0047 | Turnip v25.3.0-R11 | 2 | Every change replaced the chain (4 replacements per run); game content after each change; Menu overlay drawn |
| Baseline | System, `0x801ea000` | 1 | Game content after each change; Menu overlay drawn |
| Baseline | Turnip v25.3.0-R11 | 1 | Game content after each change except one black frame on the title-screen transition; the title rendered 3 s later |

No replacement failed and no process exited. 0047 creates each replacement
with the live chain as `oldSwapchain`. The log showed each new extent:

```text
Vulkan swap chain created: extent 2000x900 (window 2000x900), images 3 (requested 3), usage 0x10, transform 0x1, alpha 0x8, format 37, color space 0, present mode 2.
Vulkan swap chain created: extent 2400x1080 (window 2400x1080), images 3 (requested 3), usage 0x10, transform 0x1, alpha 0x8, format 37, color space 0, present mode 2.
```

On the system driver, SDL sometimes reported the new window size about a frame
before the surface's current extent followed. In 3 of 3 system runs one change
logged `extent 2400x1080 (window 2000x900)`. The first 0047 build kept that
stale chain until the next size change; the compositor scaled it. 0047 now
reads the surface again while the chain's size differs from the window. In the
two runs after that change, the next line was `extent 2000x900 (window
2000x900)`. The baseline sized the chain from the window and does not log, so
its extents are unknown.

### Home and resume

The game area stays black after resume with either build. RT64's Vulkan
surface belongs to the window Android destroyed on Home, so a new swap chain
needs a new surface. That is
[issue 359](https://github.com/alondero/automobililamborghini-recomp/issues/359).

On the system driver, every cold start (baseline and 0047) wrote one present
stall report before the first frame. RT64 Present was idle and no swap chain
had been created yet, so it is not a swap chain wait.

The Xiaomi phone from [issue 329](https://github.com/alondero/automobililamborghini-recomp/issues/329)
was not available. Its swap chain behaviour with 0047 is unverified.

The patch also changes desktop Vulkan. On Windows 11 with an NVIDIA GeForce
RTX 3080 (driver 32.0.16.3020), a `build.ps1` build from the same ROM started
the attract race in a 1280x720 window:

```text
Vulkan surface capabilities: extent current 1280x720 min 1280x720 max 1280x720, images min 2 max 8, transforms 0x1 current 0x1, alpha 0x3, usage 0x9F.
Vulkan swap chain created: extent 1280x720 (window 1280x720), images 3 (requested 3), usage 0x10, transform 0x1, alpha 0x1, format 44, color space 0, present mode 2.
```

Resizing to 1578x844 and then 778x544 each created one swap chain at the new
size, and the race filled the window. Minimizing created none; restoring
created one. The settings overlay (F1) then drew over the race. D3D12 also
rendered the attract race.

Some desktop Vulkan starts on this PC exit before the first frame, with
`0xC0000374` (heap corruption) or `0xC0000409`, or never present. The pinned
build does this too. In 10 alternating cold starts of each build, the build
without 0047 failed 7 times and the build with it failed twice. The later
0047 builds used for the checks above failed 3 of 6 starts. The cause has
not been found.

## Adreno 615 startup device loss

Status: a resolution-dependent system-driver fault is reproduced. Patch 0042
limits automatic resolution on the measured driver; its internal fault is
unverified. This is separate from the descriptor-pool exhaustion above.

The 2026-10-26 checks used Samsung SM-T720 / `gts4lvwifi`, Android 11,
Adreno 615, system driver `0x801f6000` (Qualcomm build `4fb6cde`, 2020-12-15),
and the supported USA ROM with SHA-256
`cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c`.
A fresh debug APK was built from `aebda1d` using the supported Android script,
NDK 28.2.13676358, CMake 3.22.1, MinGW host tools and JBR 21.0.9.
USA and RSP output was freshly generated. Candidate changes were rebuilt with
CMake and packaged with `scripts/build_android.py --package-only`.

Automatic resolution selected a 6x internal scale for the 2560x1438 surface
and 320x240 VI. This was measured at the renderer's scale-selection boundary.
The tests changed graphics settings independently of the existing user
configuration; no texture packs or mods were active.

| Driver and settings | Observed result before 0042 |
| --- | --- |
| System, default Auto resolution and MSAA2X | Black or incomplete startup graphics, then repeated `VK_ERROR_DEVICE_LOST`, fence timeouts and unavailable query results. Reproduced with and without validation. |
| System, only resolution changed to Original2x | Reached an attract race with no Vulkan submission or fence errors in the capture. |
| System, Auto with MSAA disabled | Still lost the device. |
| System, Auto with an experimental clear-rectangle clamp | Clear validation errors disappeared; device loss remained. Experiment reverted. |
| Turnip v25.3.0-R11, default Auto and MSAA2X | Reached an attract race without Vulkan submission or fence errors. Driver selection was staged through ADB, not the ZIP importer. |

Khronos validation layer 1.3.290.0 reported unsupported shader non-uniform
indexing (`VUID-VkShaderModuleCreateInfo-pCode-08740` and `-08742`) and
oversized clear rectangles (`VUID-vkCmdClearAttachments-pRects-00016`).
The latter also occur on Turnip. Neither establishes the cause of device
loss: clipping the clears did not fix it, and the system driver also renders
at 2x with the shader declarations unchanged. The first KGSL submission
failure preceded the command-buffer reuse, fence reuse and descriptor-update
validation errors. Those later errors do not explain the initial fault.
No minimized GPU reproducer or driver debugger capture was obtained.

Patch 0042 matches Android, vendor `0x5143`, version `0x801f6000`, and the exact
name `Adreno (TM) 615`. It bounds only automatic scale to at most 2x, retains
1x on small or unavailable surfaces, and logs the workaround at startup.
The saved Auto setting is preserved. Explicit native/manual choices and
supersampling keep their existing meaning; higher manual scales have not been
validated on this driver. Other GPU/driver combinations, including Turnip,
keep their existing automatic scale. Use a compatible Turnip driver when
window-sized rendering is needed on this tablet.

The ROM-free fixture compiles the production scale-selection switch for
Android and desktop. It fails against the original code (`expected 2, got 6`)
and covers the measured combination, other models/versions/vendors, Turnip,
manual/native settings and small surfaces:

```sh
python -m unittest tests.test_android_resolution_limit tests.test_patch_composability -v
```

The candidate with 0042 reached the attract race with the system driver and
default Auto/MSAA2X settings, both with and without validation enabled. The
touch Menu button opened settings. Captures contained the startup workaround
message and no Vulkan submission, fence or allocation failures. Shader and
clear validation errors remain; opening settings also reported
`VUID-vkCmdDrawIndexed-renderPass-02684`. These are outside this resolution
workaround, and this result does not claim a validation-clean renderer.
Long play sessions and other system-driver versions remain untested.
Patches 0043 to 0045 later removed these validation errors
([issue 350](https://github.com/alondero/automobililamborghini-recomp/issues/350));
see the [validation result](#validate-a-driver-without-descriptor-indexing).
The candidate used the successful initial clean build followed by incremental
CMake compilation and packaging. At that point, repeating source preparation
rejected the composed RT64 patches, including without 0042
([issue 351](https://github.com/alondero/automobililamborghini-recomp/issues/351)).
Source preparation now validates the full composed Android and shared CMake
series in a temporary Git index. It recognizes each shared patch prefix and
leaves those patches for CMake to apply on a clean build. Conflicting edits
to patch hunks still fail; existing files and the real index are preserved
when the composed series is already present. ROM-free patch replay tests
cover repeat preparation and conflict rejection. This preparation fix has
not been verified with an APK build or a device run.

## Build locally

Requirements: Python 3.11 or newer, Git, CMake 3.22+, Ninja, JDK 17, Android SDK platform 35,
build tools 35.0.0, and NDK 28.2.13676358. Windows host tools use MinGW-w64 GCC
(put its `bin` directory on PATH); Linux uses GCC. Use native Windows CMake,
not an MSYS CMake. Gradle 8.12 is bootstrapped by the checked-in wrapper.

```sh
sdkmanager 'platforms;android-35' 'build-tools;35.0.0' 'ndk;28.2.13676358'
export ANDROID_HOME=/path/to/Android/Sdk
export JAVA_HOME=/path/to/jdk-17
python3 scripts/build_android.py --install
```

PowerShell example after setting `JAVA_HOME` and adding MinGW-w64 to PATH:

```powershell
$env:ANDROID_HOME = "$env:LOCALAPPDATA/Android/Sdk"
python scripts/build_android.py --cmake "$env:ANDROID_HOME/cmake/3.22.1/bin/cmake.exe" --install
```

Supply `Automobili Lamborghini (USA).z64` at the repository root for the host
recompilation step. The script initializes submodules, applies the shared runtime
and renderer patches without discarding local edits, fetches pinned SDL and
FreeType and AdrenoTools sources, builds host recompilers and `file_to_c`, regenerates the game,
cross-compiles the native libraries, and packages a debug APK. Host DXC creates
SPIR-V shader blobs; Android executables are never run on the build machine.

RecompFrontend includes SDL through the `<SDL2/...>` prefix, which only SDL's
generated header tree satisfies: SDL copies its public headers into
`build-android/native/SDL/include/SDL2` and writes the generated `SDL_config.h`
to a sibling `build-android/native/SDL/include-config-<build type>/SDL2`
directory. The Android build therefore does not use SDL's flat source
`include/` directory for that target; the root `CMakeLists.txt` links
`recompui` against the SDL2 target, which both supplies that tree and orders
the compile after `sdl_headers_copy` has produced it. A missing `SDL2/...`
header on this platform means that wiring, not the SDL fetch.

Output: `dist/lamborghini-recomp-android-arm64-debug.apk`. `--package-only`
repackages an already compiled native build. `--install` uses `adb install -r`;
connect one device and authorize USB debugging first. Open **Lamborghini
Recompiled**, import your ROM through Android's file picker, and select Play.
No broad storage permission is needed.

The Android instrumentation checks use isolated cache fixtures to exercise
driver import and diagnostic ZIP export, including missing logs, size limits,
game-data exclusions, symlinks and destination failures. After building and
installing the debug APK, run:

```sh
android/gradlew -p android assembleDebugAndroidTest
adb install -r android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb shell am instrument -w io.github.alondero.lamborghinirecomp.test/io.github.alondero.lamborghinirecomp.DriverImportInstrumentation
```

Use `android/gradlew.bat` on Windows. These fixtures do not induce an ANR or
verify retrieval of a retained system trace. Check the Save dialog separately
on a device, including a non-debuggable package, before releasing export changes.

Generated dependencies, native objects, packaging inputs, and logs live under
`build-android/`; Gradle output lives under `android/app/build/`. Neither directory
belongs in Git. Keep ROM-derived C and RSP sources ignored as on desktop.

## Validate a driver without descriptor indexing

Use a debug APK and the Android `arm64-v8a` binary of the Khronos
[validation layer](https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases).
Select **Use system GPU driver** first; Android only inserts debug layers into
its own Vulkan loader, so an imported Turnip driver bypasses them.

```sh
adb push libVkLayer_khronos_validation.so /data/local/tmp/
adb shell run-as io.github.alondero.lamborghinirecomp cp /data/local/tmp/libVkLayer_khronos_validation.so .
adb shell settings put global enable_gpu_debug_layers 1
adb shell settings put global gpu_debug_app io.github.alondero.lamborghinirecomp
adb shell settings put global gpu_debug_layers VK_LAYER_KHRONOS_validation
adb logcat -c
# Start the game from the launcher and let the attract race run.
adb logcat -d | grep -o 'Validation [A-Za-z]*: \[ [A-Za-z0-9_-]*' | sort | uniq -c
```

To check the settings overlay too, select **Menu** during the attract race
before reading the log.

Result on the SM-T720 system driver (Adreno 615, `0x801f6000`) with layer
1.3.290.0, Original2x resolution and MSAA2X, on 2026-10-26. Each run went from
launch to the attract race, then opened settings with the touch **Menu**
button. The baseline was the debug APK already installed, built before
patches 0043 to 0045:

| Error | Baseline | With 0043–0045 | Cause |
| --- | --- | --- | --- |
| `VUID-VkShaderModuleCreateInfo-pCode-08740` | 102 | 0 | Non-uniform indexing capabilities in RT64 shaders; fixed by 0044. |
| `VUID-VkShaderModuleCreateInfo-pCode-08742` | 51 | 0 | `SPV_EXT_descriptor_indexing` in the same shaders; fixed by 0044. |
| `VUID-vkCmdClearAttachments-pRects-00016` | 2 | 0 | A fill rectangle one pixel wider than the target: 321x241 on 320x241 and 642x482 on 640x482. Fixed by 0043. |
| `VUID-vkCmdDrawIndexed-renderPass-02684` | 2065 | 0 | The settings overlay used a BGRA pipeline on the RGBA swap chain. Fixed by 0045. |

The layer loaded in both runs. With the patches it reported no validation
messages. Screenshots from both runs show the textured race and the settings
overlay drawn correctly.
Turnip bypasses Android debug layers, so Turnip was not checked this way. A
clean validation run does not explain the device loss at automatic 6x; see the
[startup investigation](#adreno-615-startup-device-loss).

Any new error is a regression, especially one that names the texture
descriptor set. Remove the settings afterwards with
`adb shell settings delete global <name>` for each of the three names.

## Release signing (one-time repository setup)

The Build & Release workflow's Android job is **required**. All four signing
secrets must be set or the entire release is blocked — Windows and Linux
publication does not proceed without the APK, and the workflow never
substitutes a debug key. Android build, packaging, signature, and alignment
failures all fail the release. Configure the secrets **before** the initial release and never rotate the
release key (a different signing key prevents installing updates over the
previous release). Back up the keystore and passwords securely.

The four secrets match the [`aerogauge-recomp` Android signing setup](https://github.com/alondero/aerogauge-recomp/blob/main/docs/android.md)
one-for-one so the same machine-level signing identity model works across both
ports.

Configure with PowerShell on Windows (uses Windows DPAPI to encrypt the
password backup, tied to this Windows user and machine):

```powershell
./scripts/setup_android_signing.ps1
```

The script generates `lamborghini-release.jks` under
`%LOCALAPPDATA%\LamborghiniRecomp\Signing`, secures the directory with
`icacls`, encrypts a DPAPI backup of the keystore password alongside it,
generates the key with a CSPRNG-derived 48-byte base64 password, decodes it
into a PKCS12 keystore via JDK `keytool`, and configures all four GitHub
Actions repository secrets via `gh secret set`. It refuses to overwrite an
existing backup so a second run cannot silently destroy a working release
identity.

After it returns, retain the `.jks` file and the four secret values in a
separate offline backup. `credentials.clixml` cannot be moved to another
machine.

The workflow also uses the existing `ROM_ASSETS_REPO` variable and
`ROM_ASSETS_PAT` secret. Keystore material is written only to the runner's
temporary directory, removed after the build, and excluded from artifacts.

For a local signed build, set `ANDROID_KEYSTORE_PATH` to the keystore's
absolute path plus the three password/alias variables above, then run
`python scripts/build_android.py --release`. Never commit signing keys or
secrets.

APK `versionName` and `versionCode` are derived from the root CMake project version
(`major * 1000000 + minor * 1000 + patch`). Bump that version for each new release.
The package ID is `io.github.alondero.lamborghinirecomp`. Local debug and public
release APKs use different signing keys: Android will reject replacing one with
the other. Use a dedicated test device/profile or preserve your saves before
changing installation types.

The packaging script checks for required native libraries and UI fonts and rejects
ROM/save filenames. Release CI additionally verifies the APK signature and ZIP
alignment. Only the packaged APK is uploaded; build inputs and generated C are not.

## References

- [SDL2 Android integration](https://wiki.libsdl.org/SDL2/README-android)
- [Android native library page sizes](https://developer.android.com/guide/practices/page-sizes)
- [Android shader compilation](https://developer.android.com/ndk/guides/graphics/shader-compilers)

- [AdrenoTools integration](https://github.com/bylaws/libadrenotools)
- [Android process exit records and retained traces](https://developer.android.com/reference/android/app/ApplicationExitInfo)
- [Android document export](https://developer.android.com/training/data-storage/shared/documents-files)
