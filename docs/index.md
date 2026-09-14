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
- [Upstream status](upstream-prs.md) records what has and has not been
  proposed to upstream projects.

## Existing evidence pages

These pages are retained research or subsystem notes. Their status line and
latest addenda matter more than their original filename.

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
| [No-LOD audit](no_lod_audit.md) | Historical LOD measurements and later addenda. |
| [Rumble audit](rumble-triggers.md) | Rumble discovery and Controller Pak coexistence evidence. |
| [Analog throttle](analog-throttle.md) | Throttle bridge measurements and tests. |
| [Analog brake](analog-brake.md) | Brake bridge measurements and tests. |
| [Player names](player-names.md) | Driver-name persistence notes. |
| [Android guide](ANDROID.md) | ARM64 build, launcher, signing, and device limits. |

## Evidence and decisions

- [Current state](reference/current-state.md) is the compact source-based
  support and divergence matrix.
- [Renderer boundary](reference/renderer.md) separates upstream RT64
  capabilities from port-specific changes.
- [Peer-project study](investigations/2026-09-14-peer-projects.md) records
  official repository and documentation comparisons. It separates observed
  documentation facts from unverified hands-on checks.
- [Repository history review](investigations/2026-09-14-repository-history.md)
  keeps useful lessons from representative build and research changes.
- [Controller-input evidence](investigations/2026-09-14-issue-96.md) records
  the status of the branch-only investigation without treating it as
  independently reproduced fact.
- [Architecture decisions](decisions/0001-human-ownership-and-evidence.md)
  record how this repository handles authority, uncertainty, guest-memory
  bridges, and renderer patches.
- [Documentation audit](DOCUMENTATION_REVIEW.md) is the original audit that
  motivated this structure. It is evidence and history, not a replacement for
  the current pages above.

## Status words

Every technical page should distinguish these states:

- **Confirmed**: supported by current source, a checked-in fixture, or a
  reproducible measurement.
- **Experimental**: implemented, but its contract or coverage is still narrow.
- **Historical**: useful evidence from an earlier branch, build, or experiment.
- **Inferred**: a reasonable explanation that still needs a direct measurement.
- **Unverified**: not checked in this worktree.

The project maintainer is the authority for support promises and architectural
choices. Notes from an AI session, an issue body, or a foreign project are
evidence to review, not project policy.
