# Documentation map

This project has two audiences. Players need a short path to a working game.
Developers need evidence, ownership, and exact boundaries.

## Start here

| Task | Read |
| --- | --- |
| Install a release | [README](../README.md) |
| Build from source | [BUILDING.md](../BUILDING.md) |
| Report a player problem | [Bug report template](../.github/ISSUE_TEMPLATE/bug_report.md) |
| Add a feature proposal | [Feature request template](../.github/ISSUE_TEMPLATE/feature_request.md) |
| Record a ROM finding | [Reverse-engineering template](../.github/ISSUE_TEMPLATE/reverse_engineering.md) |
| Make a code change | [CONTRIBUTING.md](../CONTRIBUTING.md) |

## Player help

- [README](../README.md) explains supported platforms, installation, controls,
  settings, saves, and known limits.
- [Configuration](configuration.md) is the current player-facing file and
  environment reference. It is mainly for troubleshooting and advanced users;
  it does not promise a stable schema for developer-only variables.
- [Android](ANDROID.md) covers the ARM64 build and device-specific driver
  notes.
- [Texture packs](TEXTURES.md) describes the supported texture replacement
  format.
- [Track Lab](TRACK_LAB.md) describes the experimental stock-track visibility
  correction format. It is not a general custom-track system.

## Developer help

- [Architecture](architecture.md) defines runtime ownership, generated code,
  threads, guest memory, patches, and the intended direction.
- [Testing](testing.md) lists host, ROM-backed, and end-to-end checks with
  exact commands.
- [Debugging](debugging.md) explains logs, captures, the optional ares
  comparison workflow, and reproducible investigations.
- [Configuration reference](configuration.md) lists persistent files and
  environment overrides.
- [Modding](modding.md) states what texture and track support can do today and
  what proper code mod support would require.
- [Glossary](glossary.md) translates project terms into plain English.
- [Patch inventory](../patches/README.md) describes every local dependency
  patch.

## Existing evidence pages

These pages hold subsystem facts, measurements, and known limits. Read their
status line before relying on a historical measurement.

| Page | Use |
| --- | --- |
| [Frontend notes](recompfrontend.md) | Current settings, profiles, and migration limits. |
| [Automated harness](automation-harness.md) | Scenario format, replay timing, and oracle limits. |
| [Texture packs](TEXTURES.md) | Dump, decode, author, and package workflow. |
| [Track Lab](TRACK_LAB.md) | Experimental stock-track visibility corrections. |
| [Track research](TRACK_MODDING_RESEARCH.md) | Historical track data map and unresolved hypotheses. |
| [Track index](TRACK_INDEX.md) | Circuit, menu, and developer-warp numbering. |
| [Car differences](CAR_DIFFERENCES.md) | Vehicle identity and measurement notes. |
| [HUD notes](HUD.md) | Widescreen and HUD display-list evidence. |
| [Interpolation](interpolation.md) | Frame interpolation behavior and regression evidence. |
| [Sky panorama](sky-panorama.md) | Sky motion, projection coverage and task-owned panorama extensions. |
| [No-LOD audit](no_lod_audit.md) | Historical LOD measurements and later addenda. |
| [Rumble audit](rumble-triggers.md) | Rumble discovery and Controller Pak coexistence evidence. |
| [Analog throttle](analog-throttle.md) | Throttle bridge measurements and tests. |
| [Analog brake](analog-brake.md) | Brake bridge measurements and tests. |
| [Controllers and input](controllers.md) | Current input path and guest-layout evidence. |
| [Player names](player-names.md) | Driver-name persistence notes. |
| [Android guide](ANDROID.md) | ARM64 build, launcher, signing, and device limits. |

## Reference pages

- [Peer-project comparison](peer-projects.md) records durable documentation
  patterns from official reference projects and marks hands-on claims that have
  not been checked.
- [Patch inventory](../patches/README.md) records local dependency and renderer
  boundaries. Upstream proposals belong in the relevant upstream project's
  issue or pull request.

## Status words

Every technical page should distinguish these states:

- **Confirmed**: supported by current source, a checked-in fixture, or a
  reproducible measurement.
- **Experimental**: implemented, but its contract or coverage is still narrow.
- **Historical**: useful evidence from an earlier branch, build, or experiment.
- **Inferred**: a reasonable explanation that still needs a direct measurement.
- **Unverified**: not checked in this worktree.

The source, tests, and reviewed pull request are the project record. AI tools
may help draft and analyze changes, but an issue or external project page is
not a substitute for checking the current repository.
