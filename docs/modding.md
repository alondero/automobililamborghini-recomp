# Modding

Status: experimental. The port uses N64ModernRuntime packages and the shared
RecompFrontend mod manager, following Banjo and Donkey Kong Recompiled.
See the [source comparison](mod-support-research.md).

## Install and manage mods

1. In Settings > Driver, enable **Show launcher at startup**, then restart.
2. Choose **Mods** in the launcher, or open Settings > Mods.
3. Use **Install Mods**, or drag a package onto the game window before Play.
4. Enable the mods you want, adjust their order or configuration, then close
   Settings and choose Play.

The Mods tab is also available during play. Code mods require a restart before
they can be installed or toggled. Texture-only packs can be toggled and ordered
while playing. If a code mod fails to load, an error appears over the Mods tab;
disable the incompatible mod and return to the launcher to retry Play.

Packages live in the `mods/` directory under the
[configuration directory](configuration.md#where-files-live). The Mods tab has
an Open Mods Folder button. `mods.json` stores selection and order;
`mod_config/` stores individual mod settings. Portable mode uses the same
layout beside the executable.

Supported package types:

- `.nrm`: packages built for this port with game ID `lamborghini`;
- `.rtz`: RT64 texture packs, with `rt64.json` at the archive root;
- loose mod directories with the runtime's `mod.json` manifest.

Banjo and Donkey Kong code mods are game-specific and cannot run in this port.
Required dependencies and minimum port versions are checked by the runtime.
Earlier texture packs in the Mods list take priority over later ones. An explicit
Graphics texture-pack path or `LAMBO_TEXTURE_PACK` overrides managed packs.
See [Texture packs](TEXTURES.md) for artwork formats and texture dumping.

Android's native file picker is not connected to this tab. Installing through
the tab and desktop drag-and-drop are desktop workflows; Android mod loading
and ARM64 code patching still need device verification.

## Code mod contract

Use the pinned N64ModernRuntime/N64Recomp toolchain and `RecompModTool` to package
MIPS code. Declare `game_id = "lamborghini"` and
`minimum_recomp_version = "0.5.0"` in the authoring manifest. Use this checkout's
`lamborghini.syms.toml` for function references. These symbols describe the USA
ROM only. They are an experimental interface, not a stable gameplay API.

Stock functions can be hooked or replaced. Functions with port-injected hooks,
instruction patches, or stubs are marked unhookable: rebuilding them from the
original ROM would remove port fixes. Ordinary replacements of those functions
report a base-recomp conflict. The runtime's explicit force-patch mechanism
bypasses replacement conflicts; authors then own the removed port behavior.
There is no general named game-data API, game-specific event API, or guarantee
that arbitrary guest-memory edits remain compatible with future releases.

CMake derives protected function metadata from the checked-in symbol and
recompiler configuration files. Generated native functions reserve entry space
for runtime replacement jumps and retain distinct function identities.
This avoids adjacent tiny functions being overwritten by a replacement.

The port exports `int lambo_log_v1(const char* message)` through the base (`*`)
import namespace. It logs an info-level message and returns its byte count, or
-1 for an invalid KSEG0 pointer or a string without a NUL in the first 1024 bytes.
Guest byte order is handled by the export. Use `--verbose` to include info logs.

## Developer smoke package

[The smoke fixture](../tests/mods/smoke/smoke.c) is original test code. It hooks
the first game-thread entry and logs `[mod-smoke] boot hook executed` once. Building
the package needs MIPS-capable Clang, LLD, and the pinned `RecompModTool`; it does
not need a ROM. Running it requires the matching USA ROM.

From the repository root, create `build/mod-smoke`, then run:

~~~text
clang -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division -fno-pic -ffunction-sections -nostdinc -c tests/mods/smoke/smoke.c -o build/mod-smoke/smoke.o
ld.lld -nostdlib -T tests/mods/smoke/mod.ld --unresolved-symbols=ignore-all --emit-relocs -e 0 --no-nmagic --gc-sections build/mod-smoke/smoke.o -o build/mod-smoke/smoke.elf
cmake --build build --target RecompModTool
build/lib/N64ModernRuntime/librecomp/N64Recomp/RecompModTool tests/mods/smoke/mod.toml build/mod-smoke
~~~

Install the resulting `.nrm`, start the game with `--verbose`, and check the log
for the marker.
Disable it and restart to check that the marker disappears. Also test an unmet
minimum version and a hook targeting a protected function; neither should enter
gameplay. These checks exercise loading and rejection, not every hook target,
mod dependency combination, native library, or platform.

## Track Lab

Track Lab still applies guarded `.altrk` visibility corrections to stock
circuits through its existing workflow. It is separate from the package manager.
Mod loading does not provide arbitrary custom playable tracks, geometry,
collision, navigation, AI data, or another regional ROM.
See [Track Lab](TRACK_LAB.md).

## Verification recorded for this integration

Confirmed on Windows x64, MinGW GCC 15.2, with freshly generated game code and
the USA ROM identified in [Testing](testing.md): the build, 33 project CTests,
29 Python tests, documentation checks, and the headless race/replay scenario
passed. The scenario reached race state 8 and completed all 600 replay frames.
The native D3D12 Mods tab was opened with the smoke package and captured.
A rejected mod was disabled through the menu and Play successfully retried
without restarting the application.

`tools/check_mod_support.py` runs the package in isolated profiles. Enabled and
disabled runs produced one and zero log markers respectively, both reaching
gameplay. An excessive minimum version and a protected-function hook both
returned exit code 2 before a guest game thread started. The latter test uses
the same fixture with `BootIdleThread` changed to `BootLoadInitialAssets`.

~~~text
python tools/check_mod_support.py --exe build/lamborghini_modern.exe --package build/mod-smoke/lambo_mod_smoke.nrm --rom "Automobili Lamborghini (USA).z64"
~~~

Add `--protected-package PATH` to check the guarded-hook variant. The checker
requires a test executable without a `portable.txt` marker beside it. It writes
logs under `artifacts/mod-support` by default. Its temporary profiles contain
all runtime saves and imported ROM copies and are removed after each case.

Unverified: Linux and Android execution, native-library mods, arbitrary gameplay
hook targets, and visual texture replacement with real artwork. The boot smoke
proves hook execution; it does not certify every mod or ABI combination.
