# Renderer reference

Status: source and patch inventory checked on 2026-09-14. Upstream capability
claims still need comparison against a freshly initialized upstream checkout
before an upstream proposal.

## Boundary

RT64 supplies the general N64 renderer, texture resource handling, and the
standard F3DEX/extended-GBI path. This repository applies a local patch series
and emits game-specific commands or policy from src/.

| Local change | Current classification | Replacement or upstream path |
| --- | --- | --- |
| Texture hashing, dumps, loose packs, and .rtz loading | RT64 capability, with port configuration wiring | Keep generic resource behavior in RT64; keep Lamborghini paths in the port. |
| src/stub_renderer.cpp software renderer | Project-owned headless diagnostic/fallback path | Not an RT64 capability. Keep its tests and captures separate from interactive RT64 claims. |
| Frame interpolation transform matching | Local RT64 patch 0006 | Compare against current RT64 interpolation behavior and propose only a reusable fix. |
| Skybox/backdrop handling | Local RT64 patches 0008 and 0011, plus port policy | A generic backdrop contract could be upstreamed; the game's choice belongs in the port. |
| Widescreen split-screen origin | Local RT64 patch 0009 and Lamborghini HUD code | Propose a generic viewport-origin API only if another game needs the same contract. |
| Intel automatic backend selection | Local RT64 patch 0010 | Treat as a driver policy experiment; upstream only with a reproducible device/driver case. |
| MinGW compiler compatibility | Local RT64 patch 0005 and Plume patch 0004 | Platform portability fixes are candidates for upstream review. |
| Android cross-build and SDL window/input changes | Local patches 0013 through 0015 | Keep build integration local until upstream dependency versions and tests are aligned. |
| Runtime presentation and frontend configuration | Local runtime/frontend patches 0016 through 0018 | These cross project boundaries; maintainers must decide whether to generalize them. |

The table does not claim any local patch has an upstream pull request. The
status of proposals is recorded separately in docs/upstream-prs.md.

## Upstream capability check

The official [N64Recomp documentation](https://github.com/N64Recomp/N64Recomp)
describes TOML metadata, function stubs, single-instruction patches, and
single-file output for patched functions. Its current README describes TOML
hook emission as planned rather than as a generally available feature. This
matters here because the port's checked-in generation inputs use hook and
stub machinery around a pinned dependency. The pinned checkout must be
initialized and compared before a maintainer calls any of that behavior
upstream-supported.

The same upstream documentation describes RSP microcode recompilation but not
RSP overlays. That distinction should stay visible when deciding whether an
audio or renderer workaround belongs in generation inputs, port glue, or an
upstream patch.

## What belongs in the port

- game-specific FOV and view-cone matching;
- Lamborghini's split-screen policy;
- the decision to show or hide a stock sky panorama;
- texture-pack paths and player-facing defaults;
- a game-specific display-list hook with a documented guest invariant.

## What should be challenged before adding locally

- a renderer feature that changes generic F3DEX behavior;
- another backend workaround without a reproducible device/driver report;
- a new extended-GBI command whose contract is not documented;
- a copy of an upstream renderer implementation;
- a direct guest-memory write that could be replaced by a named generated
  function or runtime API.

The build must continue to use a pinned public RT64 checkout plus a reviewable
patch series. A private renderer fork is not the intended architecture.
