# Targeted decompilation: peer-project evidence

Status: **Confirmed** source comparison, checked 2026-11-13. Peer games were
not built or played in this investigation; their runtime behaviour and relative
performance are **Unverified** here. Recommendations are **Inferred** from the
source examples below, not a claim that another game's implementation can be
copied directly into Lamborghini.

## Assessment

Recovering selected functions is a sound way to make substantial gameplay
patches easier to reason about, test and extend. Banjo and Zelda explicitly
compile hand-written guest C patches and recompile them into the port. Ship of
Harkinian demonstrates the broader value of readable game source and semantic
enhancement hooks, but it is a decompilation-based source port, not the same
static recompilation architecture. Neither comparison proves that recovering a
particular Lamborghini function will improve frame time.

Choose a player problem, measured bottleneck or fragile patch first. Recover
the smallest complete behaviour needed to own that problem, establish an
original-behaviour reference, then make the intended change. Treat recovered
guest C, a direct host replacement, renderer work and frontend work as distinct
implementation choices.

## Source snapshots

These links pin the source inspected, rather than following a moving branch.

| Project | Snapshot |
| --- | --- |
| Banjo: Recompiled | [dbd52154bc1e00e56867b1fac94a8f907052f41b](https://github.com/BanjoRecomp/BanjoRecomp/tree/dbd52154bc1e00e56867b1fac94a8f907052f41b) |
| Shipwright / Ship of Harkinian | [d4bf6528a3bb242bc5d862e4f1ab2b6d385c157b](https://github.com/HarbourMasters/Shipwright/tree/d4bf6528a3bb242bc5d862e4f1ab2b6d385c157b) |
| Zelda64Recomp | [b65c482ed672258eb684ab94a4e8c8aa662615ee](https://github.com/Zelda64Recomp/Zelda64Recomp/tree/b65c482ed672258eb684ab94a4e8c8aa662615ee) |
| N64Recomp | [598ce2080004a5672d604dceadbda436045307b7](https://github.com/N64Recomp/N64Recomp/tree/598ce2080004a5672d604dceadbda436045307b7) |

## Seven concrete examples

### 1. Banjo has the same guest-source patch mechanism

Banjo's [patch Makefile](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/patches/Makefile)
uses Clang's MIPS target, headers from the Banjo decompilation and a linker
script with retained relocations. Its [patch configuration](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/patches.toml)
feeds the resulting ELF to N64Recomp, resolves references through game symbols,
enables exports and validates patch symbols. Its [CMake build](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/CMakeLists.txt)
compiles the generated patches and links them with the original recompiled
functions, runtime and renderer.

**Implication:** Lamborghini's pit-stop approach follows an established
architecture. Compiling source to guest MIPS still preserves a guest-memory
boundary; it does not turn game structures into ordinary host pointers.

### 2. Banjo's controls change game semantics at named functions

The [camera and inversion patches](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/patches/camera_and_axis_inversion_patches.c)
replace complete functions, including `func_8029105C`. They obtain right-stick
input, apply deadzones and axis settings, and update the camera's yaw through
the game state machine. The enhancement is disabled during demo playback and
gated during crouching and egg actions so the stick can still supply C-button
actions. Other replacements handle camera-target history and transition state.

**Implication:** native-feeling input often requires understanding the consumer
and its modes, beyond mapping a modern device onto N64 buttons. For Lamborghini,
look at input consumption, pause/replay/pit gates and ownership before changing
sampling or injecting values.

### 3. Banjo makes object lifetime an explicit modding boundary

The [marker extension patches](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/patches/marker_extension_patches.c)
create extension data in `marker_init`, destroy it in the corresponding release
function and export named extension accessors. The [prop extension patches](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/patches/prop_extension_patches.c)
also maintain handles during deletion, compaction, sorting and cube cleanup.

**Implication:** recovering lifecycle owners can enable stronger mod interfaces
than exposing another address. Racer extensions, race reset and scene teardown
need defined identity and lifetime; more readable code alone does not supply
that contract.

### 4. Ship of Harkinian combines source edits with semantic hooks

Shipwright's [README](https://github.com/HarbourMasters/Shipwright/blob/d4bf6528a3bb242bc5d862e4f1ab2b6d385c157b/README.md)
identifies its dependence on the OoT decompilation. Its [game build](https://github.com/HarbourMasters/Shipwright/blob/d4bf6528a3bb242bc5d862e4f1ab2b6d385c157b/soh/CMakeLists.txt)
compiles that game C directly. In [camera source](https://github.com/HarbourMasters/Shipwright/blob/d4bf6528a3bb242bc5d862e4f1ab2b6d385c157b/soh/src/code/z_camera.c),
free-look consumes right-stick and mouse input and integrates with camera modes.
Its [hook table](https://github.com/HarbourMasters/Shipwright/blob/d4bf6528a3bb242bc5d862e4f1ab2b6d385c157b/soh/soh/Enhancements/game-interactor/GameInteractor_HookTable.h)
defines named game, actor, input, interface and save events. For example,
[faster rupee accumulation](https://github.com/HarbourMasters/Shipwright/blob/d4bf6528a3bb242bc5d862e4f1ab2b6d385c157b/soh/soh/Enhancements/TimeSavers/FasterRupeeAccumulator.cpp)
uses the interface-update hook and named save fields.

**Implication:** aim for a small set of meaningful extension points around
recovered game behaviour. A stable event can be better than requiring every
mod to replace the same function. This example changes pacing, not CPU speed.

### 5. Zelda patches scheduling to address latency

Zelda's [input-latency patches](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/input_latency.c)
move actual input polling into `PadMgr_GetInput` when the game requests it and
tag the poll for latency tracking. `Graph_TaskSet00` moves a wait until after
task submission, then waits for completion and framebuffer progression.
`GameState_Destroy` removes a wait made redundant by that change.

**Implication:** recovered scheduling code can expose valuable changes to
response time and ordering. This is evidence of a deliberate implementation,
not a measured latency reduction in this investigation. Such changes need
thread, queue, shutdown and frame-pacing verification; an FPS counter alone is
insufficient.

### 6. Zelda adds renderer information at game-side producers

Zelda's [actor transform patches](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/actor_transform_tagging.c)
replace actor and skeleton draw functions to emit matrix-group metadata for
interpolation. Its [culling and glow patches](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/culling.c)
retain distance tests while removing some frustum tests, and replace a glow's
old depth-buffer check with RT64 vertex-Z-test commands for widescreen support.

**Implication:** recovering the producer can replace a downstream rendering
guess with explicit intent. This is useful for camera cuts, object identity,
HUD placement, visibility and effects. Expanded visibility may increase render
cost, and game-source recovery does not replace GPU profiling.

### 7. Zelda's autosave is integrated with game state

The [autosave implementation](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/patches/autosaving.c)
checks UI, pause, cutscene and time conditions, tracks whether relevant save
fields have changed, waits for stability and resets timers around load/reset
events. It also adjusts save-loading and deletion behaviour and supplies mod
events and exports at meaningful stages.

**Implication:** save migration, persistence, checkpoints and deterministic
replays are additional reasons to recover selected code. A convenient write
address is insufficient when correctness depends on the surrounding lifecycle.

## What the comparison does and does not establish

N64Recomp's [design and patching documentation](https://github.com/N64Recomp/N64Recomp/blob/598ce2080004a5672d604dceadbda436045307b7/README.md)
describes literal instruction-by-instruction C translation and a separate
patch-ELF workflow. Readable reconstructed C can expose better algorithms and
remove dependence on intermediate registers, but it can still pass through
MIPS and the literal translator. It is not automatically an algorithmic
optimization or a direct native subsystem rewrite.

Banjo also retains [single-instruction patches](https://github.com/BanjoRecomp/BanjoRecomp/blob/dbd52154bc1e00e56867b1fac94a8f907052f41b/banjo.us.rev0.toml).
Established projects therefore support both small surgical patches and complete
source replacements. The evidence does not justify replacing every short hook,
recovering cold functions for hypothetical speed or starting a full decompilation
without a concrete benefit.

For performance candidates, first locate the limiting CPU/GPU/thread work and
estimate the recoverable fraction. If a routine consumes fraction `p` of the
limiting work, speeding just that routine by factor `s` bounds total speedup by
`1 / ((1 - p) + p / s)`, assuming other costs remain fixed. A cleaner pit-stop
routine can be an excellent maintenance and accessibility improvement while
having negligible effect on ordinary racing frame time.

The strongest repeatable workflow is: identify a concrete outcome; map the
smallest behavioural owner and its callers; recover and document the contract;
compare stock behaviour against an independent ROM-backed reference where
practical; add the intended enhancement separately; then measure its actual
benefit. A suitable existing hook or host-only fix should remain a valid result
of candidate discovery.
