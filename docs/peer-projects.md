# Reference project comparison

This page records useful documentation and workflow patterns from established
N64 projects. The repository sources below are official. Documentation findings
are sourced; hands-on rows remain unverified until someone runs the projects.

## Sources and durable lessons

- [Banjo-Kazooie decompilation](https://github.com/n64decomp/banjo-kazooie)
  has a compact build README, ROM checksums, platform prerequisites, module
  targets, and visible progress.
- [Banjo: Recompiled](https://github.com/BanjoRecomp/BanjoRecomp) separates
  player installation from building and documents ROM import, settings, saves,
  portable use, platforms, and mods. Its
  [mod template](https://github.com/BanjoRecomp/BKRecompModTemplate) shows a
  manifest and native function-patch workflow.
- [Shipwright](https://github.com/HarbourMasters/Shipwright) has a task-first
  player README, a separate
  [build guide](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/BUILDING.md),
  and a dedicated
  [modding guide](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/MODDING.md).
- [SpaghettiKart](https://github.com/HarbourMasters/SpaghettiKart) has a
  documentation map, build guide, and candid
  [modding guide](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/modding.md).
  Its [mods.toml reference](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/mods-toml.md)
  makes metadata, dependencies, and load order explicit.

The standards adopted here are concrete: a short player path, settings and
save locations in one place, platform claims tied to evidence, a repeatable
developer build, and clear limits before code mods are promised.

## Comparison by task

| Project | Player path | Settings and saves | Platforms and build | Modding and developer workflow |
| --- | --- | --- | --- | --- |
| Banjo-Kazooie decompilation | Source-first README with supported ROM hashes, not a player release path. | Source reconstruction, not a finished player settings contract. | Ubuntu and Docker paths, with visible progress and module boundaries. | Strong source ownership and progress signals. |
| Banjo: Recompiled | Releases are separate from building; the README describes North American 1.0 ROM import. | In-game gameplay, graphics, input, and audio settings; save and portable locations are documented. | Windows, Linux, macOS, Linux binaries, Flatpak, Steam Deck, and a build guide. | Drag-and-drop or menu-based mods; native patch workflow in the official template. |
| Shipwright / Ship of Harkinian | Quick Start verifies the ROM and gives platform launch steps. | Keyboard defaults, menu shortcuts, save states, fullscreen, and backend selection are documented. | Windows, Linux, macOS, Switch, and Wii U build paths. | Fork-and-branch code workflow plus OTR asset mods. |
| SpaghettiKart | Documentation is primarily a build and modding map; release UX is not verified here. | Asset and track workflows are clearer than a finished player settings contract. | Windows, Linux, and macOS build and packaging paths. | Folder/archive mods with identity, version, dependencies, and load order in `mods.toml`. |

## Manual checks still unverified

The following observations require a real run. Do not infer them from
documentation alone:

| Check | Status |
| --- | --- |
| Install a release on Windows, Linux, or macOS | Unverified |
| First-run ROM selection and error recovery | Unverified |
| Default controls and remapping flow | Unverified |
| Graphics, audio, input settings, and save locations | Unverified |
| Mod installation, enable/disable, and load order | Unverified |
| Backend selection and failure recovery | Unverified |
| Update and uninstall behavior | Unverified |

When these checks are run, update the result here with the project version and
the observed behavior. Do not claim that a reference game was played without
that check.
