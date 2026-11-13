# Targeted decompilation: assessment and repeatable prompts

Status: **Confirmed** local source inspection at `0b13e40` on 2026-11-13.
Recommendations and candidate priorities are **Inferred**. No new ROM parity,
profiling, game run or peer-project playthrough was performed for this assessment.
The [peer research](targeted-decomp-peer-research.md) records external evidence.

## Assessment

Targeted recovery of readable game source is a good default when several
enhancements depend on the same game decision, or when instruction hooks obscure
who owns state. It can enable more capable mods and easier verification. It is
an investment in understanding and ownership; faster execution needs its own
evidence. The objective is useful, verified source boundaries, rather than a
percentage of the game decompiled.

Three operations have different purposes:

| Operation | What it establishes |
| --- | --- |
| Static recompilation | Translates the game's machine instructions into host-buildable code. The existing port already does this. |
| Targeted decompilation | Recovers readable logic, types, state transitions and complete owners from ROM evidence. This can stop at documentation when no replacement is justified. |
| Source replacement or enhancement | Integrates recovered logic, then changes a specified behavior through a deliberate interface. Each change needs its own validation. |

Ship of Harkinian is a decompilation-based source port; Banjo: Recompiled uses
static recompilation with hand-written patches. Their architectures differ.
The transferable pattern is understood game logic, explicit extension points
and a clear platform boundary. Their features do not prove a Lamborghini
speedup or require a whole-game decompilation. See the concrete source examples
and limitations in the [comparison](targeted-decomp-peer-research.md).

In this repository, recovered C is compiled to big-endian MIPS and then passed
through N64Recomp. It is not compiled directly as ordinary native C over host
objects. Guest addresses, data layouts, service calls and recompilation costs
still exist. A direct host implementation would be a separate design with
additional memory, ABI, lifecycle and mod-compatibility obligations.

## What the pit-stop work actually demonstrates

The [pit handler](automatic-pit-stops.md) replaced three hooks dependent on stack
offsets/registers with a complete guest owner and a setting-only host query.
Its independent ROM oracle compares persistent RAM and ordered external calls;
the recorded 5,120 deterministic cases provide much stronger evidence than a
test of just the assistance formula. They do not establish a speedup or complete
interactive presentation coverage.

The same pattern is already implemented for [vehicle selection](guest-vehicle-select.md),
[menu input](menu-input.md) and the [complete vehicle updater](vehicle-source.md).
Current build integration in [GuestPatches.cmake](../cmake/GuestPatches.cmake)
includes all four areas. The updater also moved brake history into snapshotted
guest storage, with an explicit, tested change to restore behavior. These are
completed migrations, not fresh shortlist entries. The older
[candidate audit](guest-source-candidates.md) and [bridge inventory](guest-bridge-inventory.md)
are historical starting points; check their successors and current source.

## Where the approach pays off

| Goal | When source recovery helps | Evidence required before accepting the work |
| --- | --- | --- |
| Performance | A measured guest owner does redundant work, repeatedly crosses an expensive bridge, or produces avoidable rendering work. | A profile identifying the owner and critical path, a plausible cost-removal mechanism, repeated controlled before/after measurements and behavior checks. |
| Native controls, options and mods | A recovered decision point can consume host input/settings or emit a stable event without exposing register allocation. | Input ownership, sampling order, digital fallback, live-toggle behavior, lifecycle and original-entry/mod-hook compatibility. Physical feel needs an interactive check. |
| Replace fragile hooks | Several register/stack/instruction-offset substitutions implement one behavior, or host code mirrors game progress. | Complete-owner or proven extraction contract, independent parity, explicit removal of obsolete production hooks/state, and preserved deliberate enhancements. |
| Other capabilities | Correctness, deterministic replays, AI/race rules, accessibility, save semantics, regional understanding or authoring tools need missing game semantics. | A specific user/developer problem, known state owner, bounded recovery scope and a suitable oracle or falsification experiment. |

For performance, separate game logic, port/bridge CPU work, rendering submission,
GPU work, I/O and synchronization first. Reducing an off-critical-path CPU cost
may not change presentation time. Report logic updates, VIs and interpolated
presentations separately. A lower instruction count or a helper microbenchmark
alone does not establish a game-wide gain.

The [performance audit](performance.md) includes host settings/diagnostic costs,
rendering choices and an attempted visibility cache that was not adopted after
measurement. Those examples support profiling and allowing a no-change result.
Decompilation is only one possible response to an identified cost.

## Starting points for the next discovery pass

These are source-confirmed leads with inferred suitability, not newly validated
replacement boundaries or a measured performance ranking. Hook definitions in
[gen_syms_toml.py](../scripts/gen_syms_toml.py) and declared ranges in
[the symbol table](../lamborghini.syms.toml) were inspected. Declared ranges can
overlap and are not proof of complete function boundaries.

| Lead | Potential benefit | First question to resolve |
| --- | --- | --- |
| An individual HUD/flare/sky emitter | Own aspect policy and command emission together; remove argument-register/bracketing dependencies. Current flare hooks name `func_80036854`; the [HUD](HUD.md) and [sky](sky-panorama.md) notes describe other owners. | Which complete emitter offers the smallest verified boundary and useful simplification? Include rain suppression, other callers and task-buffer lifetime when applicable. |
| Scene builder, `func_8000A6C0` | Consolidate visibility/LOD, object interpolation and road-weather seams; potentially eliminate repeated producer work. | Can a bounded owner be recovered without taking on the whole scene builder? The stack-dependent interpolation hook is real, but a performance benefit is unmeasured. |
| Setup-menu initializer, `func_8003CD84` | Replace four reverse-track branch-operand overrides with a source-owned availability decision. | Is recovering the larger initializer worth four small hooks? Preserve earned progress, unrelated vehicle unlocks and entry-time toggle behavior. |
| Timer owner within `BootLoadInitialAssets` | Give timer policy and clock state a readable owner; improve testability of future race rules. | Is there a callable boundary, or must an extraction contract be proved? Existing extracted timer tests do not prove a standalone function. |
| Guest input-consumption or race-launch lifecycle | Give assists/replays/mods a named timing boundary; reduce dispatcher/address coupling. | Which order of input sampling, pause/reset, queued graphics completion, audio and race setup is required? Existing mod events already cover part of this need. |
| Stub/function-boundary or regional investigation | Recover missing behavior or establish reliable mappings before extending gameplay. | Is the defect a bad symbol split rather than missing logic? USA split fixes and JP overlay/address work are different tasks. |

The most useful immediate audit is a comparison of individual producer emitters
with the remaining setup/timer hooks. Do not assume the emitter is small from
its visible hook count. Keep the scene builder as a higher-cost lead until its
boundary and verification cost are established. Use the performance prompt
independently before calling any of these a performance priority.

## How to use the prompts

Each prompt below is standalone. Run one category per discovery session, using
the current checkout. Discovery should produce a shortlist and one bounded
follow-up prompt, without starting a speculative rewrite. Re-run discovery after
major migrations or new profiling evidence. Reuse current subsystem notes and
existing issues rather than creating competing inventories.

### 1. Find performance candidates

```text
Audit this checkout for performance work where targeted decompilation or a
source-level guest patch would materially help. Start with CLAUDE.md,
CONTRIBUTING.md, docs/README.md, docs/performance.md, docs/architecture.md,
docs/guest-bridge-inventory.md and current guest-source implementation notes.
Treat historical rankings as leads and exclude already completed migrations.

First classify the cost: guest logic, host bridge/runtime, renderer submission,
GPU, I/O or synchronization. Inspect available profiles and replay evidence.
If measurements are missing, specify or run a bounded profiling experiment
using existing tooling; mark candidates unmeasured. Do not invent hot functions,
frame-time savings or a minimum hardware requirement. Distinguish game updates,
VIs and interpolated presentations.

For each lead, identify current files, symbol/runtime address, known boundary,
callers, inputs/state, cost-removal mechanism, expected affected workload and
cheaper alternatives. Compare recovery with a host fix, a renderer fix, a
compiler change and leaving the code alone. Separate maintenance value from
measured performance value. Do not edit generated output or production code.

Return at most five candidates with evidence locations, confidence, likely
effort, behavior/mod risks and a validation plan. Rank measured opportunities
separately from profiling leads; returning no decompilation candidate is valid.
For the best lead, give an independent behavior oracle and a repeated A/B
benchmark plan with fixed scene/input/build/settings/hardware, warmup, run
variation, CPU/GPU timing and a success threshold chosen before implementation.
End with one copy-paste prompt for the next bounded experiment or change.
```

### 2. Find improvements to controls, options and mods

```text
Audit this checkout for game decisions whose recovery into readable guest
source would enable better controls, accessibility, options or composable mods.
Read CLAUDE.md, CONTRIBUTING.md, docs/README.md, docs/architecture.md,
docs/controllers.md, docs/mod-api.md and the current guest-source notes first.
Recheck the old guest-source candidates against actual source so completed
pit, selector, menu-input and vehicle-updater migrations are not proposed again.

Start from concrete player or mod-author problems. Trace host device/settings
publication through guest consumption to the behavior it controls. Identify
where an existing recovered owner, query, event or API can be extended before
proposing more decompilation. Leave platform input, persistence and UI with
their existing host owners. Do not treat moving a hardcoded address into C as
a stable interface. Do not change production or generated code during discovery.

Return at most five candidates, each with the user benefit, files and symbols,
complete owner or unresolved boundary, current fragile coupling, proposed
minimal settings/input/event contract, and what recovery newly enables. State
sampling timing, thread ownership, units/ranges, digital fallback, toggle,
pause/restart, player-count and snapshot behavior. Describe mod composition,
replacement conflicts and compatibility with calls through the original entry.

Rank by user value, confidence, recovery scope and verification effort. For
the top candidate, define stock/default parity, intended enabled differences,
deterministic tests, runtime mod checks and interactive/controller checks.
Separate automated logic evidence from physical feel and visual claims.
End with one bounded implementation prompt and explicit non-goals.
```

### 3. Find fragile hooks worth replacing

```text
Audit this checkout for existing patches whose fragility would be reduced by
targeted source recovery. Read CLAUDE.md, CONTRIBUTING.md, docs/README.md,
docs/architecture.md, docs/automatic-pit-stops.md, docs/guest-source-candidates.md,
docs/guest-bridge-inventory.md and their current implementation successors.
Inspect scripts/gen_syms_toml.py, scripts/race_field_patches.py, guest/ and the
actual host adapters. Discovery only: do not change production/generated code.

Find register substitutions, stack offsets, instruction-address hooks,
duplicated game-state ownership and post-emission repair. Group by the owning
game function/decision, not by feature name or number of MEM_* uses. Distinguish
semantic hacks from mechanical table relocation and necessary runtime/platform
adaptation. Small explicit, tested hooks may remain the best solution.

Return at most five candidates. For each, cite exact source and symbol evidence,
what depends on compiler layout today, which production hooks/state would go,
what new coupling remains, recovery scope, unknown callers/interior edges,
oracle availability, mod risk and effort. A symbol or disassembly fragment is
not proof of a complete function; require a proven contract for any extraction.
Do not rename unknown fields based on guesses or propose completed migrations.

Rank by useful coupling removed relative to verification cost and behavior risk.
For the top candidate, plan an independent ROM oracle preserving current
relocations and frozen enhancements. Compare persistent RAM and ordered calls;
justify exclusions such as compiler stack scratch. Keep intentional bug fixes
or lifecycle changes separate from parity. Include original-entry and runtime
mod hook/conflict checks; rendering needs command/capture evidence too.
End with one copy-paste recovery prompt, or explain why retaining the hook wins.
```

### 4. Find other valuable recovery work

```text
Find high-value uses of targeted decompilation beyond performance and hook
cleanup in this checkout. Read CLAUDE.md, CONTRIBUTING.md, docs/README.md,
docs/architecture.md, docs/mod-api.md, docs/testing.md and relevant current
subsystem evidence. Check completed work and existing issues before proposing
new work. Keep this a discovery task; do not change production/generated code.

Consider correctness and hidden limits, race rules/AI, accessibility, replay
determinism, save/progression semantics, multiplayer, camera/animation behavior,
weather/track authoring and USA/JP understanding. Choose problems supported by
source, tests or a reproducible symptom, rather than proposing every category.
Distinguish understanding-only research, symbol/boundary correction, data-format
recovery, extension of already recovered source, and a new source replacement.
Prefer the cheapest route that answers the actual question.

Return at most five candidates. For each, give the concrete capability or defect,
evidence and confidence, owning files/functions/data, minimum recovery scope,
required dependencies, what remains unknown, and the experiment that could
disprove the proposed explanation. Explain how the result becomes a reusable
symbol/type/event/tool or regression fixture. Preserve guest layout, endian,
thread/lifecycle and regional boundaries. A source replacement does not itself
create a complete savestate system, network synchronization or JP compatibility.

Rank by practical value, feasibility and verification cost. For the best one,
define a bounded deliverable, acceptance evidence, non-goals and one copy-paste
follow-up prompt. If evidence is too weak for implementation, make the follow-up
an investigation with a clear stop condition and the artifact it should produce.
```

### 5. Turn a selected candidate into a verified patch

```text
Implement the selected candidate described below using the repository's existing
guest-source pipeline. First read CLAUDE.md, CONTRIBUTING.md, docs/README.md,
the current subsystem evidence and the pit/selector/menu/vehicle parity patterns.

Candidate and intended behavior: [paste the selected discovery result here]

Before replacing anything, verify the supported ROM identity, complete function
boundaries, incoming edges/callers, data references, widths, signedness, units,
floating-point steps and thread/phase ownership. If a required contract cannot
be established, report that specific gap with evidence instead of guessing.
Recover only the bounded owner; keep unknown fields neutral and host settings,
devices, files and renderer services with their existing owners.

Build the independent original-ROM oracle before removing production originals.
Preserve existing relocations and freeze existing enhancement semantics in the
reference. Compile guest C through Clang/LLD and N64Recomp; never edit generated
output or cast host structs over RDRAM. Keep reconstruction and deliberate
behavior changes reviewable separately. Document any intentionally changed
save/toggle/input sampling semantics and test them explicitly.

Compare persistent RAM and ordered service calls over boundaries and multi-update
sequences. Justify every excluded region. Preserve original callable entries,
patch bytes/relocations and mod-hook regeneration; test entry/return hooks,
original-symbol calls and replacement conflicts. Remove obsolete hooks/state.
Run relevant build/tests and end-to-end scenarios. Add command-stream and visual
checks for rendering, physical-control checks for feel, and controlled A/B
measurements for any performance claim. Report checks not run and oracle limits.

Update the owning evidence page with reproducible results and remaining limits.
Do not widen scope to neighboring functions, regions or speculative APIs.
```
