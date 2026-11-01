# Modding

Status: experimental. The port uses N64ModernRuntime packages and the shared
RecompFrontend mod manager, following Banjo and Donkey Kong Recompiled.
See the [source comparison](mod-support-research.md).
The [mod loader audit](mod-loader-audit.md) records A21 coverage and the JP mods'
game-specific prerequisites.

## Install and manage mods

1. In Settings > General, enable **Show launcher at startup**, then Apply and restart.
2. Choose **Mods** in the launcher, or open Settings > Mods.
3. Use **Install Mods**, or drag a package onto the game window before Play.
4. Enable the mods you want, adjust their order or configuration, then close
   Settings and choose Play.

The Mods tab is also available during play. Code mods require a restart before
they can be installed or toggled. Texture-only packs can be toggled and ordered
while playing. If a code mod fails to load, an error appears over the Mods tab;
disable the incompatible mod and return to the launcher to retry Play.

**Refresh** discovers newly added packages during play. New texture-only packs
start disabled; enable them and adjust their order without restarting. Existing
selections and order are kept. Code mods, packs with dependencies, custom game
modes, native libraries, and packages requiring a newer port version cannot be
enabled this way. Replacing or removing an already discovered package takes
effect after restarting. Before Play, Refresh rescans the whole mods folder.

Finish copying a package before pressing Refresh. A package discovered before
its copy finishes may need a restart. Refreshing a large mods folder can pause
the game for a moment.

Packages live in the `mods/` directory under the
[configuration directory](configuration.md#where-files-live). The Mods tab has
an Open Mods Folder button. `mods.json` stores selection and order;
`mod_config/` stores individual mod settings. Portable mode uses the same
layout beside the executable.

Supported package types:

- `.nrm`: packages built for this port with game ID `lamborghini`;
- `.rtz`: RT64 texture packs, with `rt64.json` at the archive root;
- loose mod directories with the runtime's `mod.json` manifest, including
  `"game_id": "lamborghini"` (not `automobili_lamborghini`). A bare texture
  directory without that manifest is only supported through the direct
  development override described below.

Banjo and Donkey Kong code mods are game-specific and cannot run in this port.
For manifest-bearing mods, required dependencies and minimum port versions are
checked by the runtime. A normal manifest-free `.rtz` instead gets generated
metadata: its filename stem becomes both ID and display name, author is
`Unknown`, version is `0.0.0`, and minimum port version is `0.0.0` (no meaningful
version gate). Keep its filename stable to preserve its identity and selection.

Do not rely on embedding `mod.json` in a texture pack made with the RT64 packer's
default zstd compression: the mod loader cannot extract zstd entries and treats
an unreadable manifest as absent. A readable stored/deflated `mod.json` **is**
parsed, including its identity, `game_id = "lamborghini"`, and minimum version;
it is not unconditionally ignored. These metadata rules belong to the Mods
route. The direct `texture_pack` override loads through RT64 without mod
identity or compatibility checks. See [Texture packs](TEXTURES.md#loading-a-pack).

Earlier texture packs in the Mods list take priority over later ones. The Mods
tab is the only place to install, enable, disable, and order texture packs.
See [Texture packs](TEXTURES.md) for artwork formats and texture dumping.

Android's native file picker is not connected to this tab. Installing through
the tab and desktop drag-and-drop are desktop workflows; Android mod loading
and ARM64 code patching still need device verification.

## Legacy texture pack setting

Earlier versions also let you type a texture pack directory or `.rtz` file into
Settings > Graphics. That control is gone. A pack configured that way still
loads, so nothing is lost by upgrading:

- `texture_pack` in `graphics.json` is still read and written back unchanged.
- It is loaded ahead of any pack enabled in the Mods tab, so it keeps priority.
- Startup logs `legacy texture_pack override active` when the value loaded, or
  `legacy texture_pack override FAILED to load` when the path did not load.

To move that pack into the Mods tab, install it through the tab, enable it, then
set `"texture_pack": ""` in `graphics.json` and remove `LAMBO_TEXTURE_PACK` from
your environment if you use it. An empty value disables the override.

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
Use the [versioned game interface](mod-api.md) for named data, events, utility
stores and racer extensions. Arbitrary guest-memory edits have no compatibility
guarantee.

The runtime supplies per-mod enum, bool, number and string configuration.
Declare options in `manifest.config_options`; the Mods tab renders and saves
them under `mod_config/`. Guest code imports `recomp_get_config_u32`,
`recomp_get_config_double` or `recomp_get_config_string` from `*` to read its
own settings. Enum values are zero-based option indices. A persisted difficulty
or weather choice needs no separate cartridge-save extension.

Startup also registers the frontend's `recompui_*` guest UI exports. They use
the pinned RecompFrontend API; the smoke checks import resolution and a symbolic
root ID, not a rendered custom mod menu.

The runtime's `native_libraries` manifest field can load a platform-specific
library beside the package. Its exports use the runtime guest-context ABI and
must declare a matching `recomp_api_version`. This is separate from a base-port
native override: importing a native function does not replace a stock function
until the package supplies the hook or replacement. The developer fixture below
exercises a native import and the runtime's native-library rejection paths.

CMake derives protected function metadata from the checked-in symbol and
recompiler configuration files. Generated native functions reserve entry space
for runtime replacement jumps and retain distinct function identities.
This avoids adjacent tiny functions being overwritten by a replacement.

The port exports `int lambo_log_v1(const char* message)` through the base (`*`)
import namespace. It logs an info-level message and returns its byte count, or
-1 for an invalid KSEG0 pointer or a string without a NUL in the first 1024 bytes.
Guest byte order is handled by the export. Use `--verbose` to include info logs.

## Developer smoke package

[The smoke fixture](../tests/mods/smoke/smoke.c) is original test code. It subscribes
to main game-thread initialization and logs `[mod-smoke] init callback executed`
once. It also hooks `BootIdleThread` and checks UI/config, native imports, data
symbols, utility stores and racer extensions. Building needs MIPS-capable Clang,
LLD (or Zig's bundled
tools), and the pinned `RecompModTool`; it does not need a ROM. Running it
requires the matching USA ROM.

From the repository root, run:

~~~text
cmake --build build --target RecompModTool lambo_mod_native_smoke lambo_mod_native_bad_api
python scripts/build_mod_smoke.py
~~~

Pass `--zig PATH` to use Zig instead of Clang/LLD. The script also builds the
protected-hook variant under `build/mod-smoke/protected/`. Keep the native
`lambo_mod_native_smoke.dll` (Linux: `.so`) beside the NRM when installing it.
The library must match the host architecture and use the runtime guest-context
ABI.

Install the resulting `.nrm`, start the game with `--verbose`, and check the log
for the marker.
Disable it and restart to check that the marker disappears. Also test an unmet
minimum version and a hook targeting a protected function; neither should enter
gameplay. These checks cover loading and rejection. Arbitrary gameplay hooks,
dependency combinations and other platforms need their own tests.

## Track Lab

Track Lab still applies guarded `.altrk` visibility corrections to stock
circuits through its existing workflow. It is separate from the package manager.
Mod loading does not provide arbitrary custom playable tracks, geometry,
collision, navigation, AI data, or another regional ROM.
See [Track Lab](TRACK_LAB.md).

## Verification recorded for this integration

The counts and rendered-menu check in the next paragraph record the earlier
package-manager integration. Current A21 execution coverage is in
[the loader audit](mod-loader-audit.md#a21-implementation-coverage).

Confirmed on Windows x64, MinGW GCC 15.2, with freshly generated game code and
the USA ROM identified in [Testing](testing.md): the build, 33 project CTests,
29 Python tests, documentation checks, and the headless race/replay scenario
passed. The scenario reached race state 8 and completed all 600 replay frames.
The native D3D12 Mods tab was opened with the smoke package and captured.
A rejected mod was disabled through the menu and Play successfully retried
without restarting the application.

`tools/check_mod_support.py` runs the package in isolated profiles. Enabled and
disabled runs produced one and zero log markers respectively, both starting
guest threads. An excessive minimum version and a protected-function hook both
returned exit code 2 before a guest game thread started. The protected variant targets
`BootLoadInitialAssets` with an ordinary hook.

~~~text
python tools/check_mod_support.py --exe build/lamborghini_modern.exe --package build/mod-smoke/lambo_mod_smoke.nrm --native-library build/mod-smoke/lambo_mod_native_smoke.dll --rom "Automobili Lamborghini (USA).z64"
~~~

Add `--check-api` with a freshly rebuilt smoke package to check the UI import,
the default enum value, and a saved `Override` choice in a second isolated
profile. The checker requires distinct default/override log markers, so an
ignored configuration file fails the check. This proves loading saved choices;
it does not exercise changing and saving a choice through the rendered UI.

Add `--protected-package PATH` to check the guarded-hook variant. The checker
requires a test executable without a `portable.txt` marker beside it. It writes
logs under `artifacts/mod-support` by default. Its temporary profiles contain
all runtime saves and imported ROM copies and are removed after each case.

Add `--texture-package PATH` for an RT64 `.rtz` with `rt64.json` at its root and
no `mod.json`. This optional case copies the archive unchanged as
`texture_smoke.rtz`, enables the filename-derived ID `texture_smoke`, and checks
for the runtime's exact `Loading mod texture_smoke` line, a started guest game
thread, exit code 0, and no `Skipping non-mod` or mod-load error. A successful
boot alone would miss an unregistered `.rtz` container. The case writes
`texture-pack.log`; the existing NRM checks still run first.

All checker runs force `LAMBO_HEADLESS=1` and clear inherited `LAMBO_*` overrides.
The texture case proves discovery and selection by the real mod loader, not
visible replacement artwork or RT64 rendering. Test those separately with a
renderer and a pack whose artwork you have permission to use. The ROM-free
checker tests run with `python -m unittest discover -s tests -p test_check_mod_support.py`.

Add `--check-lifecycle --bad-api-library build/mod-smoke/bad-api/lambo_mod_native_smoke.dll`
to require API/race/frame markers during a circuit-1 warp and check native
missing-library, missing-export and incompatible-ABI rejection before guest
thread creation. `--native-library PATH` overrides the default sibling.

Unverified: Linux and Android execution, arbitrary gameplay hook targets,
rendered custom mod UI, and visual texture replacement with real artwork.
The fixtures do not certify every mod or ABI combination.
