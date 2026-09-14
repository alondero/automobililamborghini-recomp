# Documentation review

Reviewed 2026-09-14 against repository commit `72bd57a` (`origin/main`). The review covers the root documentation, all Markdown under `docs/`, source comments and headers, build/test scripts, GitHub issue and pull-request workflow, and the public documentation practices of [Banjo-Kazooie decompilation](https://github.com/n64decomp/banjo-kazooie), [Shipwright / Ship of Harkinian](https://github.com/HarbourMasters/Shipwright), and [SpaghettiKart](https://github.com/HarbourMasters/SpaghettiKart).

This is a recommendations document, not a claim that every proposed page must be created immediately. Priorities are:

- **P0**: a new developer can be blocked, misled, or sent to the wrong source of truth.
- **P1**: important knowledge is difficult to discover or likely to be lost.
- **P2**: quality, consistency, and maintenance improvements after the main paths are fixed.

## Executive assessment

The project has strong raw documentation quality and weak documentation architecture.

The strongest material is unusually valuable for a recompilation project: it records measured addresses, guest-memory layouts, framebuffer hashes, exact probes, known uncertainty, commands, fixtures, and the distinction between ROM behavior and port behavior. [`HUD.md`](HUD.md), [`CAR_DIFFERENCES.md`](CAR_DIFFERENCES.md), [`TEXTURES.md`](TEXTURES.md), [`TRACK_LAB.md`](TRACK_LAB.md), [`automation-harness.md`](automation-harness.md), and the source headers are substantially better than generic feature notes.

The main problem is that this knowledge is organized as a growing collection of investigations rather than as a maintained knowledge base. There is no documentation landing page, architecture guide, authoritative configuration reference, testing guide, patch index, upstream-status tracker, or current roadmap. The root README links only a subset of the existing material. Several long documents mix current contracts, historical investigation logs, hypotheses, and completed work without a current-status summary. A new developer therefore has to infer both where to look and which statement is still true.

The recommended direction is incremental: create a small navigation and source-of-truth layer first, then split the largest audience mixtures, and finally add lightweight checks so the system does not regress. A documentation website or wholesale comment rewrite is not the first priority.

## Inventory and evidence

### Current surface

- The root has [`README.md`](../README.md), [`BUILDING.md`](../BUILDING.md), [`CONTRIBUTING.md`](../CONTRIBUTING.md), and [`CLAUDE.md`](../CLAUDE.md).
- There are 15 project Markdown files under `docs/`, but no `docs/README.md`, `docs/index.md`, or generated documentation entry point.
- The root README routes readers to Android, RecompFrontend, textures, car differences, the harness, and Track Lab. It does not route them to HUD, interpolation, player names, rumble, analog input, the track index, the no-LOD audit, or the track research.
- `build.sh`, `build.ps1`, `scripts/build_android.py`, CMake, and CI contain executable build knowledge, but the prose build guide duplicates much of that knowledge instead of treating the scripts as the canonical path.
- `.github/ISSUE_TEMPLATE` and `.github/PULL_REQUEST_TEMPLATE.md` are more project-specific than the usual templates. The bug template asks for ROM identity, build commit, dependency patches, ares/vanilla comparison, GPU, mods, save state, warp, and logs.
- There is no `CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`, architecture page, testing page, patch index, or upstream-patch tracker.

### Method and limits

This was a content and maintainability audit, not a formal line-by-line catalog of every historical tracker entry. I inventoried the root and `docs/` Markdown, sampled all source/header areas relevant to public contracts, scanned `src/`, `scripts/`, and `tools/`, and read the documentation-related issue bodies/comments and representative docs/build/research pull requests through GitHub. The local scan covered 101 source/tooling files and found 226 issue-number matches and 100 `W###` matches; those are heuristic reference counts, not counts of unique comments or unique investigations. GitHub currently exposes 111 issue records and 93 pull requests to the CLI; the report focuses on entries that affect documentation, build reproducibility, reverse-engineering evidence, or contributor workflow.

The peer comparison used only the projects' own repositories, official project documentation, generated documentation, and first-party issue/merge-request pages. The comparison is about documentation patterns and is not a claim that those projects are uniformly correct or that every linked page is current.

### Quality profile

| Area | Assessment | Why |
| --- | --- | --- |
| Empirical/reverse-engineering evidence | Strong | Measurements, addresses, confidence labels, probes, and retained fixtures are common. |
| User quick start | Medium | The README has legal, ROM, graphics, texture, warp, and Android information, but is long and incomplete as a user guide. |
| Build reproducibility | Medium | The scripts are good and CI delegates to them; the prose path is manual, duplicated, and has platform-specific ambiguity. |
| Contributor onboarding | Weak | Contributors are sent to an AI-oriented `CLAUDE.md`; there is no project map, architecture guide, first-change tutorial, or testing matrix. |
| Discoverability | Weak | No index; naming is inconsistent; many good documents are reachable only by knowing their filenames or finding a related issue/PR. |
| Currentness/status | Weak to medium | Later addenda and merged PRs often invalidate earlier prose, with no standard way to mark the active truth. |
| Source comments | Strong evidence, inconsistent audience | Comments explain difficult code well, but often mix contracts with session history and raw issue/wave identifiers. |
| Documentation automation | Weak | No link, case-sensitivity, freshness, config-parity, or documentation-build check is visible in CI. |

## What is already working

These practices should be preserved, not flattened into generic prose.

1. **Evidence is attached to behavior.** [`src/lambo_vehicle.h`](../src/lambo_vehicle.h) explicitly says its packed structure is a guest layout schema rather than a host view of RDRAM, leaves unknown fields unnamed, gives static layout assertions, and points to the evidence. That is exactly the right instinct for recompilation work.
2. **Configuration contracts are close to code.** [`src/lambo_config.h`](../src/lambo_config.h) documents paths, defaults, JSON keys, environment overrides, startup-only behavior, and per-circuit semantics. The issue is not lack of knowledge; it is that this contract has not been promoted into an easy-to-find user/developer reference.
3. **The better investigation documents are reproducible.** [`TEXTURES.md`](TEXTURES.md) gives a full dump/decode/author/package flow. [`TRACK_LAB.md`](TRACK_LAB.md) gives an experimental workflow and safety boundary. [`automation-harness.md`](automation-harness.md) gives scenarios, traces, pre-roll, and oracle limitations. [`CAR_DIFFERENCES.md`](CAR_DIFFERENCES.md) ties claims to retained evidence.
4. **The source often explains why.** The renderer, HUD, configuration, audio, no-LOD, input, and vehicle code contain useful rationale and measurements. The project should retain these local explanations while moving long historical narratives to durable evidence pages.
5. **The issue forms collect the right debugging context.** The project understands that a ROM hash, ares comparison, generated-source state, dependency patches, graphics backend, and visual artifact can change the diagnosis. That information model should be reused for documentation work.
6. **Build automation has a useful boundary.** CI comments say that `build.sh` and `build.ps1` are the build source of truth. That is a good design; the prose should align with it instead of presenting a competing manual recipe as the main route.

## Findings and recommendations

### D1 — P0: there is no documentation map

The repository has enough material that filename discovery is no longer adequate. A developer looking at the root cannot tell which document is authoritative, experimental, historical, user-facing, or intended only for reverse-engineering.

Create `docs/README.md` as a deliberately small router. It should have sections such as:

| Reader goal | Landing page | Existing material to link |
| --- | --- | --- |
| Install, run, and configure | Root README and a future `docs/CONFIGURATION.md` | `README.md`, `recompfrontend.md`, `ANDROID.md` |
| Build from source | `BUILDING.md` | `build.sh`, `build.ps1`, `ANDROID.md` |
| Make a first code change | Future `docs/DEVELOPMENT.md` | `CONTRIBUTING.md`, `CLAUDE.md`, `automation-harness.md` |
| Understand the port | Future `docs/ARCHITECTURE.md` | runtime, generated code, RT64, patches, frontend |
| Debug a mismatch | Future `docs/DEBUGGING.md` | `ares`, warp, logs, harness, `CAR_DIFFERENCES.md`, `HUD.md` |
| Create or test mods | Future `docs/MODDING.md` | `TEXTURES.md`, `TRACK_LAB.md`, `TRACK_INDEX.md`, track research |
| Read evidence | `docs/evidence/README.md` | no-LOD, HUD, car, rumble, input, track research |
| Release or package | Future `docs/RELEASING.md` | `ANDROID.md`, `.claude/skills/release/` |

Every entry should have a one-sentence description and a status label such as `stable`, `experimental`, `historical`, or `needs update`. Link the map from the root README and CONTRIBUTING.

### D2 — P0: the main build path is harder than the scripts

[`BUILDING.md`](../BUILDING.md) leads with dependency patching, manual recompilation, and hand-written CMake commands. This duplicates the logic in `build.sh`, `build.ps1`, and CI. It also presents a Bash-style `export PATH=...` command in the Windows/MinGW section, where many readers will be using PowerShell. The document does not give a simple test command or a recovery matrix.

Restructure it around one fast path per supported host:

1. prerequisites and ROM identity;
2. clone with submodules;
3. one copyable `./build.sh` or `./build.ps1 -RomPath ...` command;
4. run command and expected first-launch behavior;
5. `ctest --test-dir build --output-on-failure`;
6. platform-specific troubleshooting;
7. an advanced section explaining patching and generated sources only when the scripted path fails.

The manual patch list should be generated or checked against the scripts. At minimum, label Windows-only patches, show PowerShell and Bash forms separately, state whether a command is run from the repository root or a submodule, and explain how to recover from a partially patched checkout. Record the exact ROM filename/hash expected by the generator and the supported compiler/CMake/Python/Ninja versions.

Do not remove the technical explanation. Move it below the successful path and explicitly label it as “what the build script does”.

### D3 — P0: current truth is mixed with chronological investigation

The documents with the most valuable evidence are also the most likely to mislead when read linearly.

- [`no_lod_audit.md`](no_lod_audit.md) begins with a headline saying no confirmed visible race-scene mechanism remains, then later addenda identify additional scenery, PVS, radius, FOV, and circuit-specific behavior. The last addendum is the current truth, but the first page does not summarize it.
- [`rumble-triggers.md`](rumble-triggers.md) contains a stale statement that a faithfulness note “must be rewritten when #106 lands”, even though the relevant work has since landed. It also mixes a current implementation map with old issue rationale.
- [`analog-throttle.md`](analog-throttle.md) still calls a shipped feature a “preflight”.
- The track and frontend documents mix durable contracts with migration history, pin dates, and session-oriented conclusions.

For each such document, put a current-status card immediately below the title:

```text
Status: stable | experimental | historical | needs update
Applies to: commit/tag or “current main”
Last verified: YYYY-MM-DD
Source of truth: code/config/fixture/document
Known limitations: ...
Supersedes: ...
Related issue/PR: ...
```

Then choose one of two patterns:

- For small documents, keep the chronology below a short “Current behavior” section and mark superseded findings explicitly.
- For large documents such as the no-LOD and track research, create a concise current reference page and move dated addenda to `docs/evidence/history/`. The reference page should contain the behavior table, configuration keys, verification command/fixture, and open probes; the history page should preserve the reasoning.

The goal is not to discard research. It is to make the answer to “what is true now?” available in the first screen.

### D4 — P0: completed research is not guaranteed to land on the default branch

Issue [#96](https://github.com/alondero/automobililamborghini-recomp/issues/96) is still open, but its completion comment ([direct link](https://github.com/alondero/automobililamborghini-recomp/issues/96#issuecomment-4937435561)) links `docs/multiplayer-input.md` on the named branch `gh96-preflight-empirical-measurement-of-multi-cont`, not on the default branch. That file is absent from the current tree. This is a serious knowledge-lifecycle failure: the tracker says the report exists, but a fresh checkout cannot read it.

Change the acceptance rule for research issues:

- the document must be merged into the default branch;
- it must be linked from `docs/README.md` or the relevant subsystem page;
- the issue closure comment must link the default-branch file, not a worktree or temporary branch;
- the document must state what remains unverified;
- if the research produces a fixture, script, or generated artifact, the acceptance criteria must name its committed path.

Reconcile #96 explicitly: recover the report if it contains unique evidence, or update the issue to point to the durable replacement and explain what was lost. Do not leave the issue marked complete with only a branch link.

### D5 — P1: known patch knowledge is still trapped in issue context

Issues [#132](https://github.com/alondero/automobililamborghini-recomp/issues/132), [#133](https://github.com/alondero/automobililamborghini-recomp/issues/133), and [#134](https://github.com/alondero/automobililamborghini-recomp/issues/134) already describe the missing documentation precisely:

- `patches/README.md`: what each patch changes, why it is needed, what symptom it fixes, and whether it belongs upstream;
- `docs/upstream-prs.md`: upstream repository, PR URL, status, date, and notes;
- a contributor-facing explanation of the RT64 F3DEX v1 path and the decision not to maintain a private renderer fork.

These should be implemented as one small, linked knowledge area. The patch index should cover all current patches, not only the original runtime and renderer subset. A useful table is:

| Patch | Dependency | Purpose/symptom | Why a patch | Upstream status | Verification |
| --- | --- | --- | --- | --- | --- |
| `0006-...` | RT64 | ... | ... | Not filed / draft / open / merged / rejected | build or scenario |

Keep the table factual and short. Put investigation detail and rejected alternatives in linked issue comments or evidence pages. Update upstream status whenever a PR changes state.

### D6 — P1: normal contributor guidance is being delegated to `CLAUDE.md`

[`CONTRIBUTING.md`](../CONTRIBUTING.md) points contributors to [`CLAUDE.md`](../CLAUDE.md) for development conventions. `CLAUDE.md` is useful agent context, but it is dense, issue-reference-heavy, and written as an internal operating brief. It is not a substitute for a first-party contributor guide.

Add `docs/DEVELOPMENT.md` or promote a contributor-oriented section in `CONTRIBUTING.md` covering:

- repository map and subsystem boundaries;
- the generated-code/recompiled-function boundary;
- the runtime, librecomp, RT64, RecompFrontend, and patch relationships;
- ares as the behavioral reference and when to compare against it;
- build, CTest, smoke scenario, and visual verification commands;
- the smallest useful first change;
- where to put a source comment, evidence note, fixture, or issue;
- how to update docs when behavior/config/build instructions change.

Keep `CLAUDE.md` for agent-specific context, concise reminders, and links into the developer guide. This avoids making future human contributors learn the project through a file whose primary audience is an AI coding tool.

### D7 — P1: there is no architecture or system model

The project description in the README explains static recompilation, but a new developer still has to reconstruct the system from CMake, submodules, generated files, and comments. Add `docs/ARCHITECTURE.md` with one diagram and a small set of invariants:

```text
USA ROM + symbols
        |
        v
N64Recomp -> generated RecompiledFuncs/*.c
        |
        v
game port glue (src/) ----> N64ModernRuntime / librecomp
        |                               |
        +---- config/input/audio --------+
        |
        v
RT64 / F3DEX renderer -> desktop or Android frontend
```

The page should also explain:

- which files are generated and should not be edited;
- the ROM-address and RDRAM-endian conventions;
- where native hooks replace or wrap recompiled functions;
- why dependency patches exist and where they are applied;
- startup, game-state, render, input, and save-state boundaries;
- which behavior is expected to match the ROM and which behavior is intentionally enhanced.

This page is the missing bridge between the user README and the forensic subsystem documents.

### D8 — P1: configuration is documented in several incompatible surfaces

The README has a useful graphics table, and `src/lambo_config.h` is a strong code contract, but they do not yet form one discoverable reference. For example, source supports `no_lod_circuit`, while the README’s user-facing option table does not list it. Input/control behavior is described in [`recompfrontend.md`](recompfrontend.md) and the README’s three bullets, but there is no complete mapping/reference page.

Create `docs/CONFIGURATION.md` as the user-facing source of truth. Organize it into:

- configuration file locations and portable mode;
- every JSON key with type, default, valid range, restart/live-apply behavior, and example;
- every environment override and its precedence;
- launcher/frontend settings and profile paths;
- controller mapping and player assignment;
- texture, fog, LOD, camera, audio, and display options;
- a “safe starting configuration” and a troubleshooting table.

The README should retain a compact summary and link to this page. Ideally, the table should be generated from a central metadata structure or checked by a test so source keys, defaults, and documentation cannot silently diverge.

### D9 — P1: testing and debugging knowledge is scattered

The repository has many CTest targets and deterministic scenarios, but the main build guide does not tell a contributor how to run them. Individual feature documents each give their own test commands. The harness documentation explains its oracle limitations well, but the project lacks one test matrix.

Add `docs/TESTING.md` with:

| Layer | Command/artifact | Purpose | Required for |
| --- | --- | --- | --- |
| Unit tests | `ctest --test-dir build --output-on-failure` | deterministic host-side contracts | every code PR |
| Targeted test | `ctest ... -R <name>` | fast iteration on one subsystem | feature/fix PR |
| Build smoke | `build.sh` / `build.ps1` | generated source and link integrity | every build-affecting PR |
| Scenario smoke | `python tools/run_game_scenario.py scenarios/harness-smoke.json` | startup/input/replay path | runtime/input/config PR |
| Visual smoke | frontend/RT64 scenario plus screenshot or hash | renderer/UI behavior | visual PR |
| ares comparison | documented ROM run and save/state | separate ROM bug from port bug | behavioral mismatch or reverse engineering |

Use the committed names under `recordings/` and `scenarios/` in examples. The old illustrative `recordings/circuit-1-time-trial.jsonl` path is not present; the repository has named per-car fixtures and `recordings/harness-smoke.jsonl` instead.

Add `docs/DEBUGGING.md` separately so the test page stays a matrix rather than becoming another investigative essay. It should cover logs, developer warp, save states, generated C VRAM comments, ares watchpoints, graphics backend failures, and how to attach a useful bug report.

### D10 — P1: source comments need a clear taxonomy

A scan of the source and tooling found hundreds of issue and wave/session references alongside many useful comments about measurements and uncertainty. This is not inherently bad; provenance is important. The problem is that a future reader cannot reliably distinguish a current invariant from a historical breadcrumb.

Use four comment categories:

| Category | Keep in source? | Recommended form |
| --- | --- | --- |
| Contract/invariant | Yes | Explain why the code must do something, units, ownership, lifetime, or guest layout. |
| Evidence/provenance | Usually short | “Measured at …; see `docs/...` / fixture / issue.” Link to durable evidence when practical. |
| Uncertainty/open probe | Yes when actionable | `TODO(#N): verify ... using ...`; distinguish hypothesis from confirmed behavior. |
| Historical narrative | Usually no | Move the story, failed attempts, and session chronology to an evidence page. |

Specific actions:

- Preserve and extend the strong contract comments in `lambo_vehicle.h`, `lambo_config.h`, and public-looking subsystem headers.
- Add short API comments to headers whose exported functions currently require source-diving, including the replay runtime, harness report, input gate, file/path, startup, track patch, and Pak I/O interfaces.
- In `stub_renderer.cpp`, `main.cpp`, `libultra_stubs.c`, and `gen_syms_toml.py`, separate the current reason for a branch from the history of how it was discovered.
- Replace unexplained bare `W###` or `#N` references with a short label and a durable link where the reference is still useful.
- Do not rename unknown guest fields merely to make the code look finished. Banjo’s practice of preserving uncertainty is a good model; the project should make the uncertainty status explicit instead.
- Remove or update stale future-tense comments such as the rumble note tied to a completed issue.

Do not attempt a wholesale comment rewrite. Review comments when touching a subsystem and use the taxonomy in the PR checklist.

### D11 — P2: research, reference, and user guides are in the same namespace

The current `docs/` directory mixes stable platform instructions (`ANDROID.md`), practical modding (`TEXTURES.md`, `TRACK_LAB.md`), implementation references (`HUD.md`, `recompfrontend.md`), and chronological research (`no_lod_audit.md`, `TRACK_MODDING_RESEARCH.md`). That is a useful archive but not an easy public information architecture.

Use a gradual classification rather than a disruptive rename:

```text
docs/
  README.md
  CONFIGURATION.md
  DEVELOPMENT.md
  ARCHITECTURE.md
  TESTING.md
  DEBUGGING.md
  MODDING.md
  evidence/
    README.md
    no-lod-history.md
    track-research.md
```

Initially, leave stable filenames in place and link them from the map with status labels. Rename or move documents only when their audience or current-status problem justifies it. In particular, make `MODDING.md` a router and keep the detailed texture and Track Lab pages as task references.

### D12 — P2: documentation changes have no explicit review or validation gate

The project’s PR template is a good start, but it is not consistently completed. The latest merged [PR #205](https://github.com/alondero/automobililamborghini-recomp/pull/205) contains a placeholder-style `Closes #` line despite the template asking for an issue link. The sampled documentation-focused PRs [#90](https://github.com/alondero/automobililamborghini-recomp/pull/90), [#107](https://github.com/alondero/automobililamborghini-recomp/pull/107), [#144](https://github.com/alondero/automobililamborghini-recomp/pull/144), [#167](https://github.com/alondero/automobililamborghini-recomp/pull/167), and [#173](https://github.com/alondero/automobililamborghini-recomp/pull/173) have no review comments. [#187](https://github.com/alondero/automobililamborghini-recomp/pull/187) has review records, but they are maintainer-authored; [#192](https://github.com/alondero/automobililamborghini-recomp/pull/192) is primarily a code fix with documentation impact. This is understandable in a single-maintainer project, but it means the repository has no systematic second reading for public-facing instructions.

Update the PR template to include:

```text
Docs impact:
- [ ] none
- [ ] user-facing docs
- [ ] contributor/build docs
- [ ] source/API comments
- [ ] evidence/research status

If docs are affected:
- [ ] current source-of-truth page updated
- [ ] docs index/backlinks updated
- [ ] commands and paths tested
- [ ] status, limitations, and verification date updated
- [ ] screenshots/logs/hashes/fixtures attached when useful
```

Use an actual issue number in `Closes #N`, or write `No linked issue: small cleanup` outside an HTML comment. Add a `docs` label family (`docs-user`, `docs-dev`, `docs-evidence`, `docs-tooling`) and require a second review for build, configuration, release, and architecture docs whenever another maintainer or trusted contributor is available.

### D13 — P2: the roadmap and issue-to-document flow need maintenance rules

The [#26 epic](https://github.com/alondero/automobililamborghini-recomp/issues/26) is useful historical context and a good explanation of the original peer-project survey, but it still presents many already-closed items as future waves. Since `CONTRIBUTING.md` sends new contributors to #26, it currently behaves as an outdated roadmap.

Choose one of these explicit states:

- update #26 into a current roadmap with checked-off work, links to the resulting docs, and a “not a complete backlog” note; or
- mark #26 as the historical roadmap/survey and create a current `docs/ROADMAP.md` or GitHub project view.

The second option is preferable if the issue is difficult to maintain. Either way, the current roadmap must link to the documentation backlog and identify which items are blocked on measurement, implementation, or review.

Use [#88](https://github.com/alondero/automobililamborghini-recomp/issues/88) as a positive example of an evidence ticket with a defined deliverable, but add the default-branch and index requirements from D4. [#188](https://github.com/alondero/automobililamborghini-recomp/issues/188) and [PR #187](https://github.com/alondero/automobililamborghini-recomp/pull/187) show a useful contract-oriented docs/tooling change; the next improvement is making that contract easy to find from the docs map and validating it when the implementation changes.

## Lessons from the reference projects

The three reference projects are strong in different ways. Their patterns should be adapted, not copied wholesale.

### Banjo-Kazooie decompilation

The [Banjo README](https://github.com/n64decomp/banjo-kazooie/blob/master/README.md) is compact but executable: it records supported ROM checksums, Linux/Docker/CI paths, submodules, Python setup, version selection, and module-specific build targets. Its visible `100.0000%` progress and [`progress/`](https://github.com/n64decomp/banjo-kazooie/tree/master/progress) artifact make project status legible. The active project’s [Style Guide](https://gitlab.com/banjo.decomp/banjo-kazooie/-/wikis/Style-Guide) records naming and subsystem conventions, while the [Actors guide](https://gitlab.com/banjo.decomp/banjo-kazooie/-/wikis/Actors) turns “document this area” into staged beginner and advanced tasks.

Adopt:

- ROM identity and supported version information in the first build path;
- a small repository style/terminology guide;
- repeatable, file-sized documentation tasks;
- explicit confirmed/unknown/hypothesis language in decompilation comments;
- a visible current status/progress artifact.

Do not adopt the split between a GitHub mirror and a GitLab-first workflow. Keep the project’s source of truth in the repository and link issue/PR evidence directly.

### Shipwright / Ship of Harkinian

The [Shipwright README](https://github.com/HarbourMasters/Shipwright/blob/develop/README.md) is a strong router: quick start, platform notes, controls, architecture, custom assets, development builds, and further reading. Its [`docs/BUILDING.md`](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/BUILDING.md) gives platform-specific dependencies and commands. [`MODDING.md`](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/MODDING.md) is a worked contributor journey rather than a list of concepts. [`FORMATTING.md`](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/FORMATTING.md), its [pinned pre-commit configuration](https://github.com/HarbourMasters/Shipwright/blob/develop/.pre-commit-config.yaml), and [`VERSIONING.md`](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/VERSIONING.md) make maintenance expectations executable.

Adopt:

- separate user setup, developer build, modding, formatting, versioning, and architecture pages;
- a worked “make your first change” flow with expected checkpoints;
- exact tool versions when they affect reproducibility;
- a clear boundary between imported/decomp-derived code and project-owned C++;
- PR evidence and generated artifacts that remain visible in the PR.

The project need not immediately reproduce Shipwright’s multi-platform build matrix or external setup website. The structure and task orientation are the important lessons.

### SpaghettiKart

SpaghettiKart’s [documentation source tree](https://github.com/HarbourMasters/SpaghettiKart/tree/main/docs) is the best model for task-oriented navigation. Its [`mainpage.md`](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/mainpage.md) routes users to modding, track making, and characters. Track creation is decomposed into overview, quick reference, setup, materials, paths, import/export, properties, and troubleshooting. The [`mods.toml` schema](https://github.com/HarbourMasters/SpaghettiKart/blob/main/docs/mods-toml.md) gives minimal and complete examples, a field table, validation, dependency ordering, and best practices. A [Doxygen configuration](https://github.com/HarbourMasters/SpaghettiKart/blob/main/Doxyfile) and [documentation workflow](https://github.com/HarbourMasters/SpaghettiKart/blob/main/.github/workflows/doxygen-and-linux-test.yml) publish generated reference pages.

Adopt:

- a task-oriented docs landing page;
- overview/setup/reference/import-export/troubleshooting decomposition for modding;
- exact trees, schemas, examples, and expected output;
- generated API/reference docs only after a minimum source-comment standard exists;
- a PR-time docs build/link check even if publication remains push-to-main.

SpaghettiKart’s generated site is a later option, not a prerequisite. This repository will gain more from a maintained Markdown map and current-status cards first.

## Target information architecture

The following is a proposed end state. It can be reached without a large rename in one change.

```text
README.md                 Public overview, legal/ROM requirements, quick run, status
BUILDING.md               Fast host build and recovery; links to platform guides
CONTRIBUTING.md           Contribution rules, issue/PR expectations, legal boundaries
CLAUDE.md                 Agent-specific context; links to human developer docs

docs/
  README.md               Documentation map and status legend
  CONFIGURATION.md        User-facing keys, defaults, paths, overrides, controls
  ARCHITECTURE.md         Runtime/generated code/renderer/frontend system model
  DEVELOPMENT.md          First change, source boundaries, conventions, workflow
  TESTING.md              CTest, build smoke, scenarios, visual checks, ares
  DEBUGGING.md            Logs, warp, states, watchpoints, common failures
  MODDING.md              Modding router and supported/experimental boundaries
  PATCHES.md              Patch purpose, application, ownership, upstream status
  RELEASING.md             Release/package checklist and platform handoffs
  evidence/
    README.md              Evidence index and confidence/status vocabulary
    ...                    Historical or subsystem-specific investigations
  user-facing/             Optional later grouping for large modding guides
```

Keep current filenames such as `TEXTURES.md`, `TRACK_LAB.md`, `HUD.md`, and `CAR_DIFFERENCES.md` where useful. The index matters more than the directory rename.

## Prioritized implementation plan

### First documentation pass: unblock readers

1. Add `docs/README.md` with the map, status legend, and links to all 15 current docs.
2. Add a short “Documentation” section to the root README and link the map, build guide, contributor guide, configuration, modding, testing, and debugging routes.
3. Rewrite the top of `BUILDING.md` around `build.sh`, `build.ps1`, and the Android script; retain manual patch/recompile details below.
4. Add the missing Windows/PowerShell equivalents, `ctest` command, expected outputs, clean/retry instructions, and supported tool/ROM matrix.
5. Add current-status cards to `no_lod_audit.md`, `rumble-triggers.md`, `analog-throttle.md`, `TRACK_MODDING_RESEARCH.md`, and `recompfrontend.md`; fix stale future-tense statements.
6. Fix #96’s branch-only completion and update #26’s roadmap status.

### Second pass: give contributors a model

1. Add `docs/ARCHITECTURE.md`, `docs/DEVELOPMENT.md`, and `docs/TESTING.md`.
2. Add `docs/CONFIGURATION.md`, including `no_lod_circuit` and a complete input/control reference.
3. Implement the patch index and upstream tracker from #132–#134.
4. Add `docs/MODDING.md` as a router, with a clear supported/experimental table for textures, tracks, and Track Lab.
5. Add the source-comment taxonomy to CONTRIBUTING and the PR template.

### Third pass: prevent regression

1. Add a CI `docs-check` job for relative links, case-sensitive paths, broken anchors, and required index membership.
2. Add a small config-parity check comparing documented keys/defaults to the code metadata or generated configuration table.
3. Add a docs freshness warning based on `Last verified` and `Applies to`; do not fail historical evidence pages merely because they are old.
4. Require documentation impact classification in PRs and a current-source-of-truth link for configuration, build, architecture, and release changes.
5. Consider Doxygen or another generated reference site only after the public headers have meaningful comments and the Markdown map is stable.

## Documentation standard for new pages

Every new operational or reference page should answer these questions near the top:

1. Who is this for?
2. Is it stable, experimental, historical, or awaiting verification?
3. What commit/tag or runtime version does it describe?
4. What is the canonical source of truth?
5. What can the reader do in five minutes?
6. What command, fixture, screenshot, hash, or external comparison verifies it?
7. What is not supported or still unknown?
8. What related page, issue, or PR should the reader follow next?

For command-heavy pages, test the commands from a clean checkout or state exactly what has already been built. For paths, use case-correct repository paths. For external links, prefer the primary repository, issue, PR, or official tool documentation. For research, preserve the evidence and uncertainty instead of converting a hypothesis into a confident summary.

## Bottom line

The project does not need more raw notes first. It needs a map, current-status summaries, a human contributor path, and a few consistency checks around the excellent evidence already present.

The highest-return first PR is therefore: add the docs index, repair the build quick path, fix the stale/branch-only references, and route every existing document by audience and status. The next PRs should add architecture, configuration, testing, and patch/upstream references. Once those exist, future reverse-engineering notes and source comments will have a durable place to land instead of becoming another isolated island.
