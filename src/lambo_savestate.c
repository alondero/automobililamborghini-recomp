// Developer save-state: snapshot the guest's RAM at a frame boundary and restore it
// later, so a hand-found moment (e.g. a car parked where a rendering bug reproduces)
// can be returned to deterministically -- interactively (F7 save / F8 load) or headless
// (LAMBO_STATE_LOAD=<file>) so an autonomous debugging agent can re-reach the spot with
// no human at the wheel. Issue #22 (A24).
//
// What is captured: the low 8 MiB of RDRAM (rdram[0 .. 0x800000)), which is the entire
// guest-addressable N64 RAM (osMemSize reports 8 MiB) -- every object table, the camera,
// the car transforms, the race/state-machine word all live here. The extended region
// above 8 MiB (PI handles at 0x80800000, the mod heap at 0x81000000) is boot-static setup,
// identical every run, so it is neither saved nor restored. A raw memcpy preserves RDRAM's
// byte-swizzle (MEM_B's ^3), so save and restore are symmetric with no un-swizzle.
//
// Why this is a frame-boundary operation, not a true resumable save-state: each guest
// OSThread is a real native std::thread with its CPU register file (recomp_context) on a
// native C stack that cannot be serialised. So we snapshot/restore ONLY RDRAM, and do it at
// the entry of the per-frame dispatcher (func_800028D0, the same hook the warp uses) -- the
// point where the game is about to read the state word and rebuild the frame from RAM.
// Restoring RDRAM there and letting the dispatcher run rebuilds the frame from the restored
// world. This works because N64 games keep persistent state in RAM and derive transient
// CPU/RSP state each frame; it is a debug tool, not a play-anywhere quicksave.
//
// A load overwrites the state word (0x800CE6AC) too, so a snapshot taken mid-race is
// self-contained: loading it even from the attract screen drops straight into that race.
//
// USAGE
//   Interactive: F7 saves the current RAM to the slot file, F8 restores it.
//   Headless:    LAMBO_STATE_SAVE=<file> auto-captures a spot (state/delay gated);
//                LAMBO_STATE_LOAD=<file> auto-loads on a cold boot -> lands on the spot.
//   LAMBO_STATE_FILE overrides the F7/F8 slot path (default lambo_savestate.lstate).
//
// RELIABILITY / THE ONE RULE: SETTLE BEFORE YOU SAVE.
//   Only guest RAM is captured; each of the game's 7 threads also has native execution state
//   (registers, native call stack) that CANNOT be snapshotted. The main game thread is safe
//   because this hook pins it at a fixed frame boundary, but the background asset/PI-loader
//   thread (func_80074884) is not -- if a snapshot is taken WHILE it is streaming track/car
//   data off the cart, restoring RAM under it leaves it mid-job with mismatched native state,
//   which crashes or freezes on load (non-deterministically). The fix is procedural, not code:
//   drive to the spot, PARK and wait a few seconds until the scene has fully loaded, THEN save.
//   A settled snapshot loads reliably; a snapshot grabbed right after entering a race may not.
//
// Another consequence of the same limit: the memcpy runs on the game thread, so the cooperative
// scheduler keeps the other GUEST threads parked -- but the NATIVE RT64 renderer / VI / audio
// threads are not cooperatively scheduled and keep reading RDRAM during the copy. In practice a
// settled restore has been stable, but a mid-scene load can momentarily race those readers (a
// torn frame, or -- with the audio voice tables swapped out from under the audio thread --
// garbled sound). This is a debug tool; hardening the load against those native readers (pause
// RT64/VI + audio-quiesce like the warp does) is tracked as a follow-up.
//
// Threading mirrors src/lambo_warp.c: the SDL main thread only flips an atomic request bit
// (F7/F8 edge-detected in main.cpp's input_sample); the actual RAM copy happens on the game
// thread inside lambo_savestate_tick, at the frame-boundary hook.

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lambo_log.h"

#include "recomp.h"

// After a wholesale RDRAM restore, guest RAM's OSThread.context fields hold the SAVE process's
// native pointers (dangling here). ultramodern re-points them to this process's live thread
// contexts (see lib/N64ModernRuntime patch). This is sound only for OSThreads the loading
// process already has: the game's 7 threads are all created during boot, so a cold-boot load
// covers them, but any thread the snapshot captured that this process lacks keeps a dangling
// context and the scheduler would fault on it -- another reason to snapshot only settled scenes.
extern void ultramodern_relink_thread_contexts(uint8_t* rdram);

#define STATE_VAR   0x800CE6ACu       // game-state halfword (see lambo_warp.c cluster map)
#define RDRAM_SNAP_SIZE 0x800000u     // low 8 MiB = guest-addressable N64 RAM (osMemSize)

// On-disk header (32 bytes). Written/read host-endian: a save-state is a local debug
// artifact, not a portable format. Magic+version+size are validated before any RAM is
// touched, so a truncated or foreign file is rejected rather than corrupting the game.
#define STATE_MAGIC "LMBOSTAT"
#define STATE_VERSION 1u
typedef struct {
    char     magic[8];      // "LMBOSTAT"
    uint32_t version;       // STATE_VERSION
    uint32_t rdram_size;    // RDRAM_SNAP_SIZE
    uint32_t state;         // captured state-machine word (informational)
    uint32_t reserved[3];
} state_header_t;

// Default file for the F7/F8 slot; LAMBO_STATE_FILE overrides. LAMBO_STATE_LOAD names a
// one-shot boot load (headless agent entry) and is parsed on the first tick.
#define DEFAULT_STATE_PATH "lambo_savestate.lstate"

// Request bits flipped by the SDL thread, consumed on the game thread.
#define REQ_SAVE 0x1u
#define REQ_LOAD 0x2u
static _Atomic uint32_t g_req;

static const char* slot_path(void) {
    const char* p = getenv("LAMBO_STATE_FILE");
    return (p != NULL && p[0] != '\0') ? p : DEFAULT_STATE_PATH;
}

// Snapshot rdram[0..8MiB) to <path> via a temp file + rename, so a crash mid-write can
// never leave a torn state file (same atomic-write discipline as lambo_pak_save).
static void do_save(uint8_t* rdram, const char* path) {
    char tmp[1100];
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp)) {
        LAMBO_LOG("state", "save: path too long: %s\n", path);
        return;
    }
    FILE* f = fopen(tmp, "wb");
    if (f == NULL) {
        LAMBO_LOG("state", "save: cannot open %s for write\n", tmp);
        return;
    }
    state_header_t h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, STATE_MAGIC, 8);
    h.version    = STATE_VERSION;
    h.rdram_size = RDRAM_SNAP_SIZE;
    h.state      = (uint16_t)MEM_H(0, (gpr)(int32_t)STATE_VAR);
    int ok = (fwrite(&h, 1, sizeof(h), f) == sizeof(h)) &&
             (fwrite(rdram, 1, RDRAM_SNAP_SIZE, f) == RDRAM_SNAP_SIZE);
    // Flush before rename so the rename publishes a fully-written file.
    ok = ok && (fflush(f) == 0);
    fclose(f);
    if (!ok) {
        LAMBO_LOG("state", "save: write failed for %s\n", path);
        remove(tmp);
        return;
    }
    // Publish atomically. POSIX rename() replaces the target in place; Windows rename() fails
    // if the target exists, so only there do we remove-then-retry. Crucially we do NOT remove
    // the existing good save until the first rename has failed, and on final failure we KEEP
    // the .tmp (it holds the full snapshot) -- so a previously-working save is never lost with
    // nothing to show for it.
    if (rename(tmp, path) != 0) {
        remove(path);
        if (rename(tmp, path) != 0) {
            LAMBO_LOG("state", "save: could not publish %s; snapshot kept at %s\n", path, tmp);
            return;
        }
    }
    LAMBO_LOG("state", "saved %u bytes to %s (state=%u)\n",
            RDRAM_SNAP_SIZE, path, h.state);
}

// Load <path> fully into a scratch buffer and validate BEFORE overwriting any live RAM, so
// a bad/short file aborts the load with the game untouched. Only on full success do we
// memcpy the payload over rdram[0..8MiB).
static void do_load(uint8_t* rdram, const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        LAMBO_LOG("state", "load: cannot open %s\n", path);
        return;
    }
    state_header_t h;
    if (fread(&h, 1, sizeof(h), f) != sizeof(h) ||
        memcmp(h.magic, STATE_MAGIC, 8) != 0) {
        LAMBO_LOG("state", "load: %s is not a save-state (bad magic)\n", path);
        fclose(f);
        return;
    }
    if (h.version != STATE_VERSION || h.rdram_size != RDRAM_SNAP_SIZE) {
        LAMBO_LOG("state", "load: %s version/size mismatch (v%u size %u)\n",
                path, h.version, h.rdram_size);
        fclose(f);
        return;
    }
    uint8_t* buf = (uint8_t*)malloc(RDRAM_SNAP_SIZE);
    if (buf == NULL) {
        LAMBO_LOG("state", "load: out of memory\n");
        fclose(f);
        return;
    }
    size_t got = fread(buf, 1, RDRAM_SNAP_SIZE, f);
    fclose(f);
    if (got != RDRAM_SNAP_SIZE) {
        LAMBO_LOG("state", "load: %s truncated (%zu/%u bytes)\n",
                path, got, RDRAM_SNAP_SIZE);
        free(buf);
        return;
    }
    memcpy(rdram, buf, RDRAM_SNAP_SIZE);
    free(buf);
    // Repair the native OSThread.context pointers the memcpy just clobbered with the save
    // process's addresses; without this the scheduler dereferences garbage on the next tick.
    ultramodern_relink_thread_contexts(rdram);
    LAMBO_LOG("state", "loaded %u bytes from %s (state=%u)\n",
            RDRAM_SNAP_SIZE, path, h.state);
}

// SDL-thread entry points (edge-detected in main.cpp). Only flip the request bit; the copy
// runs on the game thread at the next frame boundary.
void lambo_savestate_request_save(void) {
    atomic_fetch_or_explicit(&g_req, REQ_SAVE, memory_order_relaxed);
}
void lambo_savestate_request_load(void) {
    atomic_fetch_or_explicit(&g_req, REQ_LOAD, memory_order_relaxed);
}

// Per-frame tick, injected at func_800028D0 entry (+1 instruction, so it coexists with the
// warp hook at the same function -- N64Recomp rejects two hooks on the exact same vram).
// Runs on the game thread with rdram+ctx live.
void lambo_savestate_tick(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;

    // Env-driven headless triggers, parsed once. LAMBO_STATE_LOAD=<file> auto-loads at boot;
    // LAMBO_STATE_SAVE=<file> auto-saves a spot without a keypress (an agent can pair either
    // with LAMBO_WARP to capture/return to a race deterministically).
    static int env_checked;
    static const char* env_load;
    static int load_min_state;      // auto-load once state >= this (LAMBO_STATE_LOAD_STATE)
    static int load_delay;          // ticks to wait once eligible (LAMBO_STATE_LOAD_DELAY)
    static int load_ticks;          // ticks counted while eligible so far
    static const char* env_save;
    static int save_target_state;   // state to snapshot at (LAMBO_STATE_SAVE_STATE, default 8)
    static int save_delay;          // ticks to wait once in that state (LAMBO_STATE_SAVE_DELAY)
    static int save_ticks;          // ticks counted at the target state so far
    if (!env_checked) {
        env_checked = 1;
        const char* l = getenv("LAMBO_STATE_LOAD");
        env_load = (l != NULL && l[0] != '\0') ? l : NULL;
        const char* lm = getenv("LAMBO_STATE_LOAD_STATE");
        load_min_state = (lm != NULL) ? atoi(lm) : 3;   // 3 = past early boot by default
        const char* ld = getenv("LAMBO_STATE_LOAD_DELAY");
        load_delay = (ld != NULL) ? atoi(ld) : 0;
        const char* s = getenv("LAMBO_STATE_SAVE");
        env_save = (s != NULL && s[0] != '\0') ? s : NULL;
        const char* st = getenv("LAMBO_STATE_SAVE_STATE");
        save_target_state = (st != NULL) ? atoi(st) : 8;   // 8 = in-race (see lambo_warp.c)
        const char* d = getenv("LAMBO_STATE_SAVE_DELAY");
        save_delay = (d != NULL) ? atoi(d) : 0;
    }

    int state = (int16_t)MEM_H(0, (gpr)(int32_t)STATE_VAR);

    if (env_load != NULL && state >= load_min_state) {
        if (load_ticks++ >= load_delay) {
            const char* p = env_load;
            env_load = NULL;      // fire once
            do_load(rdram, p);
            return;               // don't also process a same-frame save/hotkey request
        }
    }

    if (env_save != NULL && state == save_target_state) {
        if (save_ticks++ >= save_delay) {
            const char* p = env_save;
            env_save = NULL;      // fire once
            do_save(rdram, p);
            return;
        }
    }

    // Manual F7/F8. Like lambo_warp's warpable() guard, hold the request PENDING (don't consume
    // it) until we are past early boot (state >= 3): by then the game's 7 threads exist, so a
    // load's relink can repair them, and we never restore a race snapshot over boot/attract RAM
    // that has not been set up yet. A keypress during boot simply fires on the first real frame.
    if (state < 3) return;
    uint32_t req = atomic_exchange_explicit(&g_req, 0, memory_order_relaxed);
    if (req == 0) return;
    if (req & REQ_LOAD) do_load(rdram, slot_path());   // load wins if both somehow set
    else if (req & REQ_SAVE) do_save(rdram, slot_path());
}
