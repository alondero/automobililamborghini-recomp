# Mod loader audit

Status: **Confirmed** source audit, checked 2026-10-26 against port commit
`aebda1d` and its pinned dependencies. Peer execution and JP feature behaviour
are **Unverified**. This is the gap analysis for
[A21 / issue 19](https://github.com/alondero/automobililamborghini-recomp/issues/19),
not a claim that its full API contract is complete.

## What already exists

The port already assigns the `lamborghini` mod game ID, registers runtime
configuration storage, registers generated base sections and protected patches,
and uses the shared Mods tab. It supports NRM packages, texture packs, dependency
and version checks, configuration schemas, and stock-function hooks/replacements.
See [startup](../src/main.cpp), [content registration](../src/lambo_mods.cpp),
[base registration](../src/register_overlays.cpp), and [Modding](modding.md).

The pinned runtime registers its own heap, configuration and hook exports during
startup. A mod can read its own enum/bool/number value with
`recomp_get_config_u32`; string and double getters also exist. The mod identity
is supplied by the runtime import shim. The shared manager renders the manifest's
configuration schema and persists changes separately from cartridge saves.
Thus a saved difficulty/weather **choice** does not require a new save format.
[Runtime startup](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/librecomp/src/recomp.cpp),
[configuration exports](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/librecomp/src/mod_config_api.cpp),
[configuration storage](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/librecomp/src/mods.cpp).

Native-library support is also present in that runtime: `native_libraries`
declares a library name and exports; the host loads the platform extension next
to the package, validates `recomp_api_version`, and resolves guest-ABI function
bridges. A native library is an implementation dependency of a package. Loading
one does not automatically replace a guest function or suppress its generation.
The package still supplies an import and the desired hook/replacement.
[Native loading](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/librecomp/src/mods.cpp),
[Banjo authoring manifest](https://github.com/BanjoRecomp/BKRecompModTemplate/blob/06bbc59c840ecdd0f2d094fdf5df50710c1475b4/mod.toml).

## Peer comparison and remaining gaps

Banjo was inspected at `dbd52154bc1e00e56867b1fac94a8f907052f41b`;
Zelda64Recomp at `b65c482ed672258eb684ab94a4e8c8aa662615ee`.

| Surface | Peer evidence | Lamborghini gap at audit baseline |
| --- | --- | --- |
| Package manager and per-mod configuration | Banjo uses the same runtime/frontend architecture. | Integrated; default and persisted guest reads need execution coverage beyond the original logging smoke. |
| Guest UI API | Banjo calls `recompui::register_ui_exports()`. | The Mods tab exists, but startup omits this registration; imports of those UI functions cannot resolve. |
| Generic data containers | Banjo implements and registers `recomputil_*` hashmaps, sets and slotmaps in its own source. | No equivalent exports. This is additional utility code, separate from named game data. |
| Named game-data symbols | Banjo's mod template supplies a matching `.datasyms.toml`. | The smoke manifest has an empty data-reference list; no public data-symbol file is supplied. |
| Object extensions | Banjo tracks marker/prop allocation, deletion and movement; Zelda exposes actor extensions. | No vehicle/object extension lifecycle or public handles. Copying a Zelda actor API would not establish a Lamborghini lifetime contract. |
| Init and gameplay events | Banjo registers generated `event_names` and emits `recomp_on_init` from a guest initialization patch. | No public base event table, race-start, per-frame or save events. The runtime event mechanism alone does not define game events. |
| Native modules | Banjo's manifest documents libraries alongside packages; the runtime implements loading. | Host/architecture ABI and package delivery need actual fixture coverage; the original smoke exercises only MIPS code. |

Sources: [Banjo startup](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/src/main/main.cpp),
[data containers](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/src/game/recomp_data_api.cpp),
[object storage](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/src/game/recomp_extension_api.cpp),
[marker lifecycle](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/patches/marker_extension_patches.c),
[prop lifecycle](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/patches/prop_extension_patches.c),
[init event](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/patches/init_patches.c),
[event registration](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/src/main/register_patches.cpp),
[Zelda actor API](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_data.c),
[authoring symbols](https://github.com/BanjoRecomp/BKRecompModTemplate/blob/06bbc59c840ecdd0f2d094fdf5df50710c1475b4/mod.toml).

`GameEntry::on_init_callback` is not an equivalent mod-init event. In the pinned
runtime it runs **before** mod loading and heap initialization. Any future init
event needs an established guest stack and a phase after successful mod loading;
the current smoke hooks `BootIdleThread` for that reason.
[Runtime ordering](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/librecomp/src/recomp.cpp),
[smoke source](../tests/mods/smoke/smoke.c).

For a Super Mario 64 comparison, sm64coopdx is a PC source-port lineage with a
Lua mod API, rather than the same N64ModernRuntime package ABI. Its explicit
update, level-init, object-load and object-unload hooks illustrate the value of
documented lifecycle events. Its Lua API is not a drop-in loader implementation
for this port.
[Project description](https://github.com/coop-deluxe/sm64coopdx/blob/8cd6e5977d9f920d51ca71f2c61801d019ed79c6/README.md),
[hook contract](https://github.com/coop-deluxe/sm64coopdx/blob/8cd6e5977d9f920d51ca71f2c61801d019ed79c6/docs/lua/guides/hooks.md).

## What blocks the JP mods

[Issue 339](https://github.com/alondero/automobililamborghini-recomp/issues/339)
still needs the measured JP difficulty field, its width/values, the consuming US
read site, and reference-versus-port evidence. The package/config primitives
already exist. A per-mod choice can default to US behaviour and persist through
the existing manager. Its proposed native override must be expressed as a
runtime package import plus hook/replacement, rather than a base-build stub.

[Issue 340](https://github.com/alondero/automobililamborghini-recomp/issues/340)
still needs every circuit's JP weather assignment, modifier values and consuming
US sites. Its native reimplementation does not inherently need a working JP
overlay recompile. A race-start/per-frame **named event API** is missing;
ordinary stock-function hooks may suffice once the actual sites are identified.
The shared weather key and hook ownership with issues 72/73 also need agreement.

For both issues, hookability is unresolved until the US targets are measured.
The port deliberately marks injected, instruction-patched and stubbed functions
unhookable because runtime regeneration reads the original ROM and would remove
port fixes. A forced replacement transfers responsibility for those fixes to
the mod. A generic loader cannot promise that an unknown AI site is hookable.
[Guard generation](../scripts/gen_mod_patch_guards.py),
[guard registration](../src/register_overlays.cpp),
[code mod contract](modding.md#code-mod-contract).

## Changes verified after the audit

Startup now registers the frontend guest UI exports, including in headless
runs. The rebuilt smoke package imports and calls `recompui_context_root` and
reads its own `mode` enum. Against the previous executable, loading failed with
`Imported function not found:*:recompui_context_root` before guest threads ran.
Against the patched executable, the enabled, disabled, minimum-version,
protected-hook, texture-discovery, default-config and saved-override cases passed.
The override case reads an existing mod-config file; it does not exercise saving
from the rendered UI.

These runs used Windows x64, MinGW GCC 15.2 and freshly generated USA game code.
The headless race scenario reached state 8 and consumed all 600 replay frames.
All 75 project CTests passed. The Python suite ran 238 tests with 10 environment
skips and no failures; documentation and branch-whitespace checks also passed.
See [Modding](modding.md#developer-smoke-package) for reproduction commands.
Rendered custom UI, native-library execution and peer gameplay remain unverified.

**Inferred next steps:** exercise native imports and keep A21 open for documented
lifecycle events, data/object interfaces and their acceptance fixtures.
Reassess the JP issues' loader dependency against the measured hook sites; their missing JP
measurements remain independent blockers. The full Zelda/Banjo API inventory is
not a prerequisite for every individual Lamborghini mod.
