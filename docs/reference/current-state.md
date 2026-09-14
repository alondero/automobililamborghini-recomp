# Current state reference

Status: confirmed from the current checkout, with platform and runtime
execution limits noted below.

Applies to: commit 72bd57a and the files in this worktree.

Last checked: 2026-09-14.

## Player support matrix

| Platform | Repository support | What was verified in this audit |
| --- | --- | --- |
| Windows x64 | Supported build/release path through MinGW and the Windows script | Script and source inspected. No executable was available to run. |
| Linux x64 | Supported build/release path through the Linux script | Script and source inspected. No executable was available to run. |
| Android ARM64 | Experimental build script and launcher path | Source and build instructions inspected. No APK or device run was available here. Existing Android documentation describes an earlier Pixel 5/Android 13 check; it was not repeated. |
| macOS | Not a documented supported release target | Some platform code and a Metal enum exist, but no current release or end-to-end validation was found. Treat as unverified. |

The graphics API enum is not a platform promise. In particular, the presence of
Metal in a shared settings type does not prove that this port has a supported
macOS window and build path.

## ROM and generated output

- The build accepts the North American USA release only.
- The local audit ROM is 4 MiB with SHA-256
  cab2467684a58bc19c787423d704a961aa497629763367d9fe691172de58591c.
- The build regenerates RecompiledFuncs/ and src/aspMain.cpp; both are ignored
  and absent from a source-only checkout.
- Symbols, splits, patches, ignored functions, and stubs are controlled by
  checked-in generation inputs.
- force_stub.txt proves that some functions are intentionally stubbed. It does
  not prove that the stubs match the original behavior.

## Current divergence from a normal decompilation workflow

| Area | Current reality | Risk |
| --- | --- | --- |
| Game code | Generated C with incomplete names and forced stubs | A generated boundary can hide a wrong symbol or missing behavior. |
| Port glue | Native hooks read and write guest memory and display-list commands | Fixed addresses and timing can break after regeneration or source changes. |
| Runtime | Public submodules plus a local patch series | Local behavior can diverge from upstream and needs patch-level review. |
| Renderer | RT64 plus project-specific projection, split-screen, backdrop, and driver patches | A local feature may be described as upstream behavior by mistake. |
| Modding | Texture packs and narrow stock-track visibility patches | There is no general code/data mod ABI. |
| Evidence | Source comments and research notes are mixed in older documents | Historical hypotheses can look like current guarantees. |

This is a static recompilation port, not a completed source decompilation.
The intended next architecture is source patches, stable symbols, explicit
hook contracts, and a versioned mod boundary. That direction needs human
decisions and implementation milestones.

## Saves and configuration

The port owns graphics.json, frontend profiles, player identity, logs, and the
Controller Pak file in a platform-specific user directory or portable
directory. See [Configuration](../configuration.md). The frontend imports
legacy controls.json but writes the framework profile file.

## What this audit did not verify

This worktree contained no built port, no ares binary, and no reference
project binaries. It therefore did not verify:

- a fresh desktop build;
- a game boot or race;
- an interactive graphics backend;
- a save round trip;
- an Android install or device run;
- playing Banjo: Recompiled, Shipwright, or SpaghettiKart.

Those gaps are deliberate in this reference page. They are not implied
successes.

## Repository records not treated as current behavior

The historical multi-wave roadmap is not the current roadmap. The open
[roadmap record](https://github.com/alondero/automobililamborghini-recomp/issues/26)
is retained as history.

The branch-only controller measurement is recorded in
[the dated input investigation](../investigations/2026-09-14-issue-96.md).
The repository requests for a patch index and upstream tracker are addressed
by [patches/README.md](../../patches/README.md) and
[upstream-prs.md](../upstream-prs.md). The proposed submodule-pin check is
outside this documentation-only pass.

The open [leaderboard documentation change](https://github.com/alondero/automobililamborghini-recomp/pull/171)
and [road-surface texture change](https://github.com/alondero/automobililamborghini-recomp/pull/195)
were not merged into this checkout and are not described as current features.
The merged [crash-stack build fix](https://github.com/alondero/automobililamborghini-recomp/pull/47)
does not define the frontend or player installation contract; those claims come
from the current source and README.
