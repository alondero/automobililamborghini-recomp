# Building

This guide is for developers. Players should use a release package from the
[Releases page](https://github.com/alondero/automobililamborghini-recomp/releases/latest).

The build has two stages:

1. translate the game code from a matching ROM;
2. compile the generated code and hand-written port.

The supported scripts perform both stages. Run them from the repository root.

## Requirements

All desktop builds need:

- Git with recursive submodule support;
- CMake 3.20 or newer;
- Python 3;
- Ninja;
- a legal copy of the North American USA ROM;
- a network connection for dependency configuration.

Linux needs GCC/G++, SDL2 development files, Vulkan headers and loader, and
the usual X11 desktop development files. The exact package list is in the
dependency check at the top of build.sh.

Windows needs MinGW-w64 GCC, Ninja, CMake, Git for Windows, and a native
PowerShell session. MSVC is not the supported compiler. The build script
adjusts its tool paths and applies the Windows RT64/Plume compatibility
patches.

The ROM is not included. The expected default filename is:

~~~text
Automobili Lamborghini (USA).z64
~~~

The known test ROM identity is recorded in
[the testing guide](docs/testing.md). Do not publish the ROM or place it in a
build artifact.

The Windows script has a RomPath parameter for its presence check and CI
plumbing, but the checked-in generator configuration still names the default
file above. Use the default filename for the supported build. Do not treat
RomPath as proof that an alternate filename is fully wired.

## Initialize the checkout

~~~bash
git clone --recurse-submodules https://github.com/alondero/automobililamborghini-recomp.git
cd automobililamborghini-recomp
git submodule update --init --recursive
~~~

On Windows, enable long paths for the deeply nested RT64 files:

~~~powershell
git -c core.longpaths=true submodule update --init --recursive
~~~

## Windows

Place the ROM at the repository root using the exact filename above, then run:

~~~powershell
.\build.ps1
~~~

For a clean rebuild:

~~~powershell
.\build.ps1 -Clean
~~~

The executable is build/lamborghini_modern.exe. Run it from the repository
root so the ROM path resolves:

~~~powershell
.\build\lamborghini_modern.exe
~~~

The script applies the Windows patch set, configures CMake twice, generates
RecompiledFuncs/ and src/aspMain.cpp, builds the executable, and runs the
Windows RDRAM allocation regression when that target is available.

## Linux

Place the ROM at the repository root, then run:

~~~bash
bash ./build.sh
~~~

For a clean rebuild:

~~~bash
bash ./build.sh --clean
~~~

The executable is build/lamborghini_modern:

~~~bash
./build/lamborghini_modern
~~~

The Linux script applies the Linux patch set, configures CMake twice, generates
the ROM-derived sources, and builds the executable.

## Android ARM64

Android uses the separate script and needs Python, Git, CMake 3.22 or newer,
Ninja, JDK 17, Android SDK platform 35, Android build tools 35, and NDK
28.2.13676358. The device target is API 26 or newer. The device needs Vulkan
1.1 plus the renderer features listed in the
[Android guide](docs/ANDROID.md).

With ANDROID_HOME configured:

~~~bash
python3 scripts/build_android.py --install
~~~

On Windows, PowerShell can pass a CMake path when the SDK installation does
not put it on PATH:

~~~powershell
python scripts/build_android.py --cmake 'path/to/cmake.exe' --install
~~~

The debug APK is written to
dist/lamborghini-recomp-android-arm64-debug.apk. The APK contains no ROM. The
launcher imports and validates the ROM on the device.

## What the scripts generate

The first CMake pass builds N64Recomp and RSPRecomp. Those tools read the ROM
and generate:

- RecompiledFuncs/ for the game functions;
- src/aspMain.cpp for the generated audio task code.

Both are ignored and must not be edited or committed. A second CMake configure
is required so the generated files become build inputs.

The checked-in TOML files, dump.toml, force_stub.txt, source hooks, tests, and
patches remain human-owned. See
[Architecture](docs/architecture.md) before changing a generation boundary.

## Dependency patches

The scripts apply the local patch series to pinned submodules. The complete
inventory and platform matrix are in [patches/README.md](patches/README.md).
Do not hand-edit a submodule and leave the change unrecorded. If a patch no
longer applies, stop and investigate pin drift.

## Tests

After a successful build:

~~~powershell
ctest --test-dir build --output-on-failure
python tools/run_game_scenario.py scenarios/harness-smoke.json
~~~

Use the Linux equivalents from
[docs/testing.md](docs/testing.md). The end-to-end scenario needs generated
output and the executable. Do not call a missing-ROM or missing-submodule
build failure a passing test.

## Troubleshooting a build

- If a submodule is empty, run the recursive submodule command again.
- If a patch fails, check for local dependency edits and the pinned commit.
- If generated files are missing, confirm the ROM path and inspect the first
  CMake pass for the recompiler tools.
- If the final build ignores generated code, run the second CMake configure.
- If Windows picks the wrong compiler or CMake, follow the script's tool-path
  diagnostics rather than adding a shell-specific export command.

For runtime failures, use [Debugging](docs/debugging.md). For test
prerequisites and skip conditions, use [Testing](docs/testing.md).
