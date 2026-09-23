# Peer recompilation mod support

Status: **Confirmed** source comparison, checked 2026-09-23. Runtime behaviour
in the peer games is **Unverified** here. This page records implementation
evidence and recommendations, not a promise of Lamborghini compatibility.

## Shared architecture

Banjo and Donkey Kong 64 both use N64ModernRuntime for mod loading and
RecompFrontend for the mod manager. Each game registers a `GameEntry` with a
nonempty `mod_game_id` (`bk` and `dk64` respectively), registers its configuration
directory and supported game, and supplies that same mod ID to the launcher.
Both register `rt64.json` as texture content with enable, disable and reorder
callbacks, allow runtime texture toggles, and register the `rtz` container without
a required manifest. Their renderer callbacks delegate texture management to
RecompFrontend. [Banjo startup](https://github.com/BanjoRecomp/BanjoRecomp/blob/ec859632cfa584e2272d32e46051543429a2baf4/src/main/main.cpp),
[DK64 startup](https://github.com/Rainchus/Donkey-Kong-64-Recompiled/blob/f52e505db8e6752cd91e8a895ac4f22cf9531d01/src/main/main.cpp).

## Recomp menu integration

Both call `recompui::config::create_mods_tab()` before `config::finalize()`.
This is the existing library manager, rather than a game-specific list of files.
[Banjo configuration](https://github.com/BanjoRecomp/BanjoRecomp/blob/ec859632cfa584e2272d32e46051543429a2baf4/src/game/config.cpp),
[DK64 configuration](https://github.com/Rainchus/Donkey-Kong-64-Recompiled/blob/f52e505db8e6752cd91e8a895ac4f22cf9531d01/src/game/config.cpp).

The frontend tab creates `ModMenu` and refreshes its list. The component handles
installation, opening the mods folder, refresh, selection, toggles, dragging for
order, and per-mod configuration. Launcher `add_mods_option()` selects
`config::mods::id` after setting the active game mod ID; this provides an optional
direct route into the same tab.
[Tab implementation](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompui/src/config/ui_config_tab_mods.cpp),
[manager interface](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompui/src/composites/ui_mod_menu.h),
[launcher](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompui/src/base/ui_launcher.cpp).

## Code mod prerequisites

Both peers register generated base overlay section tables. Their patch
registration also supplies generated patch code, `export_table`, `event_names`
and `manual_patch_symbols`. These make game code and explicit port APIs available
to the runtime. A visible Mods tab alone does not establish these contracts.
[Banjo overlays](https://github.com/BanjoRecomp/BanjoRecomp/blob/ec859632cfa584e2272d32e46051543429a2baf4/src/main/register_overlays.cpp),
[Banjo patches](https://github.com/BanjoRecomp/BanjoRecomp/blob/ec859632cfa584e2272d32e46051543429a2baf4/src/main/register_patches.cpp),
[DK64 patches](https://github.com/Rainchus/Donkey-Kong-64-Recompiled/blob/f52e505db8e6752cd91e8a895ac4f22cf9531d01/src/main/register_patches.cpp).

Both disable MSVC identical code folding with `/OPT:NOICF`, explicitly because
merging function bodies breaks mod function patching. Their macOS builds use a
linker wrapper to support writable executable memory. These are host build
requirements, separate from the authoring toolchain.
[Banjo CMake](https://github.com/BanjoRecomp/BanjoRecomp/blob/ec859632cfa584e2272d32e46051543429a2baf4/CMakeLists.txt),
[DK64 CMake](https://github.com/Rainchus/Donkey-Kong-64-Recompiled/blob/f52e505db8e6752cd91e8a895ac4f22cf9531d01/CMakeLists.txt).

## Packages and authoring

The official Banjo template compiles MIPS code with Clang and `ld.lld`, using
`make`, then runs `RecompModTool mod.toml build` to produce an `.nrm` package.
Its manifest declares identity, version, target `game_id`, minimum port version,
dependencies, optional dependencies, native libraries and configuration options.
Tool inputs include an ELF, matching function/data symbol files and optional
additional files. The template documents MIPS-capable LLVM as a requirement;
Apple's bundled Clang is insufficient. Its specific LLVM version warning is a
template compatibility note, not a recommendation to pin all future builds to
that release.
[Official template](https://github.com/BanjoRecomp/BKRecompModTemplate/blob/master/README.md),
[manifest](https://github.com/BanjoRecomp/BKRecompModTemplate/blob/master/mod.toml),
[build flags](https://github.com/BanjoRecomp/BKRecompModTemplate/blob/master/Makefile).

## Application to Lamborghini

**Inferred implementation direction:** reuse the existing frontend Mods tab and
runtime package loader, assign a stable Lamborghini mod game ID, preserve a
single configuration/mod directory, and register texture callbacks through the
renderer already used by this port. Do not invent another package format or
manager when these dependencies provide both.

Before claiming code mod compatibility, check generated function metadata and
patchability, reference symbol availability, guest mod-memory reservation, host
linker settings, and the interaction with this port's replacement functions.
Validate with an actual package that changes game behaviour; installation or
listing alone is insufficient. Keep experimental Track Lab packages documented
separately until their content loader explicitly participates in the mod API.
These are integration recommendations drawn from the prerequisites above, not
claims that either peer provides Lamborghini-specific adapters.

## Failed-load frontend lifecycle

The pinned frontend and upstream main checked on 2026-09-23 only disable
Install/Refresh and the selected code-mod toggle when gameplay starts. They do
not restore those controls on a later failed-load transition back to the
launcher. The local 0017 patch assigns both enabled and disabled states and
notifies the UI on both transitions. The native Windows failure/disable/retry
workflow was exercised locally. This is a downstream fix, not an upstream
support claim. The canonical proposal is
[RecompFrontend issue 43](https://github.com/N64Recomp/RecompFrontend/issues/43).
[Upstream mod-menu state handling](https://github.com/N64Recomp/RecompFrontend/blob/main/recompui/src/composites/ui_mod_menu.cpp).
