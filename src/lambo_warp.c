// Developer warp menu (issue #12, survey pattern A13: Banjo warps_table, Dr Mario
// scene_table): jump straight to a race on any circuit without driving the menus.
//
// The ROM funnels every race start through one path: menu screens park their
// selections in a cursor cluster at 0x800CE6xx, the RACE menu action quiesces audio
// (func_800676B4 stop-and-drain + func_80008ECC) and stores 7 into the game-state
// halfword 0x800CE6AC, and the state-7 handler (splat boot_pad_apply_calibration,
// runtime 0x80005A24) copies the cursors into the live race config keyed by mode.
// The warp performs exactly those menu-side stores and the same state write -- the
// ROM's own finalizer does everything else, so nothing about the race is invented.
//
// Trigger paths (both consumed on the game thread by lambo_warp_tick, hooked at the
// entry of the per-frame game-logic dispatcher func_800030F8, runtime 0x800024F8):
//   - F1..F6 (SDL keyboard, published from input_sample in main.cpp): warp to that
//     circuit as a 1-player single race with the current defaults below.
//   - LAMBO_WARP=circuit[:laps[:car[:players]]] (env, circuit 1-6): one-shot boot
//     warp, fired at the first frame the game is in a warpable state.
//
// Cluster map (all halfwords; MEM_H handles guest byte order):
//   0x800CE6A4 players (1-4)          0x800CE6C0 track cursor (0-5; 0-2 if 3+ players)
//   0x800CE6A6 track-select-entered   0x800CE6E0 laps cursor (menu range 3-30)
//   0x800CE6B4 mode (2 = SINGLE RACE) 0x800CE7E8 player model cursors (0-23, +2 per player)
//   0x800CE7A4 difficulty (0 = NOVICE, 1 = EXPERT), NOT a car selector
//   0x800CE6BA race counter (0)       0x800CE6AC game state (7 = load race)

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lambo_log.h"
#include "recomp.h"
#include "lambo_vehicle.h"

// Audio quiesce pair the ROM runs before every state->7 write (menu RACE action,
// pause-restart, championship next-race). Recompiled bodies; safe to call from a
// func_800030F8 entry hook (that function is argument-free and both callees keep
// the guest stack balanced).
void func_800676B4(uint8_t* rdram, recomp_context* ctx);
void func_80008ECC(uint8_t* rdram, recomp_context* ctx);

#define WARP_PLAYERS    0x800CE6A4u
#define WARP_TRACK_FLAG 0x800CE6A6u
#define WARP_MODE       0x800CE6B4u
#define WARP_COUNTER    0x800CE6BAu
#define WARP_TRACK_CUR  0x800CE6C0u
#define WARP_LAPS_CUR   0x800CE6E0u
#define MODE_SINGLE_RACE 2

// The game's circuits are unnamed (menu shows "CIRCUIT" + a numbered map preview;
// ROM has no track-name strings), so the scene table mirrors that presentation.
// The basic/pro split is documented in docs/TRACK_INDEX.md, not in these labels —
// the labels feed runtime log output, and the basic/pro category is a doc concern.
static const char* const lambo_scene_table[6] = {
    "CIRCUIT 1", "CIRCUIT 2", "CIRCUIT 3", "CIRCUIT 4", "CIRCUIT 5", "CIRCUIT 6",
};

// Request packing: bit 31 = pending, bits 0-7 circuit (0-based), 8-15 laps,
// 16-23 car, 24-27 players. Published by the SDL main thread / env parse,
// exchanged to 0 by the game-thread tick.
static _Atomic uint32_t g_warp_request;
static _Atomic int g_warp_applied;
static _Atomic int g_warp_failed;
static _Atomic int g_warp_circuit;
static _Atomic int g_warp_laps;
static _Atomic int g_warp_car;
static _Atomic int g_warp_players;
static _Atomic int g_warp_mode;

int lambo_warp_env_applied(void) {
    return atomic_load_explicit(&g_warp_applied, memory_order_acquire);
}
int lambo_warp_env_failed(void) {
    return atomic_load_explicit(&g_warp_failed, memory_order_acquire);
}
int lambo_warp_env_circuit(void) {
    return atomic_load_explicit(&g_warp_circuit, memory_order_relaxed);
}
int lambo_warp_env_laps(void) {
    return atomic_load_explicit(&g_warp_laps, memory_order_relaxed);
}
int lambo_warp_env_car(void) {
    return atomic_load_explicit(&g_warp_car, memory_order_relaxed);
}
int lambo_warp_env_players(void) {
    return atomic_load_explicit(&g_warp_players, memory_order_relaxed);
}
int lambo_warp_env_mode(void) {
    return atomic_load_explicit(&g_warp_mode, memory_order_relaxed);
}
static int g_warp_options_parsed;
static int g_warp_options_valid = 1;
static int g_warp_mode_option = MODE_SINGLE_RACE;
static int g_warp_difficulty = -1;

static uint32_t pack_request(int circuit0, int laps, int car, int players) {
    return 0x80000000u | (uint32_t)(circuit0 & 0xFF) | ((uint32_t)(laps & 0xFF) << 8)
         | ((uint32_t)(car & 0xFF) << 16) | ((uint32_t)(players & 0xF) << 24);
}

// Hotkey entry point (called from main.cpp on the SDL thread). circuit0 = 0-based.
void lambo_warp_request(int circuit0) {
    if (circuit0 < 0 || circuit0 > 5) return;
    atomic_store_explicit(&g_warp_request, pack_request(circuit0, 3, 0, 1),
                          memory_order_relaxed);
}

// LAMBO_WARP=circuit[:laps[:car[:players]]], circuit 1-based to match the game UI.
static void parse_env_request(void) {
    const char* env = getenv("LAMBO_WARP");
    if (env == NULL || env[0] == '\0') return;
    int v[4] = { 0, 3, 0, 1 };
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", env);
    char* p = buf;
    for (int i = 0; i < 4 && p != NULL && *p != '\0'; i++) {
        v[i] = (int)strtol(p, NULL, 10);
        p = strchr(p, ':');
        if (p != NULL) p++;
    }
    if (v[0] < 1 || v[0] > 6) {
        atomic_store_explicit(&g_warp_failed, 1, memory_order_release);
        LAMBO_LOG("warp", "LAMBO_WARP=%s invalid: circuit must be 1-6\n", env);
        return;
    }
    if (v[2] < 0 || v[2] >= 24) {
        LAMBO_LOG("warp", "LAMBO_WARP=%s invalid: model must be 0-23\n", env);
        return;
    }
    atomic_store_explicit(&g_warp_request, pack_request(v[0] - 1, v[1], v[2], v[3]),
                          memory_order_relaxed);
}

// Environment options are process configuration, not per-frame state. Parse them
// once before looking at the atomic request so a bad difficulty cannot consume a
// valid request and leave the menu half-transitioned.
static void parse_env_options(void) {
    const char* mode = getenv("LAMBO_WARP_MODE");
    if (mode != NULL && mode[0] != '\0') {
        char* end = NULL;
        long value = strtol(mode, &end, 10);
        if (end == mode || *end != '\0' || value < -32768 || value > 32767) {
            LAMBO_LOG("warp", "LAMBO_WARP_MODE=%s invalid: expected an integer\n", mode);
            g_warp_options_valid = 0;
        } else {
            g_warp_mode_option = (int)value;
        }
    }

    const char* difficulty = getenv("LAMBO_WARP_DIFFICULTY");
    if (difficulty != NULL) {
        if (strcmp(difficulty, "0") != 0 && strcmp(difficulty, "1") != 0) {
            LAMBO_LOG("warp", "LAMBO_WARP_DIFFICULTY must be 0 or 1\n");
            g_warp_options_valid = 0;
            atomic_store_explicit(&g_warp_failed, 1, memory_order_release);
        } else {
            g_warp_difficulty = difficulty[0] - '0';
        }
    }
}

// States a warp is allowed to fire from, mirroring where the ROM itself issues the
// state->7 write: 6 = menu (RACE action), 8 = in race (pause restart), and the
// attract flow states 3-5 (title/attract screens; the attract handoff enters its
// demo race the same way). 7 (already loading) and boot states 0-2 hold the
// request pending.
static int warpable(int state) {
    return state >= 3 && state <= 8 && state != 7;
}

// Per-frame tick, injected at the entry of func_800030F8 (see the [[patches.hook]]
// block carried by scripts/gen_syms_toml.py). Runs on the game thread.
void lambo_warp_tick(uint8_t* rdram, recomp_context* ctx) {
    if (!g_warp_options_parsed) {
        g_warp_options_parsed = 1;
        parse_env_options();
        parse_env_request();
    }
    if (!g_warp_options_valid) return;
    uint32_t req = atomic_load_explicit(&g_warp_request, memory_order_relaxed);
    if (!(req & 0x80000000u)) return;

    int state = (int16_t)MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_GAME_STATE_ADDR);
    if (!warpable(state)) return;
    if (!atomic_compare_exchange_strong_explicit(&g_warp_request, &req, 0,
                                                 memory_order_relaxed,
                                                 memory_order_relaxed)) {
        return;
    }

    int circuit0 = (int)(req & 0xFF);
    int laps     = (int)((req >> 8) & 0xFF);
    int car      = (int)((req >> 16) & 0xFF);
    int players  = (int)((req >> 24) & 0xF);
    if (players < 1) players = 1;
    if (players > 4) players = 4;
    if (players >= 3 && circuit0 > 2) circuit0 = 2;  // ROM offers 3 tracks to 3-4 players
    if (laps < 1) laps = 1;
    if (laps > 30) laps = 30;                        // menu lap range tops out at 30

    // 0x800CE6B4 selects the race mode the state-7 finalizer builds: 2 = single race
    // (LAP/RANK HUD), 0 = time trial (PREVIOUS/RECORD/BEST-LAP HUD, no rank). Overridable
    // for testing the non-arcade HUD variants (issue #42); defaults to single race.
    const int mode = g_warp_mode_option;
    // The retail difficulty callback XORs this halfword with 1. Keep the
    // existing setting unless explicitly overridden for a controlled run.
    if (g_warp_difficulty >= 0)
        MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_MENU_DIFFICULTY_ADDR) = (int16_t)g_warp_difficulty;
    MEM_H(0, (gpr)(int32_t)WARP_PLAYERS)    = (int16_t)players;
    MEM_H(0, (gpr)(int32_t)WARP_TRACK_FLAG) = 1;
    MEM_H(0, (gpr)(int32_t)WARP_MODE)       = (int16_t)mode;
    MEM_H(0, (gpr)(int32_t)WARP_COUNTER)    = 0;
    MEM_H(0, (gpr)(int32_t)WARP_TRACK_CUR)  = (int16_t)circuit0;
    MEM_H(0, (gpr)(int32_t)WARP_LAPS_CUR)   = (int16_t)laps;
    func_800676B4(rdram, ctx);
    func_80008ECC(rdram, ctx);
    // The menu maintains one model cursor per player.  A warp request carries a
    // single model selector, so mirror it to every requested player rather than
    // silently leaving players 2-4 on stale menu selections.
    for (int player = 0; player < players; ++player) {
        MEM_H(0, (gpr)(int32_t)(LAMBO_GUEST_PLAYER_MODEL_CURSOR_ADDR +
                                (uint32_t)player * LAMBO_GUEST_PLAYER_MODEL_CURSOR_STRIDE)) =
            (int16_t)car;
    }

    MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_GAME_STATE_ADDR) = 7;

    atomic_store_explicit(&g_warp_circuit, circuit0 + 1, memory_order_relaxed);
    atomic_store_explicit(&g_warp_laps, laps, memory_order_relaxed);
    atomic_store_explicit(&g_warp_car, car, memory_order_relaxed);
    atomic_store_explicit(&g_warp_players, players, memory_order_relaxed);
    atomic_store_explicit(&g_warp_mode, mode, memory_order_relaxed);
    atomic_store_explicit(&g_warp_applied, 1, memory_order_release);

    const char* mode_name = mode == 0 ? "time trial" :
                            mode == MODE_SINGLE_RACE ? "single race" : "custom mode";
    LAMBO_LOG("warp", "%s: %s (mode %d), %d lap(s), car %d, %d player(s) (from state %d)\n",
            lambo_scene_table[circuit0], mode_name, mode, laps, car, players, state);
}
