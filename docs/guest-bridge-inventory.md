# Guest bridge inventory

Status: **Confirmed source inventory**, inspected at `b5ae30c` on 2026-11-07.
Migration suitability below is an engineering assessment, not a verified
reconstruction. No runtime or new ROM-oracle measurements were made for this
inventory. This is the bridge inventory for the audit requested by
[#393](https://github.com/alondero/automobililamborghini-recomp/issues/393).

## How to read the inventory

**Host access** means handwritten code reads or writes emulated RAM.
**Generated hook** means checked-in generation inputs inject a call or modify a
register at a particular guest instruction. **Relocation** means substituting
an address while preserving the instruction's operation. A single feature can
use all three. Host `MEM_*` accessors account for the word-swapped RAM image;
guest source would instead use big-endian MIPS layouts.

The hook source of truth is
[gen_syms_toml.py](../scripts/gen_syms_toml.py), supplemented by
[race_field_patches.py](../scripts/race_field_patches.py). Generated C is not
evidence of recovered source. Source migration must first establish complete
function boundaries and compare behavior against ROM-derived execution.

## Input and gameplay bridges

The [ranked shortlist](guest-source-candidates.md) records detailed layouts,
owners, evidence and parity plans for selectors, menu input and vehicle update.

| Area and code | Mechanism and ownership | Disposition |
| --- | --- | --- |
| [Pedals](../src/lambo_analog_throttle.cpp), [brake](../src/lambo_analog_brake.cpp), [steering](../src/lambo_steering.cpp) | Host atomics/settings feed game-thread generated hooks inside `func_80019D20`; hooks read/write guest fields, and steering also rewrites a loaded register. | Rank 3 as one complete updater; host sampling remains outside guest source. |
| [Menu stick](../src/lambo_menu_stick.cpp) | Four generated register substitutions in `func_800427D4`; host only scales a value using an atomic setting. | Rank 2: recover the full handler before replacing the hooks. |
| [Cheats](../src/lambo_cheats_runtime.cpp) | Generated register substitutions in selectors, setup menus and the boot dispatcher's timer block. Host exposes flags, guest code owns writes. | Selector pair is rank 1. Timer and reverse gates sit inside larger owners; existing extracted-block tests do not establish standalone callable functions. |
| [Driving assists](../src/lambo_driving_assists_runtime.cpp) and [replay](../src/lambo_replay_runtime.cpp) | Game-thread hooks consume host-produced input or recording state and modify guest controller data at dispatch timing. | Retain host device/recording policy. Recover a named guest input-consumption seam before considering source migration; avoid changing menu edges or replay order. |
| [Pit service](../guest/pit_stop.c) | Complete compiled guest replacement, host setting query only; original entry preserved. | Completed pattern, not a new candidate. [Parity evidence](automatic-pit-stops.md) defines the verification standard. |

## Non-input bridges

| Area and code | Mechanism and guest state | Timing and ownership | Migration assessment |
| --- | --- | --- | --- |
| [Object interpolation](../src/lambo_interpolation.cpp) | Generated hooks in `func_8000A6C0` at runtime `0x80009AF8` and `0x8000E400`; entry reads the object halfword at `sp+0x1B2`. Host reads relocated vehicle flags and viewport halfword `0x800CE6A6`; writes 64-bit extended GBI commands through the u32 display-list cursor `0x800A39CC`. | Guest game thread builds each viewport's list. Object/viewport IDs must survive culling and reorder; body/wheel order determines linear versus automatic matching. | Good eventual benefit from source ownership of object iteration, but the two short adapters conceal a dependency on the much larger scene builder. Keep RT64 interpolation semantics; merely moving tag emission to guest C does not remove the stack hook. [Existing tag tests](../tests/test_interpolation_tags.cpp) check policy, not full scene-builder parity. |
| [VI promotion](../src/lambo_vi.cpp) | Host-only copying of two 0x30-byte libultra contexts reached through u32 pointers `0x8008D1A0/0x8008D1A4`; forwards mode, framebuffer and black state to host VI functions. Invalid pointers are a no-op. | Runtime VI callback thread, as specified in [lambo_vi.h](../src/lambo_vi.h) and called from `vi_cb` in [main.cpp](../src/main.cpp). This is outside the normal game-thread guest-hook model. | Retain as runtime adaptation for this audit. Replacing it requires a scheduler/VI synchronization design, not just decompiling gameplay. [VI context tests](../tests/test_vi_context.cpp) cover the bridge. |
| [Developer save states](../src/lambo_savestate.c) | Generated frame-entry hook in `func_800028D0` at `0x80001CD4`; host snapshots low 8 MiB plus relocated race storage `0x80900000..0x80924000`, normalizes corrected fog words, and relinks native thread contexts on load. | SDL publishes atomic requests; game thread copies RAM at a fixed dispatcher boundary. Native renderer/VI/audio threads are not parked by the guest cooperative scheduler. Native stacks are not serialized. | Retain host filesystem, snapshot and native-runtime work. Source-level lifecycle events could remove the instruction-offset hook later; they cannot turn RAM snapshots into complete execution snapshots. |
| [Developer warp](../src/lambo_warp.c) | Host writes signed-halfword menu cursors (players, circuit, laps, mode, model, difficulty) in the `0x800CE6xx/0x800CE7xx` clusters, calls guest audio routines, and sets game state `0x800CE6AC` to 7. Current generated hook is `func_800028D0` at `0x80001CD0`, shared with reset and the queued-task drain gate. | SDL publishes an atomic request; game-thread dispatch consumes it before loading a race. The hook must return while old graphics tasks remain queued rather than block message processing. | A guest race-launch function is a useful future seam, but migration must include launch ordering and audio/task lifetime. It is not a small cursor-write cleanup. Some comments in `lambo_warp.c` still name the older dispatcher; use current generation inputs for placement. |
| [Track Lab corrections](../src/lambo_track_patch.cpp) | Generated post-load hook in `boot_pad_apply_calibration` at `0x80006094`; host reads context pointer `0x80098238`, circuit s16 `0x800CE794`, and context u32 PVS begin/end at +4/+8. Writes signed-halfword entries in 20-byte PVS rows only after circuit, shape, hash and every expected-old-value guard pass. Also reapplies after snapshot load. | Game-thread post-load/post-restore mutation; immutable package is atomically published. File parsing and allocation stay outside `apply_to_active_track`. Rejection is read-only. | Retain host package ingestion and validation. A named post-load event would remove offset coupling; wholesale track-loader reconstruction has little additional payoff for this bounded correction. [Tests](../tests/test_track_patch.cpp) and [interop checks](../tests/test_track_patch_interop.py) already cover the guarded operation. |
| [Scene visibility and LOD](../src/lambo_no_lod.cpp) | Semantic generated hooks in `func_8000A6C0` replace branch results, PVS entry/loop registers and drawn-list indices. Host writes float radii `[6][5]` at `0x80088FD0`, double view-cone cosines at `0x8008D8C0/C8`, and the car-distance halfword at `0x80098720`. PVS has ten signed-halfword slots per row, including negative holes; drawn list `0x800B6758` has 21 slots. | Game thread during world draw, per viewport. Rewrites repeat to handle live toggles and restored RAM. Camera FOV coupling applies independently of the no-LOD toggle. | Large potential reduction in register/layout coupling, but the owning scene builder also handles vehicles, scenery, interpolation and display lists. Treat as a larger reconstruction after narrower candidates. Preserve authored-entry order, culling and list capacity; [policy tests](../tests/test_no_lod_policy.cpp) alone are not a whole-function oracle. |
| [HUD alignment](../src/lambo_hud_widescreen.c), [sky extension](../src/lambo_sky_widescreen.cpp) and [flare alignment](../src/lambo_flare_widescreen.c) | Semantic generated hooks surround guest emitters; host appends extended display-list commands and adjusts matrices/arguments. Shared cursor is `0x800A39CC`; sky uses task pointer `0x800A2BFC`, viewport s16 `0x800CE6A6`, and task-owned extension storage. | Guest producer thread before task submission; sky storage reuse follows the original task-buffer fence. | Guest emitter replacements can remove argument-register and bracketing-offset knowledge. They still require host aspect/settings queries and RT64 command contracts. Assess each complete emitter separately; do not combine the entire renderer into one migration. See [HUD](HUD.md) and [sky lifetime evidence](sky-panorama.md). |
| [Producer display-list corrections](../src/lambo_graphics_task.cpp), [fog](../src/lambo_fog_widescreen.cpp), [texture bounds](../src/lambo_hud_texture_bounds.cpp) | Generated hook at runtime `0x80005728`, before task publication. Host validates the two USA task slots and microcode identity, walks F3DEX lists, then edits exact texture clamp contracts and fog words. Fog uses player/circuit halfwords and retains authored-word bookkeeping for snapshots. | Guest producer thread after asset loading/list construction and before `osSendMesg`; the task reuse fence protects mutation. Both renderers consume corrected lists. | A guest emitter reconstruction could author correct commands directly, but the shared bounded walker already consolidates several emitters and assets. Preserve this ownership; do not move writes to renderer threads. Exact command parity plus visual checks would be needed. GPU driver selection/advisory in [rt64_renderer.cpp](../src/rt64_renderer.cpp) is host platform policy, not guest-source work. |
| [Expanded race tables](../scripts/race_field_patches.py), [layout constants](../src/lambo_race_field.h) | Mechanical relocation scans verified USA ROM LUI/indexed references and replaces direct accesses with addresses in `0x80900000..0x80924000`. It does not relocate already materialized pointers twice. Vehicle stride is `0x10C`, racer stride `0x84`. | Build-time generation; replacements execute at the original guest instruction on its owning thread. Host race setup also initializes the expanded tables. | Do not count each relocation as semantic source-migration value. Preserve the relocated layout in any migrated guest function. The same generator separately contains semantic AI rank clamps, minimap palette wrapping, digit extensions and larger pool limits; those require behavior checks rather than address-equivalence checks. See [race fields](race-fields.md) and [relocation tests](../tests/test_race_field_patches.py). |

## Related work and evidence limits

[#338](https://github.com/alondero/automobililamborghini-recomp/issues/338)
owns regional address/layout selection; moving an address into guest C does not
make it region-independent. [#394](https://github.com/alondero/automobililamborghini-recomp/issues/394)
owns the broader gameplay-extension contract. Asset replacement under
[#20](https://github.com/alondero/automobililamborghini-recomp/issues/20) is a
separate concern from gameplay state ownership.

The original premise of
[#337](https://github.com/alondero/automobililamborghini-recomp/issues/337)
must not be used as a USA migration blocker. Current
[architecture](architecture.md) and [force-stub checks](../tests/test_force_stub.py)
describe USA's single text section and incomplete/overlapping function bounds;
JP has a separately described overlay. This inventory did not re-prove the
cartridge mapping or execute those tests.

For any replacement, compare persistent RAM and ordered guest-service calls
against an independent ROM oracle, preserving existing semantic enhancements
and relocations. Display-list work additionally needs command-stream and visual
evidence; runtime work needs concurrency/lifetime evidence. A host unit test of
an existing policy is useful input but is not proof of complete guest parity.
