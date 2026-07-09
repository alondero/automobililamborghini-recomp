#!/usr/bin/env python3
"""Generate a RUNTIME-ADDRESSED whole-ROM syms.toml for the modern metadata-driven
n64recomp (ultramodern + librecomp stack, ADR 0002 / epic #54, pivot phase 2).

WHY this exists
---------------
The feasibility spike (#55) proved n64recomp ingests this game's code, but only from
`build/boot_menu_race_combined.elf`, which lives in the SPLAT address world (entry
0x80001160, the +0xC00 offset world) and omits the real ROM entrypoint. ultramodern runs
the *real* recompiled entrypoint at RUNTIME addresses. This script produces the runtime
input n64recomp needs in ROM+syms.toml mode (the drmario64 template model).

WHAT it does (validated against N64Recomp/src/config.cpp + main.cpp + elf.cpp):
  * Source of function boundaries: `--dump-context` output of the combined ELF
    (recomp/dump.toml) -> 1088 functions with n64recomp's own sizes, at splat VRAM.
  * Bake split-function merges into `size` (the top-level `function_sizes` config key is
    ELF-mode ONLY and silently IGNORED in ROM mode). We replicate ELF-mode
    `manually_sized_funcs` semantics: override size by name, then DROP any function fully
    contained in another's [vram, vram+size) range (the absorbed split tails).
  * Complete the coverage gap below splat 0x80001160: add the entrypoint prologue funcs
    BootClearDMATable (splat 0x80001000, 0xB8) and BootInitSegmentTable (0x800010B8, 0xA8)
    from splat.yaml. The func that lands at runtime 0x80000400 / rom 0x1000 is what
    n64recomp renames to `recomp_entrypoint`.
  * Shift to RUNTIME VRAM: runtime = splat - 0xC00 for every addr >= 0x80001000
    (the documented splat-shift; reconciles splat 0x80001000 -> runtime 0x80000400 = the
    ROM-header entrypoint). Splat NAMES are kept so the race stubs/ignored lists still match.
  * Emit ONE contiguous .text section (rom=0x1000, vram=0x80000400). A single code section
    means in-section jal misses self-heal into static_ funcs; only cross-section misses are
    fatal. No relocs / no datasyms needed for absolutely-linked runtime code.

Usage:  python recomp/gen_syms_toml.py   (run from repo root; reads recomp/dump.toml +
        tools/n64recomp_race.toml, writes recomp/lamborghini.syms.toml)
"""
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DUMP = REPO / "dump.toml"
RACE_TOML = Path(__file__).resolve().parent / "n64recomp_race.toml"
OUT = REPO / "lamborghini.syms.toml"
CONFIG = REPO / "lamborghini.us.toml"

SPLAT_SHIFT = 0xC00          # runtime = splat - 0xC00 for addrs >= SHIFT_THRESHOLD
SHIFT_THRESHOLD = 0x80001000
SECTION_ROM = 0x1000
SECTION_VRAM = 0x80000400    # runtime entrypoint (ROM header bytes 0x08-0x0B)
ROM_SIZE = 0x400000          # 4 MB; per-function rom must stay in bounds
ROM_FILE = REPO / "Automobili Lamborghini (USA).z64"
CODE_ROM_START = 0x1000      # game code region in ROM (post-IPL3)
CODE_ROM_END = 0x88CE8

# Entrypoint prologue functions missing from the combined ELF (.text starts at 0x80001160).
# splat VRAM + size from splat.yaml boot_stage2 subsegments.
#
# BootClearDMATable is really TWO functions: a DMA-clear trampoline (0x1000..0x1050) that
# ends with `jr $t2` where $t2 = EntryPoint+0x50 (a STATIC target built by lui/addiu of the
# EntryPoint symbol), jumping to the real boot body at 0x1050 (own `addiu $sp,-0x20`
# prologue; jal's into func_80072F40 etc.). N64Recomp emits the `jr` as a runtime
# get_function(0x80000450) lookup, so 0x1050 must be its own registered function entry or
# the boot aborts ("Failed to find function at 0x80000450"). Split it here.
PROLOGUE = [
    ("BootClearDMATable",     0x80001000, 0x50),
    ("BootMain",              0x80001050, 0x68),  # jr target EntryPoint+0x50; real boot body
    # splat's BootInitSegmentTable (0x10B8, 0xA8) wrongly fused an 8-byte `jr $ra; nop` leaf at 0x10B8
    # with the boot idle-thread body that starts at 0x10C0. Once osCreateThread is routed to its native,
    # ultramodern resolves the thread ENTRY POINT via get_function(0x800004C0); a thread entry passed as
    # an ARG (not a jal target) is invisible to the jal-scan, so 0x10C0 must be its own registered start
    # or boot aborts ("Failed to find function at 0x800004C0"). Split here (verified from the recompiled
    # body: 0x4B8 = jr/nop leaf; 0x4C0 = addiu $sp,-0x20 prologue, = osCreateThread(thread 0x80091CD0,
    # id=1, entry=0x800004C0, pri=0xA) target in BootMain).
    ("BootInitSegmentTable",  0x800010B8, 0x08),  # jr $ra; nop leaf (splat over-sized it to 0xA8)
    ("BootIdleThread",        0x800010C0, 0xA0),  # osCreateThread(id=1) entry @ runtime 0x800004C0
    # The big boot body that loads the initial assets. splat.yaml boot_stage2 names it at ROM 0x1160
    # (splat vram 0x80001160 .. next subseg BootStage2UnkB70 @ 0x1B70), but its glabel sits on a leading
    # `nop` at 0x80001160; the real prologue `addiu $sp,-0x98` is at 0x80001164 (asm/BootLoadInitialAssets.s).
    # Absent from the combined-ELF dump (no FUNC symbol emitted in splat 0x80001160..0x800028D0). Once
    # osSetThreadPri lets func_800740E0/the boot idle thread complete, the runtime invokes this body and
    # ultramodern's get_function(0x80000564) aborts ("Failed to find function at 0x80000564") unless it is
    # registered at the REAL prologue. Register at 0x80001164 (runtime 0x80000564) so the lookup matches;
    # the leading nop at 0x80001160 is left uncovered (never an entry target). Size = 0x1B70 - 0x1164 = 0xA0C.
    # SPLIT-MERGE (do NOT shrink to 0xA0C): splat carved the boot main-thread function into three
    # pieces (BootLoadInitialAssets 0x80001164, func_80001CD0 0x80001CD0, func_800024F8 0x800024F8)
    # that SHARE one $sp,-0x98 frame and a SINGLE jr $ra epilogue at splat 0x800028C8. Control flow
    # branches across the piece boundaries (e.g. `b .L80002510`), and the body builds the boot frame's
    # display list (writes RDP cmds 0xBC00xxxx into the render-queue head D_800A39CC at 0x80001C48+)
    # then enters the per-frame dispatch loop. With the old 0xA0C size the recompiled thread body fell
    # off the end at 0x80001B70 with NO return-into-loop, so the OS thread (entry runtime 0x80000564)
    # simply RETURNED and exited -> the game produced 0 gfx tasks (verified live: thread "0x80000564"
    # EXITs ~immediately; all event delivery already works -- the scheduler __scMain wakes every retrace).
    # Real size = 0x800028D0 - 0x80001164 = 0x176C. The two later pieces are also jal'd internally
    # (jal func_80001CD0 @ 0x80002034/0x80002074; jal func_800024F8 @ 0x80002A80), so they are also
    # registered below as overlapping alternate-entry functions ending at the shared epilogue.
    ("BootLoadInitialAssets", 0x80001164, 0x176C),  # runtime 0x80000564; boot main thread (asset load + DL build + dispatch loop)
]

# Boundary corrections (splat addrs). splat occasionally labels a function start on the
# PRECEDING function's delay-slot nop (e.g. ROM ...jr $ra / nop / <real prologue>), so the
# named start is 4 bytes early. In ELF mode this is harmless; in ROM mode a jal to the REAL
# start lands mid-function and n64recomp invents a wrongly-sized `static_` there that fails to
# recompile. Each correction drops the mislabeled symbol and claims the real boundary.
# Format: (drop_name, new_name, new_splat_vram, new_size).
BOUNDARY_FIXES = [
    ("func_800102D4", "func_800102D8", 0x800102D8, 0xBFC),  # static_0_8000F6D8
]

# Split-merge corrections discovered from n64recomp "branching outside" diagnostics (the same
# pattern PROLOGUE applies to BootLoadInitialAssets). splat occasionally mislabels ONE function as
# two: a head whose --dump-context size stops at a splat SEGMENT boundary, and a "tail" the head
# actually branches into. We grow the head to its real (spimdisasm) size and DROP the absorbed
# tail -- valid ONLY when the tail is NOT an independent jal target (verified by grep before
# adding; a kept-but-stubbed tail would also work but leaves a dead symbol). Splat names; format:
#   head_name: (real_size, [absorbed_tail_names])
SPLIT_MERGES = {
    # W133 (2026-07-03, #53): func_8006B470 (runtime 0x8006A870) is the game->audio FIRST-HOP
    # jtbl dispatcher (W130/W132: the gate between game logic and the audio cascade; voice
    # registration func_80068230 runs 32x but voice-start func_80067FB4 runs 0x). splat carved
    # the real 0x1604-byte body into three exactly-contiguous fragments 0xA04+0x454+0x7AC
    # (branch at +0x128 targets 0x8006BE58 inside the third fragment -> "unhandled branch" ->
    # force-stub). Neither tail has any independent jal or stored-pointer reference in the
    # recompiled corpus (verified: no `jal 0x8006B274`/`jal 0x8006B6C8`, no 0XB274/0XB6C8
    # constants) -> absorb both. Same proven pattern as func_800028D0 below.
    "func_8006B470": (0x1604, ["func_8006BE74", "func_8006C2C8"]),
    # W134 (2026-07-04, #53): func_80078E14 (runtime 0x80078214) = alSndpNew, the sound-player
    # constructor. splat carved its 0x180 body into SIX contiguous fragments
    # (0x6C+0x18+0x58+0x38+0x60+0xC = 0x180, epilogue `jr $ra; addiu $sp,0x40` at splat
    # 0x80078F8C matching the head's `addiu $sp,-0x40` prologue). The func_80078F28 fragment
    # (standalone body starts on a backward `bnel` -> force-stubbed, stays in force_stub.txt as
    # the standalone-neutraliser) contains the load-bearing tail: it stores the voice-handler
    # pointer runtime 0x80077A7C (= func_8007867C, already an EXTRA_FUNCS indirect target) into
    # player->node.handler and calls alSynAddPlayer (runtime jal 0x8007F3F0 -> func_8007FFF0).
    # With the head truncated at 0x6C the constructor "succeeded" without ever registering the
    # handler with the synthesizer, so alSndpPlay events (posted 11x/2400 VIs, gdb-measured
    # W134) were never consumed and voice-start func_80067FB4 ran 0x -- the missing link of the
    # W133 silence. All internal branch targets (0x80078F10, 0x80078F30) fall inside the grown
    # range; ZERO independent jal/recomp-C callers of any tail (verified: only
    # `func_80078E14(rdram` appears, funcs_8.c audio init); no COP0/MMIO in any fragment.
    "func_80078E14": (0x180, ["func_80078E80", "func_80078E98", "func_80078EF0", "func_80078F28", "func_80078F88"]),
    # W134 (2026-07-04, #53): func_80076EE4 (runtime 0x800762E4) = the SECOND sound-player
    # constructor -- the SFX player (object D_8008C1AC = static 0x800CF940) whose voice handler is
    # runtime 0x80075D9C (materialized in the func_80076FBC tail as `%lo(func_80075D80+0x1C)`).
    # ares title dump ground truth: real HW registers TWO players with the synthesizer (node
    # 0x800CF998 handler 0x80077A7C -> next 0x800CF940 handler 0x80075D9C); the port registered
    # only the first. This head was auto-added by add_missing_call_targets (jal 0x800762E4 from
    # game sound init func_80066D48, a0=*(D_8008C1AC)) and auto-sized 0xD8 -- exactly cutting off
    # the func_80076FBC tail that does node setup + alSynAddPlayer + the cmd-5 priming post. Real
    # entry is 0x76EE4 (the `addiu $sp,+0x108` at 0x76EE0 is the PREVIOUS function's severed
    # epilogue); real extent 0x76EE4..0x77018 = 0x134, epilogue `jr $ra; addiu $sp,0x48` matching
    # the head's -0x48 prologue. func_80076FBC has zero independent jal/recomp-C callers.
    "func_80076EE4": (0x134, ["func_80076FBC"]),
    # W134 (2026-07-04, #53): func_8007699C (runtime 0x80075D9C) = the SFX-player VOICE HANDLER
    # itself -- the function pointer constructor B registers (see func_80076EE4 above; ares title
    # dump shows 0x80075D9C at node+0x8 of player 0x800CF940). splat shredded it into TEN
    # fragments: the carved func_8007699C is ONLY the 9-instruction prologue (`addiu $sp,-0x108`;
    # saves ra@0x44/fp/s7..s0/f20@0x18), and the matching epilogue's `jr $ra` sits at splat
    # 0x80076EDC with its `addiu $sp,+0x108` delay slot glued onto the NEXT carving at 0x76EE0
    # (which is why constructor B's real entry is 0x76EE4). Without this merge the emitted
    # prologue-only body moved $sp by -0x108 and returned, so the FIRST dispatched handler call
    # unbalanced the caller's stack and alAudioFrame SIGSEGV'd reading its own spilled client
    # pointer (measured W134: crash at 0x800790F0 with *(sp+0xB4)=0 right after the handler's
    # first call). Extent 0x24+0x50+0x50+0x54+0x80+0xE0+0x88+0x12C+0xA8+0x70+4 = 0xA48. All nine
    # absorbed tails: zero jal/recomp-C callers, zero COP0, all ALREADY in force_stub.txt from
    # W133's warning-gated bulk (they were the "splat mis-splits needing SPLIT_MERGES later") --
    # they STAY there as standalone-decode neutralizers.
    "func_8007699C": (0xA48, ["func_800769C0", "func_80076A10", "func_80076A60", "func_80076AB4", "func_80076B34", "func_80076C14", "func_80076C9C", "func_80076DC8", "func_80076E70"]),
    # W134 (2026-07-04, #53): func_8007FDCC (runtime 0x8007F1CC) = the audio library's OWN
    # event-post ("alEvtqPostEvent"), UN-ROUTED from the native osSendMesg (entry removed from
    # LIBULTRA_NAMES below, same edit). Verified from bytes: it is NOT an osSendMesg -- it
    # early-outs when *(q+0) == 0, then does a PURE-RDRAM priority-sorted list insert (unlink
    # free node via runtime 0x80078C90, memcpy the 0x10-byte event via runtime 0x80085D90,
    # delta-sorted insert) into the SAME evtq list the (real, emitted) nextEvent func_8007FEF0
    # pops -- no thread tables, no OSMesgQueue. All 21 of its call sites are audio-library
    # functions posting to custom evtq structs (sndp setters/play, handler internal posts, boot
    # cmd-0x11 poster func_800796F0 targeting player+0x48); the REAL libultra osSendMesg copy
    # (runtime 0x80074EE0, 17 OS/scheduler/PI call sites) is untouched. Routed native (ORCH17),
    # every audio event-post hit ultramodern do_send against a non-OSMesgQueue (msgCount=0) and
    # returned -1 -- measured W134: alSndpPlay posted 21x, every send failed, voice-start 0x.
    # The busy-polling worker (W97 cooperative yield) picks list inserts up without any native
    # wake. Extent 0x4C+0x54+0x88 = 0x124 (matches the alias's old size), epilogue at splat
    # 0x8007FEE4 `jr $ra; addiu $sp,0x30` matching the -0x30 prologue; back-branch .L8007FE68
    # internal to the span. func_8007FE6C STAYS in force_stub.txt (standalone body branches
    # outside its own range -- the stub neutralizes standalone decode only, same pattern as
    # func_80078F28); func_8007FE18 emits standalone harmlessly (zero callers).
    "func_8007FDCC": (0x124, ["func_8007FE18", "func_8007FE6C"]),
    # W135 (2026-07-04, #53): func_80077E5C (runtime 0x8007725C) = the audio library's MIDI/BGM
    # EVENT DISPATCHER -- the jtbl_8008EDB0 switch on (status&0xF0)-0x80 (MIDI note-off 0x80 ..
    # system 0xE0) that handler A func_8007867C jal's 2x. The boot cmd-0x11 handshake
    # (func_800676B4 busy-wait on obj->0x2C) routes through it; while force-stubbed the case
    # body never ran, so under the real event-post (LAMBO_REAL_AUDIO_POST=1) the ack never
    # cleared and boot stuck at state 6 (W134 suspect (a), confirmed: the emitted body was `;}`).
    # splat carved the real 0x818 body into NINE pieces; the head's claimed 0x244 ends mid-body
    # (falls through into func_800780A0, whose first real instruction uses live $t7/$s0 with no
    # prologue). Real extent splat 0x77E5C..0x78674: single matching epilogue at .L80078664
    # (`lw $ra,0x24($sp); addiu $sp,+0xE0; jr $ra` vs the head's `addiu $sp,-0xE0`, ra@0x24).
    # All branch targets internal (min .L80077F4C, max .L80078668); zero COP0/MMIO in the span;
    # all six absorbed tails have ZERO jal/recomp-C callers (verified across RecompiledFuncs).
    # KEPT as overlapping entries, NOT absorbed: func_80078214 (graveyard W127 -- stays a stub,
    # overlap-proven pattern) and func_800785B8 (its 0xC4 shrink entry above; contains only this
    # function's epilogue + a stray `jr $ra; nop` at 0x78674).
    "func_80077E5C": (0x818, ["func_800780A0", "func_800781A4", "func_800783BC", "func_800783D8", "func_800783E8", "func_80078434"]),
    # W135 (2026-07-04, #53): func_800804C0 (runtime 0x800798C0) = the VOICE-EVENT POSTER the (now
    # real) MIDI dispatcher jal's on note arms: allocates an event node (jal func_80078FA0), tags
    # it type 0x10 (the handler-A voice-event arm), computes the sample deadline, then `jalr` the
    # registered voice handler with a1=3 (the kick that starts a voice -- the last missing link
    # between MIDI note-on and audible synthesis; voice-start func_80067FB4 measured 0x while this
    # was stubbed). splat cut it at 0x60 mid-body (branches to .L80080524/48 in the func_80080520
    # carving) -> force-stub. Real extent 0x804C0..0x80554 = 0x94, epilogue `jr $ra; addiu $sp,
    # +0x18` matching the -0x18 prologue; all branches internal; zero COP0/MMIO; the absorbed tail
    # func_80080520 has zero jal/recomp-C callers.
    "func_800804C0": (0x94, ["func_80080520"]),
    # W136 (2026-07-04, #53): func_80080040 (runtime 0x8007F440) = the VOICE ALLOC/STEAL SEARCH the
    # voice-start func_80080128 (runtime 0x8007F528, emitted real) jal's with a1=&outVoice on its
    # stack. Walks the synth's voice lists (free head +0x14, active +0x4, steal-by-priority
    # lh +0x16 vs the note's priority) and writes the chosen voice to *a1. While stubbed the
    # out-param stayed the prologue's zero -> func_80080128 always took its "no voice" branch ->
    # ZERO voices ever started (the W135 ACMD gap: ares's 0x1/0x3/0x5 ADPCM/ENVMIXER/RESAMPLE
    # trios missing; every func_800804C0 post early-outing on slot+0x8==0). Found live: ares Z2
    # write-watch on 0x80109A68 at music start, EPC/ra candidates runtime 0x80075E68 = the return
    # address of the handler arm's jal 0x8007F528, whose sole voice-writing callee chain is this
    # function. splat capped the head at 0xA0 (ending at 0x800800E0), severing the epilogue: the
    # forward branches to .L80080114/118 land in the func_800800E0 carving -> "branching outside"
    # -> force-stub (and W133's bulk un-stub was silently undone by force_stub.txt precedence).
    # Real extent 0x80040..0x80128 = 0xE8, epilogue `lw $ra,0x14; addiu $sp,+0x28; jr $ra`
    # matching the -0x28 prologue; all branch targets internal (min .L80080094, max .L80080118);
    # zero COP0/MMIO; callees runtime 0x78C90/0x78CC0 (splat func_80079890/func_800798C0) both
    # emitted real. Absorbed tail func_800800E0 has zero jal/recomp-C callers; it STAYS in
    # force_stub.txt as a standalone-decode neutralizer (W134 pattern).
    "func_80080040": (0xE8, ["func_800800E0"]),
    # W136 (2026-07-04, #53): func_800819BC (runtime 0x80080DBC) = the voice DECODER pull handler
    # (ADPCM/raw16), the DEEPEST source in the per-voice filter chain the synth registers at
    # voice-start (live chain walk at the crash: envmixer 0x80081C6C <- resampler 0x8008134C <-
    # decoder 0x80080DBC, source=0). While emitted-empty its vtable dispatch returned leftover $v0
    # as the "updated ACMD cursor" -> the resampler stored ACMD words through garbage (0x2e1c0) ->
    # SIGSEGV in func_80081F4C on the first started voice. splat capped the head at 0x200 (ending
    # 0x80081BBC) but the body branches to .L80081E2C -> "branching outside" -> race-stubbed.
    # Real extent 0x819BC..0x81E5C = 0x4A0: single epilogue at .L80081E2C restoring exactly the
    # prologue's saves (ra@0x4C, s0-s7@0x28-0x44, fp@0x48, sp+0xB0 vs -0xB0). All branch targets
    # internal; zero jalr/COP0/MMIO; sole jal callee runtime 0x80080C90 (splat func_80081890,
    # emitted real -- the sample-window fetch). Absorbed tail func_80081BBC: continuation fragment
    # (opens on live-register sltu, no prologue), zero jal/recomp-C callers.
    "func_800819BC": (0x4A0, ["func_80081BBC"]),
    # func_800028D0 (runtime 0x80001CD0) is the boot command-loop CONTINUATION that dispatches the
    # game state machine -- the per-frame-render blocker measured live 2026-06-26. splat capped it
    # at 0x6C8 (ending exactly at the stage3/menu ROM boundary 0x2F98); spimdisasm sizes it 0x828
    # (asm/func_800028D0.s, `nonmatching func_800028D0, 0x828`). With the short size it branched to
    # 0x80002464/6C/80 -- targets inside the spuriously-split func_80002F98 -- so n64recomp reported
    # "branching outside" and the function had to be force-stubbed (the prior "needs per-overlay
    # sections" theory; FALSIFIED -- this is a plain split-merge, no overlay machinery). func_80002F98
    # has NO independent jal anywhere in the asm corpus (verified), so absorb it. Un-stubbing
    # func_800028D0 lets the boot's msg=1 command handler advance the state machine past frame 1.
    "func_800028D0": (0x828, ["func_80002F98"]),
    # func_800785B8 (runtime 0x800779B8) is a SMALL leaf the dump over-sized to 0x258 (ending at the
    # next splat segment boundary 0x80078810). Its real body is only 0xC4: it ends with `jr $ra; nop`
    # at splat 0x80078674 (asm/race_full_functions/func_800785B8.s), immediately followed by a NEW
    # prologue `addiu $sp,-0xD0` at 0x8007867C (= func_8007867C, the state-4 indirect-call target).
    # Shrink it to its real size so func_8007867C's prologue is no longer buried inside this stub's
    # claimed range -- the entry at 0x8007867C is registered separately via EXTRA_FUNCS below. No tail
    # to absorb (func_8007867C is a distinct function, registered in its own right, not merged in here).
    "func_800785B8": (0xC4, []),
    # func_800030F8 (runtime 0x800024F8) is the ODD-state game-logic state machine -- the title-screen
    # 6->7 advancer. It owns the unconditional state=7 store at splat 0x80003EA8 (addiu $t5,0x7; sh
    # D_800CE6AC) reached through its inner jump-table dispatch on sub-state D_800CE810. splat fragmented
    # this ONE function (real prologue `addiu $sp,-0xE8` at 0x800030F8, single shared epilogue
    # `jr $ra; addiu $sp,0xE8` at splat 0x80006010) into SIX pieces at segment boundaries; the body
    # branches across them (e.g. the div-by-zero `break 7` guard at 0x80005398 belongs to the `div` at
    # 0x8000537C; the bnez at the end of the 0x80003258 piece targets 0x8000539C inside the next piece).
    # Real size = 0x80006018 - 0x800030F8 = 0x2F20. It is directly jal'd ONCE (from func_800028D0 @
    # runtime 0x80001E80, the even-state render dispatcher) at its prologue -- NOT an indirect/mid-function
    # entry (the handoff's "func_80003258 indirect call" was the +0xC00 mid-label confusion; runtime
    # 0x80002658 has zero jal callers, runtime 0x800024F8 has one). All 5 absorbed tails (func_80003258,
    # func_80005398, func_80005418, func_8000577C, func_80005A24) have ZERO independent jal callers
    # (verified by ROM scan), so dropping them is safe. Its only `ignored` callee, func_80009158 (runtime
    # 0x80008558), is itself over-split (branches outside its own 0x7C8 range) -> force-stubbed to satisfy
    # the link (TRACKER #58: un-split + emit it real next). The state=7 store is UNCONDITIONAL after the
    # jal's, so the stubbed callee does not block the 6->7 advance.
    "func_800030F8": (0x2F20, ["func_80003258", "func_80005398", "func_80005418", "func_8000577C", "func_80005A24"]),
    # boot_pad_apply_calibration (runtime 0x80005A24) is jal'd by the now-real func_800028D0 state
    # dispatch. splat capped its dump size at 0x1BE4 (ending at func_80008208); it really runs to
    # 0x80008E04 (docs/symbol_addrs.txt) -- size 0x27E0 -- absorbing the jump-table tail. With the
    # short size it branched to 0x80007D8C/765C/762C ("branching outside"). func_80008208 IS an
    # independent jal target (race loop) and stays a kept STUB, so we grow the head WITHOUT dropping
    # it (the proven "grow head, keep tail stubbed" overlap pattern). Emitted via FORCE_EMIT below.
    "boot_pad_apply_calibration": (0x27E0, []),
    # func_80075C60 (runtime 0x80075060) is osViSetMode -- verified from bytes (writes
    # __osViNext(D_8008D1A4)->modep(+0x8), state(+0x0)=1, control(+0xC)=modep->comRegs.ctrl(+0x4))
    # and from live gdb (W115, 2026-07-02): jal'd from osCreateScheduler (func_80074B90 @ splat
    # 0x80074C24) with a0=&osViModeTable[4]=0x8008C4A0 (OS_VI_NTSC_LPN2, 32bpp) at boot, then from
    # the game SM (func_80038498 @ splat 0x800384D4/EC) with &osViModeTable[6]=0x8008C540
    # (OS_VI_NTSC_LAN2) -- exactly the modep the ares title RDRAM dump holds. splat fragmented the
    # 0x68-byte body (jr at splat 0x80075CC4) into head 0x10 + func_80075C70 (0x40, race stubs) +
    # func_80075CB0 (0x18, race ignored); the empty head meant BOTH osViSetMode calls were no-ops,
    # __osViNext->modep stayed 0 forever, and ultramodern/RT64 scanned out on the dummy-mode
    # fallback (the #58 black-present root). 5 jal callers in the ROM all target the head at
    # 0x80075060; ZERO independent jals into either tail (verified by ROM scan). #58.
    "func_80075C60": (0x68, ["func_80075C70", "func_80075CB0"]),
    # func_8008286C (runtime 0x80081C6C) is the TITLE-SCREEN leaf DL emitter -- the per-child node
    # method invoked 32x by the container's vtable loop func_80082DF0 (which is reached from the title
    # event-scheduler drain func_80079BD8 -> func_80082F04 -> func_800811F0 -> func_8008733C). While
    # force-stubbed it returned the stale 0x140 (a count, a2<<1) instead of an advanced DL-buffer
    # pointer, so func_8008733C wrote GBI words to 0x140 and SIGSEGV'd at state 6 (live-measured
    # 2026-06-27, native gdb on the pivot binary). The dump over-split ONE shared-code blob
    # [0x8008286C, 0x80082DB4) into three independently-entered functions: 0x8008286C (vtable, our
    # path), 0x80082AEC (jal'd 3x, kept STUB), 0x80082C84 (jal'd 2x, already real, spans to the blob
    # end). func_8008286C's branches reach 0x80082D44 (inside func_80082C84), so with its short 0x280
    # size they "branch outside" -> the force-stub. Grow it to span the whole blob (0x80082DB4 -
    # 0x8008286C = 0x548) so every branch resolves internally; absorb NO tails -- func_80082AEC
    # (kept stub) and func_80082C84 (kept real) stay as their own jal-target symbols, the proven
    # "grow head, keep tail" overlap pattern (boot_pad_apply_calibration above). Its only stubbed
    # callee, func_80081924 (a self-contained 0x98 leaf), is un-stubbed alongside (removed from the
    # race stubs list); the other callee func_80081BBC is already HLE'd. TRACKER #58.
    "func_8008286C": (0x548, []),
    # func_80011F70 (runtime 0x80011370) is the demo-race actor-system HEADER writer (sets D_800B69A8/
    # AC/AE, the control fields the actor-table populator func_80038D6C reads). splat capped it at 0x30C
    # (ending 0x8001167C); the body branches to runtime 0x80012268 (splat 0x80012E68) -> n64recomp
    # "Unhandled branch ... at 0x8001139C to 0x80012268". Real body runs to the shared epilogue
    # `jr $ra` at runtime 0x80012590, i.e. through 0x80012598 (func_80013198 is the NEXT function) ->
    # size 0x80012598 - 0x80011370 = 0x1228. Absorbed tails func_8001227C, func_800125A8, func_80012DDC
    # all have ZERO independent jal callers (verified). The lone interior jal target func_80012E7C
    # (runtime 0x8001227C, jal'd 1x from func_80029DCC) overlaps the tail end -> kept as a force-stub
    # (the proven "grow head, keep tail stubbed" overlap pattern, like boot_pad_apply_calibration's
    # func_80008208). Pure-RDRAM, no COP0/MMIO. TRACKER #64 (state 7->8 demo-race actor table).
    "func_80011F70": (0x1228, ["func_8001227C", "func_800125A8", "func_80012DDC"]),
    # func_80083F70 (runtime 0x80083370) is the VALIDATOR -- the lone stubbed callee of the audio/object
    # registration gate func_8007A8A0 (runtime 0x80079CA0). The W89/W90 handoff claimed "7 of 9 gate
    # callees are stubs"; that was the +0xC00 SPLAT-SHIFT TRAP applied to its OWN list -- it listed the
    # gate's RUNTIME jal-target names (func_8007EB10, func_80082500, ... func_80083370) and checked their
    # SPLAT-named twins. Re-checking the gate's ACTUAL bound callees (the recomp C: func_8007F710,
    # func_80083100, func_8007F754, func_80083EFC, func_8008326C, func_800836EC, func_800832D4,
    # func_80083F70, func_80084300) shows 8 of 9 are already EMITTED-REAL; func_80083F70 is the ONLY stub.
    # splat over-split it into THREE pieces: prologue func_80083F70 (0x8, `addiu $sp,-0x60` @ 0x80083F70),
    # body func_80083F78 (0x1E8), epilogue tail func_80084160 (0x2C, holds the matching `addiu $sp,0x60;
    # jr $ra` @ 0x80084180, ending 0x8008418C). Real size = 0x8008418C - 0x80083F70 = 0x21C. With the 0x8
    # size its body branched outside -> stubbed. All body branches are internal (verified .L80083FE4 ..
    # .L8008416C in [0x80083F70,0x8008418C)); both absorbed tails have ZERO independent jal callers
    # (verified). The validator is called from ~20 sites; while a no-op stub the gate's validation step
    # never ran. Removed from the race stubs list in the same change. TRACKER #64/#53.
    "func_80083F70": (0x21C, ["func_80083F78", "func_80084160"]),
    # func_80069710 (runtime 0x80068B10) is the OBJECT-SLOT REGISTRATION driver -- the populator of the
    # active-object array D_80110F08[] whose emptiness keeps the per-frame producer func_8006A7A0
    # (runtime 0x80069BA0) dormant and the boot stuck at state 6 (W91, ares-grounded: D_80110F08[] has
    # persistent non-zero slots 1/5/18/19.. in EVERY ares dump but is ALL-ZERO in every port dump).
    # ROOT CAUSE (2026-06-29): splat split this ONE function (prologue `addiu $sp,-0x38` @ 0x80069710,
    # single shared epilogue `addiu $sp,0x38; jr $ra` @ 0x80069E70) into SIX pieces at segment
    # boundaries. func_80069710's own tail sets up `$at = 0x80110000 + idx*4` and `$t0 = 1` as its LAST
    # two instructions (0x800698B0/B4) -- and the VERY NEXT instruction, the actual write
    # `sw $t0, 0xF08($at)` (= D_80110F08[idx] = 1), is the FIRST instruction of the next piece
    # func_800698BC, which was force-/race-stubbed (EMITTED-EMPTY). So the recomp ran func_80069710,
    # set up the write, then RETURNED -- the D_80110F08 write, the gate call `jal func_8007A8A0`
    # @0x80069A20, and the gate-return object-setup jump table jtbl_8008E9C0 ALL live in the severed
    # stubbed pieces and never executed -> the slot array stayed empty -> producer dormant -> state 6.
    # Real size = 0x80069E7C - 0x80069710 = 0x76C. All 5 absorbed tails (func_800698BC, func_80069A84,
    # func_80069BA0, func_80069C2C, func_80069CB4) have ZERO independent jal callers (ROM scan by runtime
    # name) and all 32 internal branch labels resolve inside [0x80069710, 0x80069E7C). NOTE the splat-shift
    # decoys: the absorbed tail splat-named func_80069BA0 is at RUNTIME 0x80068FA0 -- it is NOT the
    # producer (producer = splat func_8006A7A0 @ runtime 0x80069BA0, outside this range); likewise the
    # `jal func_80069C2C` inside this body targets RUNTIME 0x80069C2C = splat func_8006A82C, a separate
    # real function, not the absorbed tail splat func_80069C2C. TRACKER #64/#53.
    "func_80069710": (0x76C, ["func_800698BC", "func_80069A84", "func_80069BA0", "func_80069C2C", "func_80069CB4"]),
    # func_80064980 (runtime 0x80063D80) is a helper called by func_80060464 (runtime 0x8005F864,
    # the STATE-8 demo-race per-participant pairwise loop) inside its inner loop at runtime 0x8005FA9C.
    # splat OVER-SPLIT it: the head func_80064980 (splat 0x80064980) ends mid-computation at splat
    # 0x800649F8 (`addu $t7,$t9,$t6`) with NO `jr $ra` -- truncated BEFORE its epilogue. The recompiled
    # head does `addiu $sp,-0x58` at entry (0x80063D80) but never the matching `addiu $sp,0x58; jr $ra`,
    # so on return it leaves the SHARED ctx->r29 (MIPS sp) decremented by 0x58. Every subsequent
    # 0xC4(sp)/0xBC(sp) access in the caller is then off by 0x58 -> the outer participant index r8 =
    # 0xC4(sp) reads a stale float (0xc660f6f8) -> the strided store MEM_W(0x800CE8DC + 0xE0*r8) lands
    # ~350MB out of bounds -> deterministic SIGSEGV at funcs_6.c:4907 (live-measured, native gdb on the
    # pivot binary 2026-06-30; same crash sp=0x80095be0/r8=0xc660f6f8 every run). Real body runs to the
    # shared epilogue `lw $ra,0x24($sp); addiu $sp,0x58; jr $ra` at splat 0x80064DD0-0x80064DDC -> size
    # 0x80064DE0 - 0x80064980 = 0x460. Absorbed tails func_800649FC, func_80064A78, func_80064B10 all have
    # ZERO independent jal callers (verified: 0 recomp-C call sites) and start mid-computation
    # (`sw $t4,0x8($sp)` / `lw $a2,0x8($sp)` / `bc1f`) -> pure over-split fragments. Pure-RDRAM, no
    # COP0/MMIO. TRACKER #64/#53 (state-8 demo-race scene).
    "func_80064980": (0x460, ["func_800649FC", "func_80064A78", "func_80064B10"]),
    # func_8005E86C (runtime 0x8005DC6C) is a second state-8 demo-race helper with the SAME truncation
    # bug as func_80064980 (W99). func_8005E660 (the demo-race per-frame driver) calls it (line 242)
    # BETWEEN its func_80060464 call (line 75, now fixed) and its func_80060DE4 call (line 347). splat
    # capped func_8005E86C at splat 0x8005E924 -- its emitted head ends after `jal func_80064F64; sub.s`
    # (delay slot) at splat 0x8005E920, BEFORE its `addiu $sp,0x40; jr $ra` epilogue. So it does
    # `addiu $sp,-0x40` at entry but never restores -> returns to func_8005E660 with the SHARED sp
    # decremented by 0x40 -> func_80060DE4 then reads its loop index 0xB6C(sp) (init 0) at the wrong
    # offset, gets a stale float (0xc1241bc6) and SIGSEGVs on the strided load 0x800CF650+0x1C*idx
    # (funcs_6.c:6052, live-measured deterministic). Real body runs to the shared epilogue at splat
    # 0x8005EBEC -> size 0x8005EBF4 - 0x8005E86C = 0x388 (prologue/epilogue both 0x40; body branches reach
    # .L8005EB64, inside the grown range). The lone interior sub-symbol func_8005E924 (runtime 0x8005DD24)
    # is jal'd 1x and stays its existing kept STUB -- grow head, keep tail stubbed (the boot_pad_apply_
    # calibration / func_8008286C overlap pattern). Pure-RDRAM (no COP0/MMIO). TRACKER #64/#53.
    "func_8005E86C": (0x388, []),
    # func_80062854 (runtime 0x80061C54) is the THIRD state-8 demo-race truncated callee,
    # found by scanning the ACTUAL recompiled C (not the stale splat .s dump): every
    # `RECOMP_FUNC` body in recomp/RecompiledFuncs whose prologue decrements ctx->r29 but
    # never restores it anywhere in the body. Unlike func_80064980/func_8005E86C, splat
    # never gave this address its OWN glabel at all -- it sits entirely inside the combined
    # dump chunk splat named func_800626C8 (asm/race_full_functions/func_800626C8.s), right
    # after that chunk's *unrelated* `jr $ra; addiu $sp,0xB70` epilogue at splat 0x8006284C/
    # 0x80062850 (which is func_80060DE4's OWN correct epilogue -- func_80060DE4 is sized via
    # the `func_80060DE4` function_sizes entry above and is NOT truncated). n64recomp's
    # add_missing_call_targets only discovered this address because func_80060DE4 calls it
    # directly twice (funcs_6.c, both inside func_80060DE4's per-participant r16-switch loop)
    # -- it auto-sized it to the gap before the next KNOWN start (func_80062AF4, splat-dump
    # real, 0x10 bytes), 0x10 bytes SHORT of the real end, so the emitted body just fell off
    # the end of the C function with sp left -0x10 forever -> every subsequent ctx->r29-relative
    # read in func_80060DE4's SAME native-thread call chain (incl. its own 0xB6C(sp) loop
    # index) is silently corrupted -> wild load -> SIGSEGV at funcs_6.c:6052 (live-measured,
    # native gdb on the pivot binary 2026-06-30, deterministic). func_80062AF4
    # (asm/race_full_functions/func_80062AF4.s) IS the missing epilogue: `b .L80062AFC; nop;
    # jr $ra; addiu $sp,0x10` -- a pure shared-epilogue trampoline with ZERO independent jal
    # callers (verified: 0 recomp-C call sites) -> absorb it. Real size = 0x80062B04 -
    # 0x80062854 = 0x2B0. Because this head has NO dump entry, it must first be ADDED via
    # tools/n64recomp_race.toml's function_sizes override (apply_merges' "absent from dump"
    # path) at the matching size 0x2B0 -- the SPLIT_MERGES entry here re-applies that same
    # size (no-op) and does the part apply_merges can't: drop the absorbed tail. Pure-RDRAM,
    # no COP0/MMIO. TRACKER #64/#53.
    "func_80062854": (0x2B0, ["func_80062AF4"]),
    # func_80009158 (runtime 0x80008558) is the per-frame ATTRACT/DEMO SUB-MODE SEQUENCER -- a pure-
    # RDRAM zero-jal leaf (no COP0/MMIO) called UNCONDITIONALLY each states-7/8 frame by the game-logic
    # SM func_800030F8 (runtime jal 0x80008558 @ 0x80002B80 -- its ONLY caller by ROM jal scan). Its asm
    # is dominated by the attract sub-mode cell D_800CE6AA (48 refs) plus reads of D_800CE6B4 (demo
    # mode=4), D_800CE6A4, D_800CE7AC, D_800CE808, D_800CE818 and writes to D_800986D8/D_800A5EE8.
    # splat split the ONE function into THREE pieces (0x7C8 + 0x1A0 + 0xA60 = 0x13C8, contiguous,
    # syms lines 31-33); with the short 0x7C8 size it branched outside -> force-stubbed at the
    # func_800030F8 merge (9a255ba, "tracked: un-split + emit real next" -- this is that un-split).
    # Both absorbed tails (func_80009920, func_80009AC0) have ZERO independent jal callers (ROM scan).
    # W114 context: the state-7 demo-LOAD path measured FAITHFUL on the port (identical idx/mode/tables
    # vs ares; per-idx replay+track+sky DMA matches the ROM's own file table at 0x899C0), so the
    # remaining attract divergence suspects are the stubbed PER-FRAME arm callees -- this is the
    # largest (0x13C8) and shallowest (depth 1). Removed from force_stub.txt + FORCE_EMIT'd (it is
    # race-`ignored`). TRACKER #53/#54.
    "func_80009158": (0x13C8, ["func_80009920", "func_80009AC0"]),
    # func_800152BC (runtime 0x800146BC) is the SECOND unconditional per-frame callee of the states-7/8
    # game-logic SM func_800030F8 (runtime jal 0x800146BC @ 0x80002B98 -- its ONLY caller by ROM jal
    # scan). Touches D_800986A0 (14 refs) / D_8009869C / D_800CE81A; its only jal is func_80074B20
    # (force-stubbed 0x28 leaf -- partial fidelity until that blob is un-split, tracked). splat split
    # the ONE function into THREE pieces (0x2F0 + 0x450 + 0x494 = 0xBD4, contiguous to func_80015E90
    # @ runtime 0x80015290). Both absorbed tails (func_800155AC race-stub, func_800159FC force-stub)
    # have ZERO independent jal callers (ROM scan) -> absorb fully; func_800159FC also removed from
    # force_stub.txt (symbol dropped). Un-stubbed via UNSTUB (it is race-`stubs`). TRACKER #53/#54.
    "func_800152BC": (0xBD4, ["func_800155AC", "func_800159FC"]),
    # func_80079F10 (runtime 0x80079310) is the AUDIO COMMAND BUILDER -- one of the two remaining
    # cascade-head stubs (the producer for the per-frame audio scheduling path). splat capped it at
    # 0x168 (splat 0x8007A078, end of head .s file) but the body has a backwards loop branch into
    # the NEXT chunk: .L8007A0CC `bnez $at, .L8007A030` (head) targets 0x8007A030 -- inside the tail
    # func_8007A078 (asm/race_full_functions/func_8007A078.s, splat-size 0xF8, holds the loop body +
    # the matching `jr $ra; addiu $sp, 0x78` epilogue at splat 0x7A168/6C). With the short size the
    # backwards branch + the 0x8007A0D8 forward targets all "branch outside" -> the function is
    # race-stubbed (currently emits `recomp_stub_hit`). The tail func_8007A078 has ONE independent
    # caller outside this split -- func_8006A7A0 at splat 0x6A7E8 (`jal func_8007A078`, the
    # per-frame audio producer func_8006A7A0 reaches into the loop body directly), so the tail
    # CANNOT be absorbed: it must stay as its own emitted real entry. Real size = 0x8007A170 -
    # 0x80079F10 = 0x260. This is the proven "grow head, keep tail as its own real entry" overlap
    # pattern (boot_pad_apply_calibration func_80008208, func_8008286C func_80082C84 above). The
    # body is pure-RDRAM (jal func_80075CB0 allocator, sw/slt arithmetic, jal func_8007FE18
    # priority-enqueue, jal func_8007FDC4 tail-call epilogue, plus the inner-loop jals func_80078CC0/
    # func_8007FEF4/func_80080710/func_8007FE6C/func_80081260/func_8007FF9C/func_80081674/func_800821C0),
    # no COP0/MMIO -> safe to recompile. Removed from the race stubs list via UNSTUB. TRACKER #53 PR 2.
    "func_80079F10": (0x260, []),
    # func_8004F5F0 (runtime 0x8004e9f0) is the menu CONFIRM/OK-press handler for the descriptor-
    # dispatched dialog screens -- the function that fires when the player presses OK on the
    # "IF YOU WANT TO USE ANY RUMBLE PAKS, PLEASE INSERT THEM NOW." screen (menu screen id 31, #69).
    # It switches on the dialog sub-state pair D_800985F2 (jtbl_8008E47C, 6 cases) / D_800985F4
    # (jtbl_8008E494, 0x20 cases), and its cases call ONLY emitted-real code: the screen-request API
    # (encoded jal 0x800377A8 -> pivot func_800383A8), the screen-advance driver (jal 0x8004EFDC ->
    # pivot func_8004FBDC, which reaches the pak-scan callback func_8004ED64 -> scan func_80069710),
    # and jal 0x8004F254/0x8004F674 -> pivot func_8004FE54/func_80050274. splat split the ONE
    # function (prologue `addiu $sp,-0x18` @ 0x8004F5F0, single epilogue `jr $ra; addiu $sp,0x18` @
    # splat 0x8004FBD4) into FOUR pieces; both jump tables and plain branches cross the seams
    # (e.g. chunk 1's jtbl_8008E47C targets 0x8004EAxx inside chunk 2; chunk 3 branches to
    # .L8004FBA4/.L8004FBCC inside chunk 4), so the head was race-stubbed and the press dispatched
    # to an EMPTY body -- measured live (gdb, headless START pulses 2026-07-03): screens walk
    # -1->1->2->11->13->21->31 then every press hits stub func_8004F5F0 and dies; no scan, no screen
    # request, stuck exactly as reported in #69. Real size = 0x8004FBDC - 0x8004F5F0 = 0x5EC. All
    # branch targets internal to [0x8004F5F0, 0x8004FBDC) (verified, incl. both jump tables); all 3
    # absorbed tails have ZERO independent jal callers AND zero ROM data refs (the head 0x8004e9f0
    # itself has 4 descriptor-table data refs -- it is the indirect entry and stays). Pure RDRAM,
    # no COP0/MMIO. func_8004F674 (race-`ignored`) and func_8004FAC8 (race-`stubs`) drop out via the
    # present-names filter; func_8004FB78 was emitted-real but is only reachable as this function's
    # tail (0 jal callers, 0 data refs). TRACKER #69.
    "func_8004F5F0": (0x5EC, ["func_8004F674", "func_8004FAC8", "func_8004FB78"]),
    # --- Controller-Pak SAVE path (#35): the osPfs file-operation layer was entirely force-stubbed
    # because splat carved every function into contiguous fragments whose branches cross the seams
    # ("branch outside" -> iterate_stubs). The save flow (records screen 30/31 -> confirm) dies in
    # the FIRST of these: the save gate func_80069E7C (runtime 0x8006927C, emitted real) does
    # `v0 = func_8007B780(pfs)` -- an empty stub leaves v0 as leftover garbage, so the game takes
    # the error path and the screen reverts without ever issuing a pak write (the traced read-loop
    # is the surviving real code re-polling around the dead layer). For every head below: all
    # branch targets verified internal to the merged span, every absorbed tail has ZERO independent
    # jal callers (whole-ROM jal scan) and ZERO stored-pointer refs in the recompiled corpus
    # (direct 0X<lo16> and negative-addiu forms), bodies are pure RDRAM + jal (no COP0/MMIO), and
    # every jal callee is emitted real (func_80083F70/func_80084EE0 read/write primitives,
    # func_80083100/func_8008326C/func_80083210/func_80083884/func_80083AE0/func_80083EFC helpers,
    # SI access pair func_8007F710/func_8007F754). TRACKER #35.
    "func_8007B780": (0x25C, ["func_8007B824"]),                                    # save-gate callee (jal 0x80069288)
    "func_8007B9E0": (0x400, ["func_8007BA88", "func_8007BB50", "func_8007BC4C"]),  # file read/write (jal'd by save writer func_80069ED8)
    "func_8007BFA0": (0x484, ["func_8007C050", "func_8007C330"]),                   # pak-scan callee (jal 0x800697a4)
    "func_8007C424": (0x264, ["func_8007C568", "func_8007C660", "func_8007C670", "func_8007C680"]),  # jal'd from inside func_8007BFA0's span; 0x264 includes its jr;nop epilogue so func_8007C680 (the bogus symbol straddling the C424/C688 boundary, zero jal callers) is absorbed cleanly
    # func_8007C688 (runtime 0x8007BA88) is the FAT-chain "get next page" helper jal'd from inside
    # func_8007C424's walk loop. splat truncated it to 0x8 (just `addiu sp,-0x28; sw a0`) and split
    # the remainder off as a bogus head func_8007C690 (which starts mid-prologue at `sw a3,0x34(sp)`,
    # NOT a real function start -- zero jal callers). While func_8007C424 was force-stubbed the call
    # never fired; un-stubbing it activated a call into the 8-byte truncated body that ran off the end
    # with no `jr ra`/sp-restore, leaving guest sp at -0x28 -> the caller's next stack read (a counter
    # out-param) came back NULL -> SIGSEGV in the records-screen pak scan (#35, gdb-confirmed). Real
    # size = 0x8007BB48 - 0x8007BA88 = 0xC0; absorbs the mislabeled tails func_8007C690 + func_8007C6E0
    # (both zero independent jal callers, zero data refs). All branches internal, pure RDRAM + jal.
    "func_8007C688": (0xC0, ["func_8007C690", "func_8007C6E0"]),
    # func_80069ED8 (runtime 0x800692D8) is the game-side SAVE-RECORDS writer: calls the pak scan
    # func_80069710 then the file writer func_8007B9E0. Reached by menu-descriptor dispatch (zero
    # jal callers), so its symbol must stay registered; splat split off its 0x24 epilogue chunk.
    "func_80069ED8": (0x6C, ["func_80069F20"]),
    # Issue #32: func_8006CEC8 (runtime 0x8006C2C8) is the MENU 3D-OVERLAY EMITTER -- reads the DL
    # cursor D_800A39CC and appends SETPRIMCOLOR + 4x MTX + gSPDisplayList(model) + POPMTX per
    # element; at the car-select screen these draw the red cursor-highlight ring around the active
    # button (model DL 0x8013C658 x4, asset verified loaded at the identical address in the port),
    # all present in an ares DL walk of the same screen and absent from the port's. splat capped it at 0x7A4
    # (ending mid-body at 0x8006CA6C, severing the last jal + epilogue into the bogus 0x54-byte tail
    # func_8006D66C which starts on live-register math, not a prologue) -> "branch outside" ->
    # force-stubbed -> empty body -> no cursor/arrows on every pre-race menu screen. Real extent
    # runtime 0x8006C2C8..0x8006CAC0 = 0x7F8, epilogue `lw $ra,0x2C; addiu $sp,+0x58; jr $ra`
    # matching the -0x58 prologue; all branch targets internal (whole-span scan); zero COP0/MMIO;
    # sole jal callee runtime 0x80070E10 (func_80071A10) emitted real with 21 existing call sites;
    # absorbed tail has ZERO independent jal callers and ZERO data refs (whole-ROM scan). Both
    # callers (runtime 0x8006CC58/0x8006CE38, inside func_8006D6C0/func_8006D8A4) emitted real.
    "func_8006CEC8": (0x7F8, ["func_8006D66C"]),
    # Issue #32 (part 2): func_8004AFD8 (runtime 0x8004A3D8) draws the menu's L/R SELECTION-ARROW
    # texrects (the `<<||`/`||>>` pairs flanking the car-select SELECT row at fb x=75/213 y=170,
    # present in an ares DL walk, absent from the port's) -- a leaf 2D-element blitter taking
    # sign-extended halfword args and appending SETTILE-CI4/SETTILESIZE/TEXRECT runs at the DL
    # cursor D_800A39CC. splat capped it at 0x498 (ending mid-emission at 0x8004A870 with no
    # epilogue) and carved the remainder as a bogus "func_8004B470" head that starts on live
    # registers mid-store; four branches from the head land in that carving -> "branch outside" ->
    # race-stubbed -> empty body -> no arrows. Real extent 0x8004A3D8..0x8004B470 = 0x1098,
    # epilogue `jr $ra; addiu $sp,+0x130` matching the -0x130 prologue; ALL branch targets internal
    # (whole-span scan); a LEAF (zero jal), zero COP0/MMIO; the absorbed carving has ZERO jal
    # callers and ZERO data refs (whole-ROM scan). Sole caller runtime 0x80041B50 (func_80042270,
    # real).
    "func_8004AFD8": (0x1098, ["func_8004B470"]),
}

# Functions ABSENT from the combined-ELF dump that a jal targets, so n64recomp invents a
# wrongly-handled `static_` there. We name them and route them to stubs/ignored.
# `cache`-bearing libultra cache routines (osInvalDCache-class; 9 cache insns total, in 3
# functions -- the other two are already in the race stubs) are stubbed: cache ops are PC
# no-ops. Format: (name, splat_vram, size, "stub"|"ignore").
EXTRA_FUNCS = [
    ("func_8007DC40", 0x8007DC40, 0x80, "stub"),  # static_0_8007D040, cache @ 0x8007DC78/9C
    # func_8007F780 (runtime 0x8007EB80, the raw SI controller read) is ignored via the RACE recipe's
    # ignored list (tools/n64recomp_race.toml) -- NOT here: registering it in EXTRA_FUNCS forces a body
    # to be emitted (the real body SIGSEGVs in the raw SI leaf func_80085260), defeating the ignore. The
    # pivot BRIDGE libultra_stubs.c:func_8007F780_recomp is linked instead. See LIBULTRA_NAMES comment
    # for func_8007F780 + #64/#53.
    # func_8007867C (runtime 0x80077A7C) is the state-4 INDIRECT-call target. The boot state machine
    # (func_800028D0, state var halfword @ 0x800CE6AC) reaches state 4 and invokes it via a function
    # pointer (jalr), so the JAL-only add_missing_call_targets scan never registered it -> ultramodern
    # aborts "Failed to find function at 0x80077A7C". It is a real function: prologue `addiu $sp,-0xD0`
    # at splat 0x8007867C, body verified self-contained through its shared epilogue at func_80078C90
    # (`jr $ra; addiu $sp,0xD0` at 0x80078C98), ending at 0x80078CA0 -- real size 0x624. All its
    # internal branches stay within [0x8007867C, 0x80078CA0) (verified: lowest target .L800786BC, highest
    # .L80078C50); func_80078810/868/8B8.. are its alternate-entry pieces, already jal'd separately and
    # registered as their own (stubbed/ignored) overlapping entries by the race recipe.
    #
    # EMITTED REAL (2026-06-27): func_8007867C is the title-screen EVENT-SCHEDULER HANDLER. The state-6
    # plateau was root-caused live (gdb non-perturbing attach): at state 6 the game-logic thread
    # func_80067B98 -> func_80067CF0 -> func_80079BD8 enters an event-drain loop that calls the per-event
    # handler `node->handler` (struct offset +0x8) which == func_8007867C @ runtime 0x80077a7c. While it
    # was a no-op STUB the handler never consumed/advanced its event, so the loop never drained and (under
    # ultramodern's priority scheduler) STARVED the lower-priority state-machine thread -> state/fade/d6
    # frozen, 6->7 never reached. (The handoff's "title phase machine D_80098562 stuck at 0" model was
    # FALSE -- live probe shows phase=-1 like the real ROM; the real blocker was this scaffold stub.)
    # The earlier "real emit cascades via func_8007F2F0" worry was overstated: func_8007F2F0 is NOT a direct
    # callee; all 9 direct callees (func_80076E70/EE0/FBC, func_8007725C, func_80078434, func_8007F1CC/9E0,
    # func_800780A0, func_8007F960) are already present (real or force-stubbed), and the body is
    # self-contained ([0x8007867C, 0x80078CA0), no branch-outside). "emit" disposition = register the symbol
    # but DON'T add to stubs/ignored, so n64recomp emits the real body. TRACKER #58.
    ("func_8007867C", 0x8007867C, 0x624, "emit"),
    # func_80082524 (runtime 0x80081924) is the TITLE-SCREEN leaf DL emitter that func_8008286C jal's
    # (asm shows `jal func_80081924`, but that is the SPLAT name of the +0xC00-shifted target: the
    # recomp binds the runtime target 0x80081924 -> splat func_80082524, NOT splat func_80081924 which
    # lives at runtime 0x80080D24). While force-stubbed it returned the stale 0x140 (a count) instead of
    # the advanced DL buffer, so func_8008733C wrote GBI to 0x140 and SIGSEGV'd at state 6 (live-measured
    # 2026-06-27, native gdb). Its real body has its own prologue `addiu $sp,-0x20` at splat 0x80082524
    # (splat MIS-MERGED it into func_80082500, whose own body ends `jr $ra` at 0x8008251C); it does a
    # vtable dispatch (jalr *(a0+4)) then writes F3DEX2 words to the returned buffer, early-returning the
    # buffer t0 unchanged. It is OVER-SPLIT: branches reach 0x800827AC (inside func_800826D4), so its real
    # extent is the whole shared blob [0x80082524, 0x8008286C) = 0x348 (all branches internal: lowest
    # .L80082548, highest .L800827AC). Register it real at full size; the overlapped func_80082610/8266C/
    # 826D4 stay their own (real/stub) entries -- proven real-over-real overlap (func_8008286C already
    # overlaps the real func_80082C84). Removed from recomp/force_stub.txt in the same change. TRACKER #58.
    ("func_80082524", 0x80082524, 0x348, "emit"),
]

# Force-stub: named functions whose body "branches outside" because they live in the
# stage3/menu/race OVERLAY regions, where the linear single-.text ROM->runtime mapping
# (vram = rom + 0x7FFFF400) does not hold -- N64 overlays load to overlapping runtime
# addresses at different times and need PER-OVERLAY sections (epic #54 phase 3). For the
# boot-path ingestion proof we stub these so the recompiler completes; the boot segment
# (ROM 0x1000-0x2398) itself lifts cleanly. Splat names (== jal-added func_<splat>).
FORCE_STUB = [
    # (func_800028D0 was here -- the per-frame-render blocker. It is NOT an overlay-section problem:
    # the "branching outside" was a splat SPLIT, fixed by the SPLIT_MERGES entry above that grows it
    # to its real 0x828 size and absorbs func_80002F98. Un-stubbed 2026-06-26.)
]
# Additional force-stubs collected automatically by recomp/iterate_stubs.sh (overlay-region
# functions that "branch outside" under the linear mapping). One splat name per line.
FORCE_STUB_FILE = REPO / "force_stub.txt"

# Force-EMIT: functions the race recipe lists as `ignored` (not emitted) but which the
# whole-ROM link references, AND which emit as clean C under the linear mapping -> emit
# them as real code (faithful) rather than stubbing. Collected from `ld` "undefined
# reference" diagnostics, then verified to NOT branch-outside (those go to force_stub.txt
# instead). Empty today: the one referenced-ignored function found so far (func_80073F90)
# branches outside its own claimed range here, so it is force-stubbed, not emitted.
FORCE_EMIT = [
    "boot_pad_apply_calibration",  # jal'd by real func_800028D0; merged to 0x27E0 (see SPLIT_MERGES)
    "func_80009158",  # per-frame attract sub-mode sequencer; race-`ignored`; merged to 0x13C8 (see SPLIT_MERGES)
]

# Present-in-dump functions whose REAL body is replaced by a NATIVE implementation in
# recomp/src/libultra_stubs.c (bare-name symbol, same pattern as the race-ignored SI bridge
# func_8007F780): added to `ignored` so n64recomp skips the body while call sites keep the name.
# W134 (#53): func_8007FFF0 (runtime 0x8007F3F0) = alSynAddPlayer. The native reproduces its four
# RDRAM ops exactly but defers the new client's first voice-handler dispatch by ONE audio frame
# (samplesLeft = curSamples + one frame instead of = curSamples). Rationale: the ROM has a latent
# race -- the SFX-player constructor func_80076EE4 links the handler, then primes the current-event
# buffer a few dozen instructions later; alAudioFrame dispatching the handler inside that window
# reads an unprimed (zero/fill) event and crashes (on real HW the window is ~us against a 16.6ms
# retrace period, so the race is never lost; a cooperative scheduler delivers backlogged retraces
# at dispatch points INSIDE the window and loses it deterministically -- measured W134, SIGSEGV in
# func_8007699C reading *(player+0x2C)=0). The <=1-frame deferral is exactly the slack real
# hardware's retrace cadence provides. NAMED TIMING ADAPTATION, not a behaviour change: remove if
# ultramodern ever gains preemptive external-message delivery.
NATIVE_OVERRIDES = [
    "func_8007FFF0",
    # W135 (2026-07-04, #53): func_80079720 (runtime 0x80078B20) = the sound-player status getter
    # (`jr $ra; lw $v0,0x2C($a0)`), sole poll of the boot stop-and-drain busy-wait func_800676B4.
    # The recompiled spin has no dispatch point, so under the cooperative scheduler it livelocks
    # (boot never yields -> handler A's stop arm never clears obj->0x2C -> state 6 wedge, 0 fps).
    # Native (recomp/src/lambo_audio.cpp) = deliver_external_and_yield + the verbatim load: the
    # poll becomes a dispatch point, modelling the AI/retrace interrupt that preempts this spin on
    # real hardware (same class as the W97 osSetIntMask yield; limitation (a) closed for this loop).
    "func_80079720",
]

# Self-contained leaf functions removed from the race `stubs` list (un-stubbed) because they
# are real game logic the demo-race needs, verified pure-RDRAM (no COP0/MMIO/jal), and self-
# contained (clean `jr $ra`, no branch-outside -> no n64recomp "branching outside" error).
# Removing the name lets n64recomp emit its real body instead of an empty stub.
# Hand-authored [[patches.instruction]] / [[patches.hook]] blocks appended verbatim to the
# generated lamborghini.us.toml (issues #2/#4 landed them by editing the toml directly; the
# generator must carry them or a regen silently drops the widescreen HUD + LOD patches).
PATCH_BLOCKS = """
# Issue #4 — game-side geometry-LOD distance-test, NOT covered by RT64's
# renderer-level forceBranch (which only touches RSP G_BRANCH_Z/W).
# NOPs the bc1f +0xDC in func_80060464 that gates the per-car high-poly
# fall-through; threshold was 100.0 IEEE-754 at 0x42C80000. Identified
# via tools/scan_lod_patterns.py; verified via tools/probe_lod_patch.py
# (ROM byte 0x450000dc at rom 0x6063c matches).
[[patches.instruction]]
vram = 0x8005FA3C
func = "func_80060464"
value = 0x00000000

# Issue #2 — per-element widescreen HUD (gEXSetRectAlign). The 1P race screen of the 2D
# dispatcher func_80050860 draws three edge-anchored HUD elements back-to-back (verified
# live 2026-07-05 by probing every 2D helper's (x,y) args during a driven race):
#   jal 0x8004FF60 -> func_80056318 (runtime 0x80055718): speedometer dial at x=0xDC,
#                     which internally also draws the digital speed + gear texrects
#   jal 0x8004FF88 -> func_800583B8 (runtime 0x800577B8): RANK label+value block, x=0x118
#   jal 0x8004FFA0 -> func_80058464 (runtime 0x80057864): LAP label+values block, x=0x28
# Each jal is bracketed with a text hook that writes gEXEnable + gEXSetRectAlign
# (RIGHT/LEFT pin, then ORIGIN_NONE reset) into the game DL through its cursor global
# 0x800A39CC; natives in src/lambo_hud_widescreen.c. TIME stays centered (no tag needed
# under RT64 Expand); the minimap is polyline/viewport-drawn, not texrects — pinning it
# is a separate follow-up (rect-align cannot move it).
[[patches.hook]]
func = "func_80050860"
before_vram = 0x8004FF60
text = "lambo_ws_pin_right(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x8004FF68
text = "lambo_ws_pin_reset(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x8004FF88
text = "lambo_ws_pin_right(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x8004FF90
text = "lambo_ws_pin_reset(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x8004FFA0
text = "lambo_ws_pin_left(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x8004FFA8
text = "lambo_ws_pin_reset(rdram);"

# The speedo NEEDLE (triangle geometry via a G_MTX chain built inside the dial drawer
# func_80056318) is handled natively inside the existing dial bracket: pin records the
# DL cursor and reset patches the needle's LOAD matrix translation (see
# src/lambo_hud_widescreen.c). No extra hooks needed.

# Issue #41 — pin the 1P minimap composite to the left edge. Three pieces:
#   dots + P1 label: texrects from the overlay func_80054FFC (jal 0x80050588 in the 1P
#     race section) — LEFT rect-align bracket, same mechanism as LAP/RANK above;
#   player arrow: two quads via pool G_MTX LOADs built inside the same overlay call —
#     the bracket reset shifts their translation in game space (same walker as the
#     needle, different delta);
#   track outline: 3D geometry drawn by the frame builder func_8004384C through the
#     race perspective projection; its placement translate(-2.05, -2.4, 0) is built by
#     the jal at 0x80043C88 — hook rewrites the x argument ($a1 float bits) in flight,
#     gated to frames where the 1P bracket ran (see lambo_ws_minimap_outline_x).
[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050588
text = "lambo_ws_minimap_pin(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050590
text = "lambo_ws_minimap_reset(rdram);"

[[patches.hook]]
func = "func_8004384C"
before_vram = 0x80043C88
text = "extern unsigned int lambo_ws_minimap_outline_x(unsigned int); ctx->r5 = S32(lambo_ws_minimap_outline_x((uint32_t)ctx->r5));"

# Issue #42 — widescreen HUD for 2P split screen. func_80050860 keys on the player
# count at 0x800CE6A4 ($s0): ==2 branches to the top/bottom section (L_80050BEC).
# Verified live (probe printf + LAMBO_WARP=circuit:laps:car:2): in 2P only the top-half
# (y=0x10) and bottom-half (y=0x80) sections run; the 1P-style sections D/E never fire.
# Each half reuses the 1P RANK (x=0x10E right) / LAP (x=0x28 left) helpers and anchors.
#   top:    RANK jal 0x80050CE8, LAP jal 0x80050D00
#   bottom: RANK jal 0x80050F2C, LAP jal 0x80050F44
[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050CE8
text = "lambo_ws_pin_right(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050CF0
text = "lambo_ws_pin_reset(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050D00
text = "lambo_ws_pin_left(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050D08
text = "lambo_ws_pin_reset(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050F2C
text = "lambo_ws_pin_right(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050F34
text = "lambo_ws_pin_reset(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050F44
text = "lambo_ws_pin_left(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050F4C
text = "lambo_ws_pin_reset(rdram);"

# The per-half speed/position readout is drawn by the alternate-dial orchestrator
# func_800717E0 (a0 = player index; not called in a 1P race) on the RIGHT (x=0xDC).
# Bracket each per-half DIALORCH call RIGHT so the readout tracks the widescreen edge.
#   top half (a0=0):    DIALORCH jal 0x80050E0C
#   bottom half (a0=1): DIALORCH jal 0x80051050
[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050E0C
text = "lambo_ws_pin_right(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050E14
text = "lambo_ws_pin_reset(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80051050
text = "lambo_ws_pin_right(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80051058
text = "lambo_ws_pin_reset(rdram);"

# Issue #56 — pin the 2P split minimap. The game does not edge-pin the 2P minimap, so
# the whole composite floats inboard of the widened edge. Unlike 1P, the 2P minimap is
# drawn ENTIRELY as 2D by the overlay func_80054FFC (dots + P1 + player arrow + the
# track OUTLINE) — verified live: the outline moves with the LEFT rect-align bracket, and
# the 1P 3D outline builder (func_8004384C @ 0x80043F4C) never fires in 2P. So one LEFT
# bracket per viewport pins the whole composite together, no 3D camera translate needed.
# The overlay runs once per half (top jal 0x80050DB4, bottom jal 0x80050FF8); reset walks
# the arrow's LOAD matrix (reuses lambo_ws_minimap_reset, same as 1P).
[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050DB4
text = "extern void lambo_ws_minimap_pin_2p(unsigned char*); lambo_ws_minimap_pin_2p(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050DBC
text = "lambo_ws_minimap_reset(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80050FF8
text = "extern void lambo_ws_minimap_pin_2p(unsigned char*); lambo_ws_minimap_pin_2p(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x80051000
text = "lambo_ws_minimap_reset(rdram);"

# Issue #42 — widescreen HUD for TIME TRIAL (race mode 0; halfword 0x800CE6B4 == 0).
# In the 1P section the mode-0 branch (L_8004FFB0, beq at 0x8004FF70) skips RANK/LAP and
# draws its own texrect top HUD: PREVIOUS (left), LAPTIME (centre), RECORD + BEST LAP
# (right). Shared speedo (0x8004FF60) + minimap (0x80050588) are already pinned; only
# this top row floats. One rect-align bracket per side (all glyphs/table draws — reset's
# matrix walker finds no G_MTX LOAD).
#   left  (PREVIOUS):        draws 0x8004FFF8 .. 0x800501D8
#   right (RECORD/BEST LAP): draws 0x800501F4 .. 0x80050274
[[patches.hook]]
func = "func_80050860"
before_vram = 0x8004FFF8
text = "lambo_ws_pin_left(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x800501E0
text = "lambo_ws_pin_reset(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x800501F4
text = "lambo_ws_pin_right(rdram);"

[[patches.hook]]
func = "func_80050860"
before_vram = 0x8005027C
text = "lambo_ws_pin_reset(rdram);"

# Issue #3 — per-frame Mtx rebuild for the widescreen skybox. func_8004384C (the
# per-frame 3D DL builder) opens the frame by calling func_80075500 (a hand-rolled
# guPerspective-equivalent: fovy/aspect/near/far -> frustum -> Mtx) to build the
# BACKGROUND/skybox layer's own projection, distinct from the main race camera. The
# call site hardcodes the aspect argument ($a3) to the hex float literal 0x3FAAAAAB
# (= 4/3, verified by decoding the IEEE-754 bits) via `lui $a3,0x3FAA` / `ori
# $a3,$a3,0xAAAB` at 0x80042CE0/E4, so the frustum's tangent — and therefore the
# skybox panels' screen coverage — stays sized for 4:3 no matter the RT64 output
# width, leaving gaps at the wide edges under ar_option Expand. Hooked right after
# the literal is fully assembled (0x80042CE8, not a branch-delay slot) and before it
# is consumed by the jal at 0x80042CFC: overwrite $a3 with the live output aspect
# (lambo_ws_get_output_aspect_bits, rt64_renderer.cpp) so the frustum widens with the
# window — the fix degenerates to the original constant at 4:3 or ar_option Original.
[[patches.hook]]
func = "func_8004384C"
before_vram = 0x80042CE8
text = "extern unsigned int lambo_ws_get_output_aspect_bits(void); ctx->r7 = (int32_t)lambo_ws_get_output_aspect_bits();"

# Issue #12 — developer warp menu. Per-frame warp tick at the entry of the TOP-LEVEL
# state dispatcher func_800028D0 (runtime 0x80001CD0, argument-free, jump-table over
# states 1..0x10, runs every frame in every state; func_800030F8 was tried first but
# only runs in states 7/8 — too late for menu/attract warps). Consumes an F1-F6 /
# LAMBO_WARP request and performs the menu's own race-launch stores (cursor cluster +
# audio quiesce + state 7) before the dispatcher reads the state word; native in
# src/lambo_warp.c.
[[patches.hook]]
func = "func_800028D0"
before_vram = 0x80001CD0
text = "extern void lambo_warp_tick(uint8_t*, recomp_context*); lambo_warp_tick(rdram, ctx);"

# Issue #22 — developer save-state. Same top-level dispatcher (runs every frame in every
# state), one instruction past the warp hook: N64Recomp rejects two hooks on the exact same
# vram, and 0x80001CD4 is still the frame-boundary entry. Snapshots/restores rdram[0..8MiB)
# on an F5/F9 or LAMBO_STATE_LOAD request; native in src/lambo_savestate.c.
[[patches.hook]]
func = "func_800028D0"
before_vram = 0x80001CD4
text = "extern void lambo_savestate_tick(uint8_t*, recomp_context*); lambo_savestate_tick(rdram, ctx);"

# Issue #40 — widescreen lens flare. The sun flare emitter func_80036854 draws a chain of
# 10 translucent "ghost" texrects. Under ar_option Expand RT64 squishes each small untagged
# texrect into the central 4:3 band (invRatioScale = 1/aspectRatioScale) and collapses its
# scissor the same way, so the flare is cropped instead of tracking the FOV-widened sun.
# Bracket the ghost loop with gEXSetRectAspect(STRETCH) -> invRatioScale = 1.0, which keeps
# the offset-from-centre in game pixels (matching the widened 3D sun) and widens the scissor
# to the full frame. 0x800361BC is just before the loop (after the flare's setup commands);
# 0x80036A60 is just past the loop back-edge (0x80036A58). Natives in
# src/lambo_flare_widescreen.c; no-op at 4:3. See docs/HUD.md.
[[patches.hook]]
func = "func_80036854"
before_vram = 0x800361BC
text = "lambo_flare_ws_aspect_stretch(rdram);"

[[patches.hook]]
func = "func_80036854"
before_vram = 0x80036A60
text = "lambo_flare_ws_aspect_reset(rdram);"
"""

UNSTUB = [
    # func_8006CEC8 (runtime 0x8006C2C8) = menu 3D-overlay emitter (cursor + selection arrows);
    # race-`stubs` only because splat truncated it (see its SPLIT_MERGES entry). Merged to 0x7F8;
    # emit the real body. Also removed from force_stub.txt in the same change (force_stub.txt
    # precedence would silently re-stub it, the W136 trap). TRACKER #32.
    "func_8006CEC8",
    # func_8004AFD8 (runtime 0x8004A3D8) = menu L/R selection-arrow texrect drawer; race-`stubs`
    # only because splat truncated it (see its SPLIT_MERGES entry). Merged to 0x1098; emit the
    # real body. TRACKER #32.
    "func_8004AFD8",
    # func_80011F70 (runtime 0x80011370) is the demo-race actor-system HEADER writer: it sets the
    # control fields just below the actor pointer table (D_800B69A8/AC/AE) that the actor-table
    # populator func_80038D6C READS (8 refs in its asm) to drive its populate loop. While stubbed
    # (empty body) the header stayed 0, so func_80038D6C's populate never ran and the actor table
    # 0x800B69B0 stayed NULL -> func_80014390 SIGSEGV'd dereferencing a null table entry at state 7
    # (the demo-race attract scene). Its asm (asm/race_full_functions/func_80011F70.s, size 0x30C) is
    # pure RDRAM stores + a clean `jr $ra`, no COP0/MMIO/jal -> safe to recompile. Called from the
    # state-7 handler boot_pad_apply_calibration (runtime 0x80006A30) BEFORE the func_80014390 read.
    # TRACKER #64 (state 7->8 demo-race crash).
    "func_80011F70",
    # func_800152BC (runtime 0x800146BC): second unconditional per-frame callee of the states-7/8
    # game-logic SM func_800030F8; race-`stubs` only because splat over-split it (see its
    # SPLIT_MERGES entry, W114). Merged to 0xBD4; emit the real body. TRACKER #53/#54.
    "func_800152BC",
    # func_80075C60 (runtime 0x80075060) = osViSetMode (see its SPLIT_MERGES entry, W115). Pure
    # RDRAM stores + two jals to the int-mask pair (runtime 0x8007D4E0/0x8007D500 -- the same pair
    # the emitted-real osViBlack func_80073DD0 already calls safely every boot), clean `jr $ra`.
    # Emitting the real body lands __osViNext->modep/state/control in RDRAM (converges to ares);
    # promote_vi_context (recomp/src/main.cpp) applies it to ultramodern's native VI at retrace
    # cadence, exactly where libultra's __osViSwapContext would. TRACKER #58.
    "func_80075C60",
    # func_8004F5F0 (runtime 0x8004e9f0) = the menu confirm/OK-press handler (see its SPLIT_MERGES
    # entry): race-`stubs` only because splat quartered it and the head "branched outside". Merged
    # to 0x5EC; emit the real body so the pak-message screen's OK press actually runs the confirm
    # dispatch instead of a no-op (#69 stuck-screen root cause, measured live 2026-07-03).
    "func_8004F5F0",
    # func_8007FDC4 (runtime 0x8007F1C4) = the cascade's shared TAIL-CALL EPILOGUE, 0x8 bytes
    # (`jr $ra; addiu $sp, $sp, 0x38`). It's the audio producer cascade's shared epilogue: at
    # least func_80079F10 (audio command builder, splat asm line `0C01FF71  jal func_8007FDC4`)
    # TAIL-JUMPS to this address instead of doing its own `addiu $sp; jr $ra` -- exactly the
    # compiler tail-call optimization. Currently a stub because splat carved it out of the next
    # function (func_8007FDCC = libultra `osSendMesg`) where it has no semantic meaning. Un-
    # stubbing it (a) lets N64Recomp emit a clean 0x8-byte body, and (b) sets up the cascade
    # translation pass for func_80079F10/func_80079CA0 by removing a "stub called from stub"
    # dead link in the call graph. Body is a clean `return;`, no COP0/MMIO/jal -> safe. #53 PR 2 inc.
    "func_8007FDC4",
    # func_80079F10 (runtime 0x80079310) = the AUDIO COMMAND BUILDER, the cascade head that
    # allocates audio command structs (via func_80075CB0), fills in fields, priority-enqueues via
    # func_8007FE18, and tail-jumps to func_8007FDC4 (W127 un-stubbed epilogue). See its
    # SPLIT_MERGES entry above: the head is grown to 0x260 to absorb the inner-loop forward-
    # branch + backwards-branch targets, and the tail func_8007A078 stays as its own emitted
    # real entry (overlap pattern, the per-frame producer func_8006A7A0 jals func_8007A078
    # directly at splat 0x6A7E8). Body is pure-RDRAM + jals to already-emitted allocator /
    # queue / epilogue, no COP0/MMIO -> safe to recompile. Removing the name lets n64recomp
    # emit the real body instead of recomp_stub_hit. TRACKER #53 PR 2.
    "func_80079F10",
    # func_80079CA0 (runtime 0x800790A0) = the AUDIO SIBLING PRODUCER, the second cascade head.
    # Self-contained in splat (single 0x1D0-byte .s file ending in its own `jr $ra; addiu $sp,
    # 0xB8` epilogue at splat 0x7A568/6C) -- no SPLIT_MERGES needed. Its loop (jlrc vtable calls
    # + cvt.d.s/double arithmetic + jals to func_80078F28 + struct stores) is pure-RDRAM with
    # NO COP0/MMIO. Was race-stubbed because splat over-fragmented the cluster, not because of
    # any actual branching-outside in the head -- removing the name lets n64recomp emit the
    # real body and the audio command build chain (cascade head -> func_8007FE18 priority-enqueue
    # -> func_8006A7A0 per-frame driver -> osAiSetNextBuffer) finally produces real audio.
    # TRACKER #53 PR 2.
    "func_80079CA0",
    # W136 (2026-07-04, #53): func_800819BC (runtime 0x80080DBC) = the voice DECODER pull
    # handler -- see its SPLIT_MERGES entry (merged to 0x4A0 absorbing func_80081BBC). Emitting
    # the real body completes the per-voice filter chain (decoder -> resampler -> envmixer) so
    # started voices emit their ACMD 0x1/0x3/0x5 trio instead of poisoning the cursor.
    "func_800819BC",
    # W129 SPLAT-SHIFT-TRAP CORRECTION (2026-07-03, post-rebuild): the W129 inc 3
    # un-stub targeted func_8006A870 (splat 0x8006A870 -> runtime 0x80069C70, 0x44 bytes),
    # but the +0xC00 splat-shift rule says runtime 0x8006A870 maps to splat func_8006B470
    # (NOT splat func_8006A870). The actual recomp C call from func_800716B8 is to
    # func_8006B470(rdram, ctx) at recomp/RecompiledFuncs/funcs_9.c:12590, NOT to
    # func_8006A870. func_8006B470 is the FIRST-HOP gate (runtime 0x8006A870, 0xA04-byte
    # jtbl dispatcher, EMITTED-EMPTY, still in stubs). func_8006A870 (the function W129
    # un-stubbed) is dead code (zero direct recomp C callers). Live-measured: W129 rebuild
    # + headless boot shows the audio probe still does NOT fire -- the gate remains.
    #
    # Keeping the func_8006A870 UNSTUB entry below (no harm done, and the body is genuinely
    # a leaf-style function whose real body is now in funcs_8.c:11096 with 21 recompiled
    # instructions). The real fix for #53 PR 2 is in a follow-up session: un-stub
    # func_8006B470 (with the same risk profile check -- verify no back-branch into the
    # func_80079F10 head range BEFORE un-stubbing, since it's a 1366-instruction jtbl
    # dispatcher that may share bytes with adjacent functions).
    #
    # Original W129 comment (kept below for the audit trail):
    # func_8006A870 (splat 0x8006A870) = AUDIO CASCADE thin wrapper, 0x44 bytes. asm:
    # NO PROLOGUE (first instr `sll $t0,$t0,3`), epilogue `addiu $sp,$sp,0x18` at
    # splat 0x8006A8A4/8/C. Calls `jal func_80079F10` then stores 0 to D_80110F18[t2*4].
    # Pure-RDRAM, NO COP0/MMIO, NO back-branch into another function. W128 graveyard
    # risk profile does NOT apply. TRACKER #53 PR 2 inc 3 (UNSTUB performed; the
    # splat-shift-trap issue means this was the wrong function -- see correction note
    # above).
    "func_8006A870",
    # ------------------------------------------------------------------
    # W133 AUDIO-LIBRARY BULK UN-STUB (2026-07-03, #53). The race stubs list blanketed
    # ~182 functions across the audio driver/synthesizer range (splat 0x80066000-0x80080C90).
    # W132 proved the mixer plumbing runs but NO voices ever start; the driver dispatches
    # via RETURNED function pointers (func_80068230 returns the voice-enqueue func
    # 0x800673B4 in $v0; audio-init func_80067790 registers &func_80068230 in a table),
    # so ANY stub in the chain silently kills all sound with zero direct-jal evidence.
    # Peer N64Recomp ports (SK2/Zelda64) carry no game-code stub blanket -- this converges
    # us to that model. Selection: every audio-range race stub EXCEPT (a) privileged
    # bodies (COP0/TLB/cache, 10 funcs), (b) the libultra/scheduler block
    # 0x80073000-0x80076080 (ultramodern natives own it), (c) known-bad
    # func_8007A078/func_80078214/func_80080890, (d) force_stub.txt failures (re-added
    # by the force list below automatically). Emission warning-gated: any [Warn]
    # branching-outside candidate is reverted via force_stub.txt. Scan tool:
    # scratchpad audio_unstub_scan.py; TRACKER #53.
    "func_800662B4",
    "func_800664BC",
    "func_800665F8",
    "func_80066728",
    "func_800667A0",
    "func_80066828",
    "func_80066A30",
    "func_800670F0",
    "func_8006735C",
    "func_8006768C",
    "func_8006927C",
    "func_80069344",
    "func_8006962C",
    "func_80069ED8",
    "func_8006A720",
    "func_8006B470",
    "func_8006BE74",
    "func_8006CAC0",
    "func_8006CCA4",
    "func_8006CE84",
    "func_8006CEC8",
    "func_8006D850",
    "func_8006DA30",
    "func_8006DD68",
    "func_8006E064",
    "func_8006F068",
    "func_80070248",
    "func_800703F8",
    "func_80070548",
    "func_80070AB8",
    "func_80070BE0",
    "func_80070E10",
    "func_80076430",
    "func_80076524",
    "func_80076710",
    "func_80076760",
    "func_80076790",
    "func_800767B0",
    "func_800767D0",
    "func_80076830",
    "func_80076890",
    "func_800768F0",
    "func_80076950",
    "func_800769C0",
    "func_80076A60",
    "func_80076AB4",
    "func_80076B34",
    "func_80076C14",
    "func_80076C9C",
    "func_80076DC8",
    "func_80076E70",
    "func_80077034",
    "func_8007707C",
    "func_800770F0",
    "func_80077160",
    "func_8007725C",
    "func_80077E5C",
    "func_800781A4",
    "func_800783E8",
    "func_80078434",
    "func_800785B8",
    "func_800788B8",
    "func_800788E8",
    "func_80078A00",
    "func_80078A40",
    "func_80078A80",
    "func_80078CC0",
    "func_80078CE4",
    "func_80078D1C",
    "func_80078D50",
    "func_80078DD0",
    "func_80078E80",
    "func_80078E98",
    "func_80078F28",
    "func_80078F88",
    "func_80079570",
    "func_80079768",
    "func_80079838",
    "func_80079930",
    "func_80079AD0",
    "func_80079BCC",
    "func_8007A740",
    "func_8007A890",
    "func_8007AB80",
    "func_8007ADE0",
    "func_8007B780",
    "func_8007B9E0",
    "func_8007BB50",
    "func_8007BFA0",
    "func_8007C050",
    "func_8007C424",
    "func_8007C568",
    "func_8007C690",
    "func_8007CC74",
    "func_8007CEA4",
    "func_8007CEB4",
    "func_8007D0C0",
    "func_8007D120",
    "func_8007D1E8",
    "func_8007D2E8",
    "func_8007E470",
    "func_8007E580",
    "func_8007E600",
    "func_8007E640",
    "func_8007E680",
    "func_8007E6D0",
    "func_8007E700",
    "func_8007E81C",
    "func_8007E9AC",
    "func_8007E9F0",
    "func_8007EAA0",
    "func_8007EAC0",
    "func_8007EB10",
    "func_8007EB54",
    "func_8007EB80",
    "func_8007EC30",
    "func_8007ED30",
    "func_8007EEA0",
    "func_8007EFF0",
    "func_8007F020",
    "func_8007F0B0",
    "func_8007F1CC",
    "func_8007F37C",
    "func_8007F3F0",
    "func_8007F440",
    "func_8007F528",
    "func_8007F5F0",
    "func_8007F700",
    "func_8007F790",
    "func_8007F8C0",
    "func_8007F9E0",
    "func_8007FA90",
    "func_8007FC70",
    "func_8007FC80",
    "func_8007FC90",
    "func_8007FD50",
    "func_8007FD80",
    "func_8007FF9C",
    "func_80080040",
    "func_800804C0",
    "func_800805C0",
    "func_80080C90",
    # NOTE: func_8007A078 (runtime 0x80079478) UN-STUB ATTEMPTED but FAILED 2026-07-03 -- the
    # tail of the func_80079F10 split at 0x7A078, called by func_8006A7A0 (per-frame producer)
    # at splat 0x6A7E8. Un-stubbing it produced `state=60888` transient blip at vi=8 (recovered
    # by vi=1151). Root cause matches the W127 graveyard func_80078214 pattern: the body
    # branches cross the SPLIT_MERGES-overlap boundary in a way that corrupts D_800CE6AC
    # (g_state) at boot. N64Recomp emits a [Warn] "branching outside (to 0x80079430)" -- the
    # back-branch .L8007A030 from the tail into the head's grown range. The current emit
    # auto-merges func_8007A078 into func_8007AC78 (runtime 0x80079C78, size 0x2E8) so the per-
    # frame path DOES have a real cascade-body function -- no separate un-stub needed. Until
    # either (a) the back-branch is reorganized (move .L8007A030 forward), or (b) the head is
    # grown to absorb the tail AND the per-frame producer's call to func_8007A078 is redirected
    # into the head (the "absorb + rebind" pattern), DO NOT blanket un-stub. See graveyard
    # entry for details.
]

# Canonical libultra naming overrides (pivot phase 3, epic #54).
# -------------------------------------------------------------
# The whole-ROM recompile labels every function `func_<splat>`. N64Recomp decides a
# function's fate purely by its NAME string (N64Recomp/src/main.cpp:430-439): a name in
# `reimplemented_funcs` (symbol_lists.cpp) routes to librecomp's native `_recomp` version;
# a name in `ignored_funcs` is skipped (no body emitted). Because our libultra functions are
# named `func_...`, their bodies are recompiled VERBATIM -- including the raw `mtc0 $a0,Status`
# in __osSetSR, which ultramodern's cop0_status_write rejects (exit(EXIT_FAILURE)). Giving these
# functions their canonical libultra names lets the recompiler route/skip them instead.
#
# KEYED BY the toml NAME (`func_<splat>`), exactly as docs/symbol_addrs.txt + force_stub.txt key
# things -- this sidesteps the +0xC00 runtime/splat confusion (the toml emits func_<splat> names
# with runtime `vram`). Source: docs/symbol_addrs.txt (each entry verified from bytes before adding).
#
# Buckets:
#   * route-to-native: canonical name IS in reimplemented_funcs -> renamed `_recomp`, links to librecomp.
#   * ignore/skip:     canonical name IS in ignored_funcs       -> body skipped (no mtc0). If the func is
#                      still CALLED and has no librecomp native (e.g. __osSetSR), we provide a no-op symbol
#                      in recomp/src/libultra_stubs.c (keeps the vendored submodule pristine).
LIBULTRA_NAMES = {
    # Boot hardware-detect init (verified from the recompiled body funcs_10.c @ runtime 0x80072F40):
    # __osGetSR -> __osSetSR(|CU1) -> __osSetFpcCsr(0x01000800) -> busy-loop probing PIF RAM 0x1FC007FC
    # via SI status (0xA4800018). That is __osInitialize_common. It is in reimplemented_funcs, so it
    # routes to librecomp's native (== ultramodern osInitialize()) -- collapsing the entire CP0+SI/PIF
    # subtree that otherwise recompiles raw `mtc0 Status` (cop0 abort) and raw SI MMIO (OOB segfault).
    "func_80073B40": "__osInitialize_common",
    # Thread primitives BootMain uses to create+start the boot idle thread (verified from bytes):
    #   func_80073E40 = osCreateThread (symbol_addrs.txt:52).
    #   func_80073F90 = osStartThread (asm/race_full_functions/func_80073F90.s: __osDisableInt ->
    #     check thread->state @+0x10 -> set RUNNABLE -> __osEnqueueThread into run-queue D_8008D1B8).
    # Both are in reimplemented_funcs -> route to librecomp natives (ultramodern's native scheduler),
    # replacing the game's cooperative run-queue. Routing func_80073F90 also retires its force_stub
    # entry (it only branched-outside because it was recompiled; as a native it isn't emitted).
    "func_80073E40": "osCreateThread",
    "func_80073F90": "osStartThread",
    # Message-queue + thread-priority primitives the boot idle thread uses (verified from bytes):
    #   func_80074350 = osCreateMesgQueue (asm/.../func_80074350.s: writes mtqueue/fullqueue =
    #     __osThreadTail D_8008D1B0, validCount=0 @+8, first=0 @+C, msgCount=a2 @+10, msg=a1 @+14).
    #   func_8007E550 = osGetThreadPri (asm/.../func_8007E550.s: `if (a0==0) a0 = __osRunningThread
    #     D_8008D1C0; return a0->priority @+4`). The recompiled getter SIGSEGVs dereferencing a thread
    #     ptr the native scheduler doesn't mirror in RDRAM; the native returns the priority directly.
    "func_80074350": "osCreateMesgQueue",
    "func_8007E550": "osGetThreadPri",
    # Thread-priority setter, sibling of osGetThreadPri (verified from bytes):
    #   func_80074270 (runtime 0x80073670) = osSetThreadPri (asm/race_full_functions/func_80074270.s:
    #     __osDisableInt (jal func_8007D4E0); `if (a0==0) a0 = __osRunningThread D_8008D1C0`; args
    #     (thread, pri)). Identical __osRunningThread dependency as osGetThreadPri: the recompiled body
    #     SIGSEGVs at 0x8007429C dereferencing the running-thread ptr the native scheduler does not
    #     mirror in RDRAM (== 0). In reimplemented_funcs (symbol_lists.cpp:86) -> the native applies the
    #     priority via ultramodern's own thread state. This is the boot idle-thread frontier crash
    #     (BootIdleThread -> func_800740E0 -> func_80074270).
    "func_80074270": "osSetThreadPri",
    # Message send/recv pair (verified from bytes; the +0xC00 alias the handoff flagged). The toml
    # `func_<splat>` names are the runtime bodies the recompiled callers actually jal:
    #   func_80075AE0 (runtime 0x80074EE0) = osSendMesg (asm: __osDisableInt; validCount@+8 < msgCount
    #     @+10 ? enqueue : block via __osRunningThread D_8008D1C0 -> state=WAITING(8)).
    #   func_80075050 (runtime 0x80074450) = osRecvMesg (validCount@+8 check + __osRunningThread block).
    # Both read/mutate __osRunningThread, which ultramodern's NATIVE scheduler does not mirror in RDRAM
    # -> recompiled bodies SIGSEGV. The natives block the calling native thread via ultramodern's queue
    # condvar instead, coherent with the now-native osCreateMesgQueue/osCreateThread.
    "func_80075AE0": "osSendMesg",
    "func_80075050": "osRecvMesg",
    # func_8007FDCC: ORCH17 (2026-06-28) routed this "second osSendMesg" to the native.
    # W134 (2026-07-04, #53) proved the routing WRONG-BUT-LOAD-BEARING: bytes say it is NOT an
    # osSendMesg -- it is the audio library's own event-post (pure-RDRAM delta-sorted list insert
    # into the evtq the real emitted nextEvent func_8007FEF0 pops; early-out when *(q+0)==0; no
    # OSMesgQueue, no thread tables). Routed native, ultramodern do_send reads msgCount=0 on the
    # custom struct and returns -1 for ALL 21 audio call sites -> every game sound request is
    # silently dropped (W134 gdb: alSndpPlay 21x, voice-start 0x). UN-ROUTING it (emit the real
    # body via its SPLIT_MERGES entry) brought the whole event pipeline ALIVE (handler A
    # func_8007867C 1016 dispatches/1800 VIs, 2538 posts, SDL sink receiving buffers) BUT the
    # boot fade-complete cmd-0x11 handshake (func_800676B4 busy-wait on obj->0x2C, obj =
    # *(D_8008C1B0)=0x800CF998) then never completes -> state machine sticks at 6 (old build
    # passed it only because the native-send no-op left obj->0x2C at its accidental-zero value).
    # W135 (2026-07-04): the LAMBO_REAL_AUDIO_POST lever is FLIPPED INTO THE DEFAULT and deleted.
    # The state-6 stick was NOT an ack-semantics problem: boot's stop-and-drain busy-wait
    # (func_800676B4) had no dispatch point, so the spinning boot thread starved the audio thread
    # whose handler-A stop arm clears obj->0x2C -- a cooperative-scheduler livelock, fixed by the
    # func_80079720 yielding native (see NATIVE_OVERRIDES). Real post + real handler + real second
    # player + real MIDI dispatcher (func_80077E5C) are now the unconditional default.
    # CP0 kernel helper: `mtc0 $a0,Status; jr $ra` (0x10 B, verified from asm/race_full_functions/
    # func_8007D260.s). In N64Recomp ignored_funcs -> body skipped; renamed `__osSetSR_recomp` at call
    # sites, no native -> we provide a no-op in recomp/src/libultra_stubs.c. Kept as a defensive route
    # in case __osSetSR is reached from a path other than __osInitialize_common (e.g. osSetIntMask).
    "func_8007D260": "__osSetSR",
    # VI hardware init, reached via osCreateScheduler (func_80074B90) -> func_8007ED10 -> here (verified
    # from bytes): func_8007E120 (runtime 0x8007D520) = __osViInit. It bzero's the 0x60 VI-context buffer
    # at D_8008D140, sets __osViCurr/__osViNext (D_8008D1A0 = buf, D_8008D1A4 = buf+0x30), reads osTvType
    # (D_80000300), then writes the VI mode registers and polls VI_CURRENT (`lw 0x10(0xA4400000)`, the
    # sltiu 0xB wait-for-vblank). That raw VI MMIO is the SIGSEGV (MEM_W(0xA4400010) is far past the 8 MB
    # RDRAM bound) -- same class as the __osInitialize_common SI/PIF MMIO. __osViInit is in ignored_funcs
    # ONLY (symbol_lists.cpp:173), NOT reimplemented -- the modern stack deliberately skips libultra's VI
    # HW init: ultramodern's VI manager doesn't use the __osViCurr/__osViNext RDRAM contexts (the per-frame
    # osViSwapBuffer/osViSetMode the game actually calls ARE reimplemented natives). So no native exists ->
    # we supply a no-op __osViInit_recomp in recomp/src/libultra_stubs.c (same pattern as __osSetSR).
    "func_8007E120": "__osViInit",
    # Interrupt-mask setter, reached via func_80074B40 (runtime 0x80073F40) from BootLoadInitialAssets
    # (verified from bytes): func_8007F0E0 (runtime 0x8007E4E0, a HANDWRITTEN libultra fn) = osSetIntMask.
    # It `mfc0 Status`, indexes the IM table D_8008F080 by the a0 mask, and writes MI_INTR_MASK
    # (`sw 0xC(0xA4300000)` = 0xA430000C), returning the previous mask. The SIGSEGV is MEM_W(0xA430000C)
    # reading MI MMIO past the 8 MB RDRAM bound -- CP0 + MI MMIO, the kernel/interrupt class ultramodern
    # owns. In reimplemented_funcs (symbol_lists.cpp:111) -> routes to librecomp's native interrupt model.
    "func_8007F0E0": "osSetIntMask",
    # Audio DAC-rate setter, reached via BootLoadInitialAssets -> func_80066C10 -> func_80067770 -> here
    # (verified from bytes + runtime registers): func_80079730 (runtime 0x80078B30) = osAiSetFrequency.
    # It float-divides the clock (D_8008D088) to compute the DAC rate, then writes AI_DACRATE
    # (`sw 0x10(0xA4500000)` = 0xA4500010) and AI_BITRATE. The SIGSEGV is that AI MMIO store: gdb showed
    # r8 = 0xA4500000 (the AI register base, NOT RDRAM) and r25 = 0x89F (dacRate-1) -- raw AI MMIO, same
    # class as the VI/MI MMIO above, which ultramodern's audio backend owns. In reimplemented_funcs
    # (symbol_lists.cpp:11). NOTE: the combined-ELF dump spuriously splits this at 0x80079768; the race
    # function_sizes override (0x190) correctly reconstitutes the whole osAiSetFrequency body, so routing
    # the func_80079730 head skips the entire primitive.
    "func_80079730": "osAiSetFrequency",
    # OS event registration the scheduler uses to wire RCP/VI interrupts to its queue
    # (verified from bytes). Without these the game deadlocks: all 7 game threads (incl.
    # __scMain func_80074884) block in osRecvMesg forever, because the recompiled ROM
    # bodies register events into the ROM's own RDRAM event tables, which ultramodern's
    # native VI/event threads never read -> retrace/SP/DP messages are never delivered ->
    # the scheduler never wakes to submit a gfx OSTask (osSpTaskStartGo is never reached).
    #   func_80074CC0 (runtime 0x800740C0) = osSetEventMesg. osCreateScheduler (func_80074B90)
    #     jal's it 3x (SP/DP/PRENMI). asm/race_full_functions/func_80074CC0.s: 3 args
    #     (event,mq,msg); __osDisableInt (jal func_8007D4E0); indexes __osEventStateTab
    #     D_8011C5B0 by (event << 3). Native osSetEventMesg_recomp registers the queue in
    #     ultramodern's events_context.{sp,dp,...} (librecomp ultra_translation.cpp:64).
    #   func_8007F070 (runtime 0x8007E470) = osViSetEvent. osCreateScheduler jal's it once,
    #     after the 3x osSetEventMesg. asm/.../func_8007F070.s: 3 args (mq,msg,retrace_count);
    #     __osDisableInt; writes mq into __osViNext [D_8008D1A4] +0x10 (the retrace event slot).
    #     Native osViSetEvent_recomp registers it in ultramodern's VI manager, whose VI thread
    #     then enqueues the retrace message each frame (ultramodern events.cpp:236), waking
    #     __scMain. Both canonical names are in N64Recomp reimplemented list (symbol_lists.cpp
    #     :23 osViSetEvent, :94 osSetEventMesg) -- same routing path as the working osRecvMesg.
    "func_80074CC0": "osSetEventMesg",
    "func_8007F070": "osViSetEvent",
    # Audio-thread AI status read (verified from bytes). Once osViSetEvent/osSetEventMesg
    # above wake the threads, the audio thread (func_80067B98 -> func_80067CF0) reaches its
    # per-frame AI poll and SIGSEGVs: func_80079A80 (runtime 0x80078E80) = osAiGetLength is a
    # 0xC-byte leaf `lui $t6,0xA450; lw $v0,0x4($t6); jr $ra` -- a raw read of AI_LEN
    # (0xA4500004), past the 8 MB RDRAM bound. Same AI-MMIO class as osAiSetFrequency above;
    # native osAiGetLength_recomp (librecomp ai.cpp:23) returns the length from ultramodern's
    # audio backend. In N64Recomp reimplemented list (symbol_lists.cpp:9).
    "func_80079A80": "osAiGetLength",
    # Audio-buffer DMA submit (verified from bytes, asm/race_full_functions/func_800799D0.s).
    # func_800799D0 (runtime 0x80078DD0) = osAiSetNextBuffer: it ping-pongs the buffer-count flag
    # at D_8008D100, calls __osAiDeviceBusy (jal func_8007FD50 == splat func_80080950, the +0xC00
    # alias) as the `if (busy) return -1` guard, calls osVirtualToPhysical (jal func_80078D50),
    # then writes AI_DRAM_ADDR (0xA4500000) and AI_LEN (0xA4500004) -- raw AI MMIO, the same class
    # as osAiSetFrequency/osAiGetLength above. The audio thread reaches this once a ucode is
    # registered for the type-2 task; the recompiled body SIGSEGVs in the __osAiDeviceBusy leaf
    # (MEM_W of AI_STATUS @ 0xA450000C, past the 8 MB RDRAM bound). Routing the PARENT primitive
    # to native osAiSetNextBuffer_recomp (librecomp ai.cpp:18, queues the buffer into ultramodern's
    # audio backend) subsumes BOTH raw-MMIO leaves: __osAiDeviceBusy (sole caller is this fn) and
    # osVirtualToPhysical are never reached. In N64Recomp reimplemented list (symbol_lists.cpp:12).
    "func_800799D0": "osAiSetNextBuffer",
    # Boot timed-delay primitive (verified from bytes + live gdb thread topology). Once the
    # event routing above wakes the threads, BootLoadInitialAssets does NOT reach gfx -- it
    # blocks in osRecvMesg inside the object-bootstrap poll: BootLoadInitialAssets ->
    # func_80069710 -> func_8007A170, which sets up a one-shot timer and waits on a LOCAL
    # stack message queue for it to fire. func_80083020 (runtime 0x80082420) = osSetTimer:
    # asm/race_full_functions/func_80083020.s writes the OSTimer struct (next/prev=0 @+0/+4,
    # value @+8/+C, interval @+10/+14, mq @+18, msg @+1C), then __osDisableInt -> indexes the
    # active-timer list __osTimerList (D_8008D5D0) -> __osRestoreInt. The caller's o32 args
    # match the native exactly (mq @ 0x18(sp), msg @ 0x1C(sp); ultra_translation.cpp:94). The
    # raw recompiled body inserts the timer into the ROM's RDRAM __osTimerList, which the modern
    # stack never services -- IDENTICAL bug class to osViSetEvent/osSetEventMesg above -> the
    # timer never expires, osRecvMesg blocks forever, boot stalls. In reimplemented_funcs
    # (symbol_lists.cpp:98); native osSetTimer (ultramodern timer.cpp:181) registers it with the
    # ultramodern timer_thread (timer.cpp:69 -- the idle "Timer Thread" in the live topology),
    # which fires the countdown and posts to the (now-native, routed osCreateMesgQueue) local mq.
    "func_80083020": "osSetTimer",
    # Controller SI read, reached once osSetTimer (above) unblocks the boot poll: the object-
    # bootstrap loop BootLoadInitialAssets -> func_80069710 -> func_8007A170 then issues a
    # controller read and SIGSEGVs (verified live: SIGSEGV in func_8007F780 -> func_80085260).
    # func_8007F780 (runtime 0x8007EB80) = osContStartReadData: asm/race_full_functions/
    # func_8007F780.s opens with `jal func_80084660` (== splat func_80085260, runtime 0x80084660,
    # __osSiDeviceBusy) -- the libultra `if (__osSiDeviceBusy()) ret = -1` busy-guard. func_80085260
    # is the crash: a 0xC-byte leaf `lui $t6,0xA480; lw 0x18($t6); jr $ra` reading SI_STATUS
    # (0xA4800018), raw SI MMIO past the 8 MB RDRAM bound -- same MMIO class as the VI/MI/AI
    # primitives above. osContStartReadData is in reimplemented_funcs (symbol_lists.cpp:34/201);
    # native osContStartReadData_recomp (librecomp cont.cpp:37) runs the whole flow via
    # ultramodern (no raw SI), returns the pad data (zeroed under the headless null poll_input),
    # AND fires ultramodern::send_si_message -> the OS_EVENT_SI queue, which also wakes the
    # boot SI-dispatcher thread func_8000A5FC (parked in osRecvMesg on OS_EVENT_SI / D_80098278).
    #
    # REBOUND 2026-06-29 (#64/#53) -> a pivot BRIDGE (libultra_stubs.c func_8007F780_recomp), now in
    # EXTRA_FUNCS as `ignore` (see below), NOT routed here. WHY: the native osContStartReadData polls
    # + send_si but does NOT fill the game's RAW PIF buffer passed in a1 (=D_8011C6D0). So func_8007A7CC
    # decoded a 0xff "no response" status -> func_80083100 returned 2 -> the object-slot gate
    # func_8007A8A0 bailed -> STUCK at state 6 (ares reaches 8: D_800CE6AC 2->3->4->6->8). The bridge
    # calls the native (keeps poll+send_si) THEN fills D_8011C6D0 + the controller-init globals
    # (count D_8011C681=4, mode D_8011C680=1, D_8011C640[i]=FF 01 04 01) from ultramodern, matching the
    # SI read's observable effect (ares D_8011C6D0[0]=FF 03 21 02 <btn> 00 00, status byte 0=clean).
    # PI cart-DMA subsystem (verified from bytes). Once the boot poll is unblocked, Game Thread 0
    # SIGSEGVs inside the recompiled PI device-manager chain doing a raw PI_STATUS read. The whole PI
    # subsystem is currently recompiled verbatim (none of it was named), so the game spins up its own
    # libultra device-manager thread and runs the raw hardware-poll leaf:
    #   func_800741F4 (runtime 0x800735F4) = osCreatePiManager. asm/race_full_functions/func_800741F4.s
    #     loads T_8007DC80 (== runtime 0x8007DC80 = func_8007E880 = __osDevMgrMain) as the thread entry
    #     and creates the device-manager thread + cmd queue at __osPiDevMgr (D_8008D0A0). In the modern
    #     stack DMA is synchronous, so no manager thread is wanted: native osCreatePiManager_recomp
    #     (librecomp pi.cpp:60) is a NO-OP -> the manager thread (__osDevMgrMain func_8007E880) and its
    #     raw leaf (__osPiRawStartDma func_8007E570, the 0xA4600010 PI_STATUS busy-poll SIGSEGV) are
    #     never created/reached. In reimplemented_funcs (symbol_lists.cpp:60).
    #   func_80075D80 (runtime 0x80075180) = osPiStartDma. asm/race_full_functions/func_80075D80.s:
    #     guard `if (__osPiDevMgr D_8008D0A0 == 0) return -1`; sets mb->hdr.type = direction ? 0xC
    #     (DMAWRITE=12) : 0xB (DMAREAD=11); fills the OSIoMesg (devAddr/dramAddr/size/retQueue); then
    #     `if (priority == 1 /*OS_MESG_PRI_HIGH*/) osJamMesg else osSendMesg` (the bne $t3,1 -> jal
    #     func_80074EE0 = osSendMesg). The recompiled body enqueues to the (now non-existent) manager
    #     cmd queue. Native osPiStartDma_recomp (librecomp pi.cpp:312) instead does the cart->RDRAM
    #     do_dma SYNCHRONOUSLY (devAddr | recomp::rom_base; needs no __osPiDevMgr handle and never
    #     checks active) and posts completion to the caller's own retQueue (the 7th arg, mq), which the
    #     caller osRecvMesg's. In reimplemented_funcs (symbol_lists.cpp:62).
    "func_800741F4": "osCreatePiManager",
    "func_80075D80": "osPiStartDma",
    # GFX-OSTASK SEAM (#58): the OSSched task dispatch func_800743D0 (osScExec-class) calls the
    # libultra RSP-task primitives, statically linked here as func_<splat>. Once BootLoadInitialAssets
    # was un-truncated (size 0x176C above) the main thread runs its dispatch loop, the scheduler
    # __scMain (func_80074884) wakes each retrace and reaches these -- which poke raw SP MMIO
    # (SP_STATUS 0xA4040010 via func_80086850=__osSpSetStatus, SP_PC 0xA4080000 via
    # func_800868A0=__osSpSetPc) past the 8 MB RDRAM bound -> SIGSEGV (verified live: crash in
    # func_80086850 from func_8007F41C from func_800743D0 from __scMain). func_800743D0 loads r18=task
    # into a0 for both calls below (verified from the recompiled body).
    #   func_8007F41C (runtime 0x8007E81C) = osSpTaskLoad: halts the RSP (__osSpSetStatus), sets PC,
    #     DMAs ucode/data. Native osSpTaskLoad_recomp (librecomp sp.cpp:6) is a NO-OP -- ultramodern
    #     reads the OSTask from RDRAM at submit time, so no SP DMA/PC setup is needed.
    #   func_8007F5AC (runtime 0x8007E9AC) = osSpTaskStartGo: waits for SP idle then writes SP_STATUS
    #     to start the RSP. Native osSpTaskStartGo_recomp (librecomp sp.cpp:12) calls
    #     ultramodern::submit_rsp_task(rdram, task) -> the gfx_thread -> renderer send_dl(OSTask*),
    #     i.e. THIS is the seam that hands the game's display list to the renderer. Both canonical
    #     names are in N64Recomp reimplemented_funcs (symbol_lists.cpp:27-28/184-185).
    "func_8007F41C": "osSpTaskLoad",
    "func_8007F5AC": "osSpTaskStartGo",
    # W136 (2026-07-04, #53): the scheduler's task-PREEMPTION arm runs for the first time once
    # audio voices actually start (real voice alloc func_80080040): __scMain's yield path
    # (func_80074704, runtime 0x80073B04) calls osSpTaskYield/osSpTaskYielded to preempt the
    # running gfx task for the audio task. Both poke raw SP MMIO (SP_STATUS 0xA4040010 via
    # func_80086850=__osSpSetStatus / func_80085930=__osSpGetStatus) -> SIGSEGV past the 8 MB
    # RDRAM bound (verified live: crash in func_80086850 <- func_8007F6A0 <- func_80074704).
    #   func_8007F6A0 (runtime 0x8007EAA0) = osSpTaskYield: `jal __osSpSetStatus(0x400)`
    #     (SP_SET_SIG0 = yield request). Native osSpTaskYield_recomp (librecomp sp.cpp:39).
    #   func_8007F180 (runtime 0x8007E580) = osSpTaskYielded: reads SP_STATUS via
    #     __osSpGetStatus, tests 0x100 (SP_STATUS_YIELDED) / 0x80 (SP_STATUS_YIELD). Native
    #     osSpTaskYielded_recomp (librecomp sp.cpp:43). Both canonical names in N64Recomp
    #     reimplemented_funcs (symbol_lists.cpp:29-30/185-186).
    "func_8007F6A0": "osSpTaskYield",
    "func_8007F180": "osSpTaskYielded",
}

# (W135, 2026-07-04, #53: the LAMBO_REAL_AUDIO_POST A/B lever introduced by W134 is FLIPPED INTO
# THE DEFAULT and DELETED. The real event-post func_8007FDCC, constructor B's merge, and the real
# MIDI dispatcher func_80077E5C are now unconditional. The W134 state-6 stick was NOT the cmd-0x11
# ack semantics: it was a cooperative-scheduler livelock in the boot stop-and-drain busy-wait --
# fixed by the func_80079720 yielding native (NATIVE_OVERRIDES above). Proven: default state walk
# 0->8 (state 8 @ vi 1139 vs 1144 pre-flip), ~30 fps sustained, real pipeline live.)

FUNC_RE = re.compile(
    r'\{\s*name\s*=\s*"(?P<name>[^"]+)"\s*,\s*vram\s*=\s*(?P<vram>0x[0-9A-Fa-f]+)\s*,'
    r'\s*size\s*=\s*(?P<size>0x[0-9A-Fa-f]+)\s*\}'
)
SIZE_RE = re.compile(
    r'name\s*=\s*"(?P<name>[^"]+)"\s*,\s*size\s*=\s*(?P<size>0x[0-9A-Fa-f]+)'
)


def parse_dump(path):
    """Return list of [name, vram, size] from a --dump-context .text section."""
    text = path.read_text()
    funcs = []
    for m in FUNC_RE.finditer(text):
        funcs.append([m["name"], int(m["vram"], 16), int(m["size"], 16)])
    if not funcs:
        sys.exit(f"ERROR: no functions parsed from {path}")
    return funcs


def parse_race_sizes(path):
    """Return {name: size} from the function_sizes array of n64recomp_race.toml."""
    text = path.read_text()
    m = re.search(r"function_sizes\s*=\s*\[(.*?)\]", text, re.DOTALL)
    if not m:
        sys.exit(f"ERROR: no function_sizes array in {path}")
    return {s["name"]: int(s["size"], 16) for s in SIZE_RE.finditer(m.group(1))}


def parse_race_name_list(path, key):
    """Return the ordered list of names from a `key = [ "a", "b", ... ]` array."""
    text = path.read_text()
    m = re.search(rf"\b{key}\s*=\s*\[(.*?)\]", text, re.DOTALL)
    if not m:
        return []
    return re.findall(r'"([^"]+)"', m.group(1))


def apply_merges(funcs, size_overrides):
    """Replicate ELF-mode manually_sized_funcs: override a head function's size by name to
    span its split tails. Crucially we KEEP the tail symbols (do NOT delete them) -- exactly
    what ELF mode does. The tails stay as valid jal targets (so n64recomp resolves calls to
    them instead of inventing wrongly-sized statics) and the race `stubs` list neutralises the
    ones whose small body would otherwise "branch outside" (stubbed funcs are not decoded).
    Override names absent from the dump (heads splat named but not emitted as FUNC symbols in
    the combined ELF) are ADDED at their name-derived splat VRAM."""
    by_name = {f[0]: f for f in funcs}
    overridden = 0
    added = []
    for name, size in size_overrides.items():
        if name in by_name:
            by_name[name][2] = size
            overridden += 1
        else:
            m = re.fullmatch(r"func_([0-9A-Fa-f]{8})", name)
            if m:
                funcs.append([name, int(m.group(1), 16), size])
                added.append(name)
    funcs.sort(key=lambda f: f[1])
    apply_merges.added = added
    return funcs, overridden, 0


def scan_jal_targets():
    """Scan the raw big-endian ROM code region for every `jal` target (runtime addresses).
    These are direct call sites; each must be a function start or n64recomp invents a
    wrongly-sized `static_` there (it auto-sizes statics and trips 'branching outside')."""
    rom = ROM_FILE.read_bytes()
    targets = set()
    for off in range(CODE_ROM_START, CODE_ROM_END, 4):
        w = struct.unpack_from(">I", rom, off)[0]
        if (w >> 26) == 3:  # JAL
            tgt = 0x80000000 | ((w & 0x03FFFFFF) << 2)
            if SECTION_VRAM <= tgt < (CODE_ROM_END + 0x7FFFF400):
                targets.add(tgt)
    return targets


def add_missing_call_targets(runtime):
    """Add a named function for every jal target that is not already a function start, so
    n64recomp resolves the call instead of creating a mis-sized static. Size = gap to the
    next start. Names keep the splat convention (func_<splat>) for consistency."""
    starts = {f[1] for f in runtime}
    all_starts = sorted(starts)
    targets = scan_jal_targets()
    missing = sorted(t for t in targets if t not in starts)
    added = []
    for t in missing:
        # size = distance to the next existing-or-added start
        nxt = next((s for s in all_starts if s > t), CODE_ROM_END + 0x7FFFF400)
        size = nxt - t
        splat = t + SPLAT_SHIFT if t >= SECTION_VRAM else t
        runtime.append([f"func_{splat:08X}", t, size])
        all_starts = sorted(set(all_starts) | {t})
        added.append(t)
    runtime.sort(key=lambda f: f[1])
    return added


def main():
    funcs = parse_dump(DUMP)
    n_dump = len(funcs)
    overrides = parse_race_sizes(RACE_TOML)
    funcs, n_over, n_dropped = apply_merges(funcs, overrides)

    # apply split-merge corrections: grow the head to its real size, drop the absorbed tail(s)
    merge_by_name = {f[0]: f for f in funcs}
    merge_drop = set()
    for head, (new_size, tails) in SPLIT_MERGES.items():
        if head in merge_by_name:
            merge_by_name[head][2] = new_size
        merge_drop.update(tails)
    funcs = [f for f in funcs if f[0] not in merge_drop]

    # apply delay-slot boundary corrections (drop mislabeled symbol, claim real start)
    drop_names = {fx[0] for fx in BOUNDARY_FIXES}
    funcs = [f for f in funcs if f[0] not in drop_names]
    funcs.extend([[name, vram, size] for _, name, vram, size in BOUNDARY_FIXES])

    # add named functions absent from the dump (would otherwise become bad statics)
    funcs.extend([[name, vram, size] for name, vram, size, _ in EXTRA_FUNCS])
    for _n, _v, _s, _disp in EXTRA_FUNCS:
        assert _disp in ("stub", "ignore", "emit"), f"EXTRA_FUNCS {_n}: bad disposition {_disp!r}"
    extra_stub = [name for name, _, _, disp in EXTRA_FUNCS if disp == "stub"]
    extra_ignore = [name for name, _, _, disp in EXTRA_FUNCS if disp == "ignore"]
    # disp == "emit": symbol registered (above) but added to neither stubs nor ignored -> n64recomp
    # emits the real body (used for func_8007867C, the title event-scheduler handler -- see EXTRA_FUNCS).

    # add prologue (splat addrs) then shift everything to runtime
    funcs.extend([list(p) for p in PROLOGUE])
    runtime = []
    for name, vram, size in funcs:
        rt = vram - SPLAT_SHIFT if vram >= SHIFT_THRESHOLD else vram
        runtime.append([name, rt, size])
    runtime.sort(key=lambda f: f[1])

    # complete coverage: add a function at every jal target that isn't already a start
    jal_added = add_missing_call_targets(runtime)

    # apply canonical libultra naming overrides (route-to-native / ignore) BEFORE building the
    # `present` set, so stub/ignored filtering and the emitted names stay consistent.
    renamed = []
    for f in runtime:
        canon = LIBULTRA_NAMES.get(f[0])
        if canon:
            renamed.append((f[0], canon))
            f[0] = canon

    # validate bounds / alignment; compute section span
    max_end = 0
    for name, vram, size in runtime:
        if vram % 4 or size % 4:
            sys.exit(f"ERROR: {name} not word-aligned vram={vram:#x} size={size:#x}")
        rom = vram - SECTION_VRAM + SECTION_ROM
        if rom < 0 or rom + size > ROM_SIZE:
            sys.exit(f"ERROR: {name} rom out of bounds rom={rom:#x} size={size:#x}")
        max_end = max(max_end, vram + size)
    section_size = (max_end - SECTION_VRAM + 0xF) & ~0xF

    lines = [
        "# Autogenerated by recomp/gen_syms_toml.py -- DO NOT EDIT BY HAND.",
        "# Runtime-addressed whole-ROM symbols for ultramodern/librecomp (ADR 0002 / #54).",
        "# Source: --dump-context of boot_menu_race_combined.elf + race split-merges + prologue,",
        "# shifted to runtime VRAM (splat - 0xC00 for addrs >= 0x80001000). Splat names kept.",
        "[[section]]",
        'name = ".text"',
        f"rom = {SECTION_ROM:#06x}",
        f"vram = {SECTION_VRAM:#010x}",
        f"size = {section_size:#x}",
        "",
        "functions = [",
    ]
    for name, vram, size in runtime:
        lines.append(f'    {{ name = "{name}", vram = {vram:#010x}, size = {size:#x} }},')
    lines.append("]")
    OUT.write_text("\n".join(lines) + "\n")

    # ---- generate the ROM-mode config with race stubs/ignored, filtered to present names ----
    # `stubs` = emitted-but-empty body; `ignored` = not emitted (split tails / problem funcs the
    # proven race recipe skips). n64recomp HARD-ERRORS if a stub/ignored name has no symbol, so we
    # filter to the names that survived the merge (absorbed tails are gone).
    present = {f[0] for f in runtime}
    race_stubs = [n for n in parse_race_name_list(RACE_TOML, "stubs") if n in present and n not in UNSTUB]
    race_ignored = [n for n in parse_race_name_list(RACE_TOML, "ignored") if n in present]
    race_stubs += [n for n in extra_stub if n in present and n not in race_stubs]
    race_ignored += [n for n in extra_ignore if n in present and n not in race_ignored]
    race_ignored += [n for n in NATIVE_OVERRIDES if n in present and n not in race_ignored]
    force = list(FORCE_STUB)
    if FORCE_STUB_FILE.exists():
        force += [ln.strip() for ln in FORCE_STUB_FILE.read_text().splitlines() if ln.strip()]
    race_stubs += [n for n in force if n in present and n not in race_stubs]
    # Emit (don't ignore) functions the whole-ROM link references and that emit cleanly.
    race_ignored = [n for n in race_ignored if n not in FORCE_EMIT]
    # A name cannot be both stubbed and ignored (n64recomp hard-errors): stub wins, since a
    # force-stubbed function must be emitted (empty) to satisfy the whole-ROM link.
    race_ignored = [n for n in race_ignored if n not in race_stubs]

    def toml_array(names):
        if not names:
            return "[]"
        return "[\n" + ",\n".join(f'    "{n}"' for n in names) + ",\n]"

    cfg = f'''# AUTOGENERATED by recomp/gen_syms_toml.py -- DO NOT EDIT BY HAND.
# Runtime-addressed whole-ROM recompile config for the modern n64recomp
# (ultramodern + librecomp stack, ADR 0002 / epic #54, pivot phase 2).
#
# ROM+syms mode (drmario64 template model): feeds the RAW big-endian .z64 + a runtime-addressed
# symbol table. n64recomp reads code bytes from the ROM at each function's derived rom offset
# (rom = vram - 0x80000400 + 0x1000) and renames the function at vram==0x80000400 && rom==0x1000
# to `recomp_entrypoint`. ALL paths are relative to THIS file's directory (the repo root).
#
# The top-level `function_sizes` key is ELF-mode ONLY (ignored here); the race split-function
# merges are already baked into the syms.toml `size` fields. stubs/ignored carried from
# tools/n64recomp_race.toml, filtered to names present after the merge.
#
# Run (see BUILDING.md step 3):
#   cmake --build build --target N64RecompCLI
#   ./build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp lamborghini.us.toml


[input]
entrypoint = 0x80000400
output_func_path = "RecompiledFuncs"
symbols_file_path = "lamborghini.syms.toml"
rom_file_path = "Automobili Lamborghini (USA).z64"

[patches]
stubs = {toml_array(race_stubs)}
ignored = {toml_array(race_ignored)}
{PATCH_BLOCKS}'''
    CONFIG.write_text(cfg, encoding="utf-8")

    print(f"dump funcs:        {n_dump}")
    print(f"size overrides hit: {n_over} / {len(overrides)}")
    print(f"override funcs added (absent from dump): {getattr(apply_merges, 'added', [])}")
    print(f"split tails dropped:{n_dropped}")
    print(f"prologue added:    {len(PROLOGUE)}")
    print(f"jal targets added: {len(jal_added)}")
    print(f"libultra renamed:  {len(renamed)} {[f'{a}->{b}' for a, b in renamed]}")
    print(f"total emitted:     {len(runtime)}")
    print(f"section .text:     rom={SECTION_ROM:#x} vram={SECTION_VRAM:#x} size={section_size:#x}")
    print("entrypoint func @  0x80000400: "
          + next((n for n, v, s in runtime if v == SECTION_VRAM), "<MISSING!>"))
    print(f"stubs kept:        {len(race_stubs)}  ignored kept: {len(race_ignored)}")
    print(f"wrote {OUT}")
    print(f"wrote {CONFIG}")


if __name__ == "__main__":
    main()
