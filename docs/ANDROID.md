# Android

The Android build is an ARM64 APK using SDL2, RT64/Vulkan, and the same
recompiled game/runtime as the desktop port. Minimum OS: Android 8 (API 26).
The GPU driver must support Vulkan 1.1, descriptor indexing, and scalar block
layout. A Vulkan version number alone does not guarantee compatibility. The APK contains no ROM. Import your own USA cartridge dump using
the launcher; `.z64`, `.v64`, and `.n64` byte orders are accepted and the complete
ROM is validated before replacing an existing import.

Touch controls provide steering, A/B/Z, Start, and Menu (the port settings). SDL gamepads and
keyboards use the desktop control mappings. Importing a ROM does not replace
your saves. Configuration, Controller Pak, and logs live in app-private storage;
uninstalling or clearing the app's data removes them. An APK update signed with
the same key retains this data.

## GPU drivers and device validation

On the tested Pixel 5 (Android 13), the system Adreno driver lacks renderer features.
The launcher can import an AdrenoTools-compatible Mesa Turnip ZIP on Android 9+
without root. Select **Import GPU driver ZIP**, choose a driver for your GPU, then
select **Play**. **Use system GPU driver** restores the default for subsequent game
launches. Driver files remain private to this app; they are not bundled in the APK.

The Pixel 5 reached the title, attract race, and player/car selection using
[Mesa Turnip v25.3.0-R11](https://github.com/K11MCH1/AdrenoToolsDrivers/releases/tag/v25.3.0-rc.11).
Other GPUs and driver versions have not been validated. Custom Adreno drivers are
specific to Qualcomm hardware; they are not a compatibility solution for Mali GPUs.

SDL and the renderer share an app-local Vulkan loader so they use the same driver
and surface. Native libraries must be extracted by Android (`useLegacyPackaging`)
for AdrenoTools hooks. NDK 28 supplies 16 KiB-aligned native libraries.

## Build locally

Requirements: Python 3, Git, CMake 3.22+, Ninja, JDK 17, Android SDK platform 35,
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

Output: `dist/lamborghini-recomp-android-arm64-debug.apk`. `--package-only`
repackages an already compiled native build. `--install` uses `adb install -r`;
connect one device and authorize USB debugging first. Open **Lamborghini
Recompiled**, import your ROM through Android's file picker, and select Play.
No broad storage permission is needed.

Generated dependencies, native objects, packaging inputs, and logs live under
`build-android/`; Gradle output lives under `android/app/build/`. Neither directory
belongs in Git. Keep ROM-derived C and RSP sources ignored as on desktop.

## Release signing (one-time repository setup)

The Build & Release workflow includes an Android job and attaches
`lamborghini-recomp-android-arm64.apk` alongside the Windows and Linux archives
when Android signing is configured. The Android job is optional for a desktop
release: if its signing secrets are absent, Windows and Linux publication still
proceeds and the release logs that the APK was omitted. Configure the secrets
below before publishing a release that must include the APK. A persistent
release key is required because a different key prevents installing updates over
the previous release. Back up the keystore and passwords securely.

Create a key locally with JDK `keytool` (it prompts for passwords):

```sh
keytool -genkeypair -v -keystore android-release.jks -alias lamborghini \
  -keyalg RSA -keysize 4096 -validity 10000
```

Set these GitHub Actions repository secrets:

| Secret | Value |
| --- | --- |
| `ANDROID_KEYSTORE_BASE64` | Base64 encoding of the complete `.jks` file |
| `ANDROID_KEYSTORE_PASSWORD` | Keystore password |
| `ANDROID_KEY_ALIAS` | `lamborghini` (or your existing alias) |
| `ANDROID_KEY_PASSWORD` | Private key password |

The workflow also uses the existing `ROM_ASSETS_REPO` variable and
`ROM_ASSETS_PAT` secret. If signing setup is missing, the optional Android job is
skipped after its configuration check and the desktop release proceeds without
an APK; it never substitutes a debug key. When signing is configured, Android
build, packaging, signature, and alignment failures fail the release. Keystore
material is written only to the runner's temporary directory, removed after the
build, and excluded from artifacts.

For a local signed build, set `ANDROID_KEYSTORE_PATH` to the keystore's absolute
path plus the three password/alias variables above, then run
`python scripts/build_android.py --release`. Never commit signing keys or secrets.

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
