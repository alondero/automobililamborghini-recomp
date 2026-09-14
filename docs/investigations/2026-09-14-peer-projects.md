# Peer-project documentation study

Status: official repository and documentation inspection. No reference binary
was available in this worktree, so no project was launched or played.

Date: 2026-09-14.

## Sources

- [Banjo-Kazooie decompilation](https://github.com/n64decomp/banjo-kazooie)
  has a compact build README, ROM checksums, platform prerequisites, module
  targets, and an explicit progress indicator.
- [Banjo: Recompiled](https://github.com/BanjoRecomp/BanjoRecomp) separates
  player installation from building, documents ROM import, settings, saves,
  portable use, platforms, and mods.
- The official
  [Banjo: Recompiled mod template](https://github.com/BanjoRecomp/BKRecompModTemplate)
  documents a manifest and native function-patch build flow.
- [Shipwright](https://github.com/HarbourMasters/Shipwright) has a task-first
  player README and a separate
  [build guide](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/BUILDING.md).
  Its documentation also has a dedicated
  [modding guide](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/MODDING.md).
- [SpaghettiKart](https://github.com/HarbourMasters/SpaghettiKart) has a
  documentation map, a build guide, and a candid
  [modding guide](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/modding.md).
  Its [mods.toml reference](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/mods-toml.md)
  makes metadata and dependency order explicit.

## Lessons adopted here

1. Put the shortest player path in the root README.
2. Keep build prerequisites and build commands in a separate guide.
3. Give developers a map, a current-state page, and a worked first-change
   workflow.
4. Treat mod support as a contract with package metadata and clear limits, not
   as a vague “mod-friendly” claim.
5. Show progress and uncertainty instead of hiding incomplete systems behind
   polished wording.
6. Keep ROM version/hash checks visible to developers without making players
   learn internal memory terminology.

These projects are models for standards, not templates to copy blindly.

## Comparison by task

This table records what the official repositories and docs make clear. It is
not a report of playing any of the projects.

| Project | Player release and first run | Controls, settings, and saves | Platforms and build workflow | Modding and developer workflow |
| --- | --- | --- | --- | --- |
| Banjo-Kazooie decompilation | Source-first project. Its README gives supported ROM hashes and build targets, not a player release path. | The README is about source reconstruction. It does not define a finished player settings or mod-install contract. | Ubuntu and Docker paths are documented, including Linux and macOS container builds. Progress and module boundaries are visible. | Strong source ownership and progress signals. It is a decompilation reference, not a released port UX model. |
| Banjo: Recompiled | Releases are separate from building. The official README says to provide the North American 1.0 ROM in the main menu. | The in-game menu covers gameplay, graphics, input, and audio. The README names save locations and portable mode. | The repository describes Windows, Linux, macOS, Linux binaries, Flatpak, Steam Deck, and a build guide. | Mods can be installed by drag-and-drop or from the mod menu, then enabled and configured. The official mod template shows a native patch workflow. |
| Shipwright / Ship of Harkinian | Quick Start verifies the ROM, points to releases, and gives separate Windows, Linux, and macOS launch steps. | The README lists keyboard defaults, menu/shortcut keys, save states, fullscreen, and graphics backend selection. | The build guide has distinct Windows, Linux, macOS, Switch, and Wii U paths with dependencies and packaging steps. | The mod guide starts from a fork and branch, then teaches code changes. Player assets use OTR files in a mods directory. |
| SpaghettiKart | The repository docs are primarily a documentation and build map; no hands-on release check was available here. | The docs emphasize asset and track mod workflows more than a finished player settings contract. | Windows, Linux, and macOS build paths are documented, including asset generation and distributable packaging. | Mods may be archives or folders. A root mods.toml declares identity, version, dependencies, and load order. The docs state that modding is still early. |

The standard this project should meet is therefore concrete: a player path
with a ROM check and clear error recovery; settings, controls, and save
locations in one place; platform claims tied to actual release or build
evidence; a repeatable developer build; and a mod contract with versioning
before code mods are promised. This repository currently meets only part of
that standard, as the current-state and modding pages explain.

## Manual comparison checklist

The following observations remain unverified because no reference binaries were
available here. A future human review should fill in the build/version and
observed result fields rather than inferring them from documentation.

| Check | Build/version | Observed result |
| --- | --- | --- |
| Install a release on Windows | unverified | unverified |
| Install a release on Linux | unverified | unverified |
| Install a release on macOS, where documented | unverified | unverified |
| First-run ROM selection and error message | unverified | unverified |
| Default controls and remapping flow | unverified | unverified |
| Graphics, audio, and input settings | unverified | unverified |
| Save and configuration locations | unverified | unverified |
| Mod installation, enable/disable, and load order | unverified | unverified |
| Backend selection and failure recovery | unverified | unverified |
| Controller and keyboard behavior | unverified | unverified |
| Update and uninstall behavior | unverified | unverified |

Only the source repositories and their documentation were inspected for this
study. No sentence above should be read as a claim that the reference games
were played.
