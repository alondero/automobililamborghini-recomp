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

With the Khronos validation layer on the system driver, the only texture errors
are that RT64's shaders declare non-uniform texture indexing without the
matching extension. The driver accepts these shaders and renders them
correctly on the tested phone. Other drivers without descriptor indexing may
not. To check a device, see
[Validate a driver without descriptor indexing](#validate-a-driver-without-descriptor-indexing).

SDL and the renderer share an app-local Vulkan loader so they use the same driver
and surface. Native libraries must be extracted by Android (`useLegacyPackaging`)
for AdrenoTools hooks. NDK 28 supplies 16 KiB-aligned native libraries.

## Export diagnostics after a failed launch

Return to the launcher and select **Export diagnostics ZIP**. Choose where to
save it, then attach the ZIP to your bug report. If Android shows an app-not-
responding dialog, close the game and reopen the launcher first. Export before
pressing **Play** again: the next game launch replaces the native logs. Do not
clear app data or reinstall before exporting.

The export works in release APKs without root or `run-as`. It includes:

- Native output (`native.log` and `native-errors.log`).
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
the [investigation below](#adreno-descriptor-pool-investigation). The Turnip
ANR remains a separate, unresolved failure. The shared
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
The remaining validation errors are tracked in
[issue 350](https://github.com/alondero/automobililamborghini-recomp/issues/350).
Repeating the full Android source-preparation script on an already composed
RT64 checkout can reject its own patch state; this was reproduced without
0042 and is tracked in
[issue 351](https://github.com/alondero/automobililamborghini-recomp/issues/351).
The candidate used the successful initial clean build followed by incremental
CMake compilation and packaging.

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

On the tested phone, the errors are `VUID-VkShaderModuleCreateInfo-pCode-08740`
and `-08742` for the shaders' non-uniform indexing. The
`VUID-vkCmdClearAttachments-pRects-00016` error (a clear one pixel wider than
the render area) and storage-image format warnings come from other RT64
passes, not the texture table. Any error that names the texture descriptor set
is a regression. Remove the settings afterwards with
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
