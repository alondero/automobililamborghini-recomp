// Hand-provided no-op symbols for this ROM's libultra CP0/kernel helpers that the pivot routes
// AWAY from recompilation (epic #54, phase 3). These functions are named canonically in
// recomp/gen_syms_toml.py (LIBULTRA_NAMES) so N64Recomp marks them `ignored` (symbol_lists.cpp)
// and emits no body.
//
// N64Recomp renames BOTH reimplemented AND ignored functions to `<name>_recomp` at the call site
// (main.cpp:430-440); only `reimplemented` names also get a librecomp native. So an `ignored`
// helper's callers emit `<name>_recomp(...)` with NO definition anywhere -> we must supply it here
// under that exact suffix, rather than forking the vendored submodule to add a native.
//
// __osSetSR: the ROM's `mtc0 $a0,Status; jr $ra` (func_8007D260, verified from bytes). ultramodern
// REPLACES the CP0/interrupt kernel with native threads, so writing N64 Status bits is correctly a
// no-op (this is exactly why librecomp's cop0_status_write aborts on non-FR Status writes). FR-mode
// float addressing is set up by the runtime itself, so dropping the write is safe -- matches how
// drmario64/Zelda64Recomp leave __osSetSR in ignored_funcs.

#include "recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void __osSetSR_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

// __osViInit: the ROM's low-level VI init (func_8007E120, runtime 0x8007D520, verified from bytes).
// Faithful translation of the RDRAM half of the body: bzero the two 0x30 OSViContexts at D_8008D140,
// point __osViCurr/__osViNext (D_8008D1A0/A4) at them, retraceCount=1, framep=K0BASE, pick the default
// OSViMode by osTvType (0=PAL->D_8008D4E0, 2=MPAL->D_8008D530, else NTSC->D_8008D580 = osViModeNtscLan1),
// next->state=0x20 (VI_STATE_BLACK), next->control=modep->comRegs.ctrl. The MMIO tail (poll VI_CURRENT
// 0xA4400010, zero VI_CONTROL, jal __osViSwapContext runtime 0x80085010) is owned by ultramodern's VI
// manager and stays untranslated -- promote_vi_context (recomp/src/main.cpp) HLEs __osViSwapContext's
// context promotion at retrace cadence. The default mode seeded here is overwritten almost immediately
// by the game's own osViSetMode (func_80075C60: LPN2 at boot from osCreateScheduler, LAN2 from the game
// SM), matching hardware ordering exactly. Reached via osCreateScheduler (func_80074B90) ->
// func_8007ED10 -> here. W115, #58.
void __osViInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    gpr vi   = (gpr)(int32_t)0x8008D140u; /* OSViContext[2]; [1] at +0x30 */
    gpr modep;
    uint32_t tv;
    int i;
    (void)ctx;

    for (i = 0; i < 0x60; i += 4) MEM_W(i, vi) = 0;                    /* bzero(vi, 0x60) */
    MEM_W(0, (gpr)(int32_t)0x8008D1A0u) = (int32_t)0x8008D140u;        /* __osViCurr = &vi[0] */
    MEM_W(0, (gpr)(int32_t)0x8008D1A4u) = (int32_t)0x8008D170u;        /* __osViNext = &vi[1] */
    MEM_H(0x32, vi) = 1;                                               /* vi[1].retraceCount = 1 */
    MEM_H(0x02, vi) = 1;                                               /* vi[0].retraceCount = 1 */
    MEM_W(0x34, vi) = (int32_t)0x80000000u;                            /* next->framep = K0BASE */
    MEM_W(0x04, vi) = (int32_t)0x80000000u;                            /* curr->framep = K0BASE */
    tv = (uint32_t)MEM_W(0, (gpr)(int32_t)0x80000300u);                /* osTvType (librecomp seeds 1) */
    modep = (gpr)(int32_t)(tv == 0 ? 0x8008D4E0u : tv == 2 ? 0x8008D530u : 0x8008D580u);
    MEM_W(0x38, vi) = (int32_t)modep;                                  /* next->modep */
    MEM_H(0x30, vi) = 0x20;                                            /* next->state = VI_STATE_BLACK */
    MEM_W(0x3C, vi) = MEM_W(0x4, modep);                               /* next->control = modep->ctrl */
}

// --- controller-read bridge (#64/#53, 2026-06-29) ------------------------------------------
// func_8007F780 (runtime 0x8007EB80) is the ROM's raw SI controller read (SIGSEGVs if recompiled:
// raw SI_STATUS MMIO via __osSiDeviceBusy). It was routed to the native osContStartReadData, which
// polls input + fires send_si_message (so the game's osRecvMesg unblocks) but does NOT fill the
// game's raw PIF buffer passed in a1 (=D_8011C6D0). The game then decoded a 0xff "no response"
// status (func_8007A7CC -> func_80083100 returns 2), the object-slot registration gate func_8007A8A0
// bailed, and boot stuck at state 6 (ares reaches state 8: D_800CE6AC 2->3->4->6->8). This bridge
// keeps the native poll/send_si AND fills the buffer + controller-init globals from ultramodern,
// replicating the SI read's observable effect. ares reference (state 6 attract):
//   D_8011C6D0[0] = FF 03 21 02 <btn_lo> <btn_hi> 00 00  (status byte 0 = clean, no pak)
//   count D_8011C681 = 4 ; mode D_8011C680 = 1 ; D_8011C640[i] = FF 01 04 01 00 00 00 00
typedef struct { uint16_t button; signed char stick_x; signed char stick_y; unsigned char err_no; } LamboPad;
void osContGetReadData(void* pads);                                  /* ultramodern (fills OSContPad[4]) */
int  osContSetCh(uint8_t* rdram, unsigned char ch);                  /* ultramodern: sets max_controllers */
void osContStartReadData_recomp(uint8_t* rdram, recomp_context* ctx); /* librecomp native */

// Joybus data CRC (the N64 pak-block CRC, poly 0x85) over the 32-byte payload at `addr`.
// Same algorithm as libultra's __osContDataCrc; the PIF returns it INVERTED (crc ^ 0xFF)
// when no pak is inserted, which is exactly how kernels detect an empty socket.
static unsigned char lambo_joybus_data_crc(uint8_t* rdram, gpr addr) {
    unsigned char crc = 0;
    int i, b;
    for (i = 0; i <= 32; i++) {
        for (b = 7; b >= 0; b--) {
            unsigned char x = (crc & 0x80) ? 0x85 : 0;
            crc = (unsigned char)(crc << 1);
            if (i < 32 && (MEM_BU(i, addr) & (1u << b))) crc |= 1;
            crc ^= x;
        }
    }
    return crc;
}

// --- controller-pak backing store (#69) ---------------------------------------------------
// This game hand-rolls its SI kernel, but its pak READ/WRITE drivers (func_80083F70 /
// func_80084EE0) are EMITTED REAL in the pivot: they build joybus block-read/-write frames
// (cmd 0x02 / 0x03) and issue them through the SI bridge (func_8007F780 -> lambo_joybus_answer).
// So a full, savable Controller Pak needs only that the bridge ANSWER those frames from a
// host-side 32 KB SRAM image (persisted to a .mpk file) instead of the W121 "no pak" convention.
//
// The image must be FORMATTED, exactly as the reference emulator (ares) formats a freshly
// created pak: the game's osPfsInitPak (func_8007A8A0) validates the ID-area checksums and
// rejects an all-zero (unformatted) pak with PFS_ERR_ID_FATAL -- the game itself never formats
// a dead pak. Port of the proven legacy HLE (src/recomp/recomp_support.c lambo_pak_image_format,
// W16): ID areas at pages 1/3/4/6 (device id bit0, 1 bank = 32 KB, id16 + inverted checksums),
// inode table page 1 + backup page 2 with slots 5..127 = 0x03 (empty).
uint8_t g_lambo_pak_image[0x8000];

static void lambo_pak_format(void) {
    uint8_t* img = g_lambo_pak_image;
    static const uint8_t id_areas[4] = { 1, 3, 4, 6 };
    int a, h, page, slot, i;
    memset(img, 0, sizeof(g_lambo_pak_image));
    for (a = 0; a < 4; a++) {
        uint32_t base = (uint32_t)id_areas[a] * 0x20u;
        uint16_t checksum = 0, inverted = 0;
        img[base + 0x01] = 0x2A;                                 /* n6 random field */
        img[base + 0x04] = 0x00; img[base + 0x05] = 0x05;        /* serial (fixed; game only */
        img[base + 0x06] = 0xA1; img[base + 0x07] = 0xB0;        /*  validates the checksums) */
        img[base + 0x08] = 0x05; img[base + 0x09] = 0x07;
        img[base + 0x0A] = 0xC3; img[base + 0x0B] = 0x39;
        img[base + 0x18] = 0x00; img[base + 0x19] = 0x01;        /* device id: bit0 set */
        img[base + 0x1A] = 0x01;                                 /* banks: 1 = 32 KB */
        img[base + 0x1B] = 0x00;                                 /* version */
        for (h = 0; h < 14; h++) {
            uint16_t data = (uint16_t)((img[base + h * 2] << 8) | img[base + h * 2 + 1]);
            checksum = (uint16_t)(checksum + data);
            inverted = (uint16_t)(inverted + (uint16_t)~data);
        }
        img[base + 0x1C] = (uint8_t)(checksum >> 8); img[base + 0x1D] = (uint8_t)checksum;
        img[base + 0x1E] = (uint8_t)(inverted >> 8); img[base + 0x1F] = (uint8_t)inverted;
    }
    /* TOC checksum (#31 follow-up): an 8-bit sum of ONLY the low byte of each inode word for
     * slots 5..127 (BLOCK_EMPTY markers), NOT a sum over the whole 256-byte sector -- verified
     * against libdragon's __get_toc_checksum (src/joybus/mempak.c), the reference SDK-compatible
     * Controller Pak filesystem implementation. Getting this wrong makes the game's own
     * osPfsInitPak-equivalent (func_8007A8A0) classify a freshly-formatted pak as
     * PFS_ERR_INVALID (return code 4) -- ID area valid, TOC "corrupted" -- which is exactly the
     * unrepairable "repair controller pak?" loop this was causing. */
    for (page = 1; page <= 2; page++) {
        uint32_t sum = 0;
        for (slot = 0; slot < 128; slot++) img[0x100 * page + slot * 2 + 0x01] = 0x03;
        for (i = 5; i < 128; i++) sum += img[0x100 * page + i * 2 + 0x01];
        img[0x100 * page + 0x01] = (uint8_t)(sum & 0xFFu);
    }
}

// Both the path and the enable flag are read from the environment ONCE and cached: env vars can't
// change after startup, and lambo_joybus_answer is on the per-frame input-poll hot path, so a
// getenv() per call is wasted work. Caching the path into our own buffer also avoids holding a
// pointer into getenv's storage.
static const char* lambo_pak_path(void) {
    static char cached[1024];
    static int init = 0;
    if (!init) {
        const char* p = getenv("LAMBO_CONTROLLER_PAK_FILE");
        if (!p || !*p) p = "lambo_controller_pak.mpk";
        snprintf(cached, sizeof(cached), "%s", p);
        init = 1;
    }
    return cached;
}

// Controller-pak feature is ON by default (this is the #69 deliverable). LAMBO_CONTROLLER_PAK=0
// is a deliberate A/B opt-out (the old W121 "no pak" behaviour) for boot-stability debugging.
static int lambo_pak_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* e = getenv("LAMBO_CONTROLLER_PAK");
        cached = (e && e[0] == '0') ? 0 : 1;
    }
    return cached;
}

// Lazy load: read the .mpk on first pak access; if absent/short, format a fresh pak IN MEMORY.
// We deliberately do NOT write the file here -- a fresh pak has nothing to persist yet, and the
// 32 KB write is slow on WSL /mnt drives, so keeping it off the boot/read hot path avoids stalling
// the game thread during boot. The file is created on the first real save (lambo_pak_save).
static void lambo_pak_ensure_loaded(void) {
    static int loaded = 0;
    FILE* f;
    if (loaded) return;
    loaded = 1;
    f = fopen(lambo_pak_path(), "rb");
    if (f) {
        size_t n = fread(g_lambo_pak_image, 1, sizeof(g_lambo_pak_image), f);
        fclose(f);
        if (n == sizeof(g_lambo_pak_image)) return;             /* good image on disk */
    }
    lambo_pak_format();                                         /* fresh formatted pak (memory only) */
}

// Persist the whole 32 KB image after a block write. ATOMIC: write a temp file, then rename it
// over the target -- a crash/kill/power-loss mid-write must never truncate the live .mpk (a short
// read on next boot would silently reformat and lose ALL saved games). rename() is atomic on POSIX
// and replaces; on Windows (MinGW/MSVCRT) it fails if the target exists, so fall back to
// remove()+rename(). We flush per block (each write persisted immediately) rather than batching --
// this trades a small hitch at save time for crash-safety, which for a save file is the right call
// (saves are rare: records/best-times, a handful of blocks).
static void lambo_pak_save(void) {
    const char* path = lambo_pak_path();
    char tmp[1024];
    FILE* f;
    size_t n;
    if ((int)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return; /* path too long */
    f = fopen(tmp, "wb");
    if (!f) return;
    n = fwrite(g_lambo_pak_image, 1, sizeof(g_lambo_pak_image), f);
    if (fclose(f) != 0 || n != sizeof(g_lambo_pak_image)) { remove(tmp); return; }
    if (rename(tmp, path) != 0) {          /* Windows: target exists -> replace explicitly */
        remove(path);
        if (rename(tmp, path) != 0) remove(tmp);
    }
}

// LAMBO_PAK_TRACE=1: per-frame joybus log (issue #35 diagnosis) -- shows exactly what the save
// flow asks the pak for and how the bridge answered, since call-level tracers proved the game
// loops on reads without ever reaching a write.
static int lambo_pak_trace(void) {
    static int cached = -1;
    if (cached < 0) { const char* e = getenv("LAMBO_PAK_TRACE"); cached = (e && e[0] == '1') ? 1 : 0; }
    return cached;
}

// Rumble sink: defined extern "C" in main.cpp; publishes the motor state to the SDL pad.
// Weak-ish decoupling: the headless/self-test paths never link a pad, but the symbol always
// exists (main.cpp is always in the link), so a plain extern is fine.
extern void lambo_pak_set_rumble(int on);

// Answer a 0x40-byte joybus/PIF command buffer IN PLACE, the way the PIF hardware does (#69).
// The game hand-rolls its SI kernel and stages real joybus frames: 0x00 = channel skip,
// 0xFF = pad byte, 0xFE = end, 0xFD = channel reset, else [tx][rx][cmd + tx-1 args][rx resp].
// We emulate 4 standard controllers (matching ares: count D_8011C681 = 4) with controller 0
// carrying live input AND a formatted Controller Pak (a Rumble Pak in the same virtual socket --
// see below); sockets 1-3 are empty.
//   cmd 0x00/0xFF (status/reset) -> type 0x05 0x00 + pak-status (bit0 = pak present on ch0)
//   cmd 0x01 (read)              -> buttons HIGH,LOW + stick x,y (channel 0; others zero)
//   cmd 0x02 (pak read)  ch0     -> 32 bytes from the .mpk image (blocks < 0x8000), or the bank/
//                                   rumble-detect echo byte31=0x80 (blocks >= 0x8000), + REAL CRC
//   cmd 0x03 (pak write) ch0     -> store block into the image + flush file (blocks < 0x8000);
//                                   MOTOR block 0xC000 -> drive SDL rumble; + REAL CRC
//   pak read/write on empty sockets -> INVERTED CRC ("no pak"), the empty-socket convention.
//
// One virtual socket answers BOTH accessory probes on ch0. The game runs two independent
// detections during its pak scan (func_80069710): osPfsInitPak (func_8007A8A0) reads the
// formatted ID area -> memory pak present (records/best-times save to blocks 0-6); osMotorInit
// (func_8007AF60) writes bank 0x80 to block 0x400 (addr 0x8000) and requires read-back byte31 ==
// 0x80 -> rumble pak present. A real controller can hold only one accessory, but the emulator can
// satisfy both because their probe regions are disjoint (RAM < 0x8000 vs bank/detect >= 0x8000),
// which is exactly how the legacy HLE (src/recomp) presented the pak. The MOTOR write to block
// 0x600 (addr 0xC000) is answered here and forwarded to SDL rumble -- it fires ONLY when the ROM
// itself issues an osMotorStart-style write, so rumble is faithful (never synthesised by us).
//
// FAITHFULNESS NOTE (#105, corrected 2026-07-11): Contrary to the prior note, the game DOES drive
// the motor during gameplay (e.g. collisions, off-road) via its custom start/stop wrappers
// (func_8006A7A0/func_8006A82C). These wrappers call libultra's osMotorStart (func_8007AC78) and
// osMotorStop (func_8007AB10). However, the ROM's pak scan (func_80069710) runs osPfsInitPak first
// and skips osMotorInit if it succeeds. Because our virtual socket satisfies both, the game would
// normally think a Controller Pak is present and skip rumble initialization. While we force the
// present flag (0x80110F08) to 1 every frame inside func_8007F780 to enable the wrappers, skipping
// osMotorInit leaves the OSPfs struct uninitialized (e.g. null queue), which would crash or submit
// all-zero buffers during raw PIF DMA. To prevent this and achieve clean dual-pak coexistence, we
// stub out osMotorStart and osMotorStop, directly intercepting the start/stop requests and natively
// driving the SDL rumble.
static void lambo_joybus_answer(uint8_t* rdram, gpr buf, const LamboPad* pads) {
    int pos = 0, channel = 0;
    int pak_ch0 = lambo_pak_enabled();  /* ch0 carries a formatted pak (#69) */
    if (pak_ch0) lambo_pak_ensure_loaded();
    while (pos < 0x40) {
        unsigned t = MEM_BU(pos, buf);
        if (t == 0xFE) break;
        if (t == 0xFF) { pos++; continue; }
        if (t == 0x00 || t == 0xFD) { pos++; channel++; continue; }
        {
            int tx = (int)(t & 0x3F);
            unsigned rxb = MEM_BU(pos + 1, buf);
            int rx = (int)(rxb & 0x3F);
            unsigned cmd;
            gpr resp;
            int has_pak;
            if (pos + 2 + tx + rx > 0x40) break;
            cmd = MEM_BU(pos + 2, buf);
            has_pak = (channel == 0) && pak_ch0;
            MEM_B(pos + 1, buf) = (signed char)rx; /* clear stale error bits */
            resp = buf + (gpr)(pos + 2 + tx);
            if (channel < 4) {
                switch (cmd) {
                case 0x00: case 0xFF: /* status / reset: standard controller */
                    if (rx > 0) MEM_B(0, resp) = 0x05;
                    if (rx > 1) MEM_B(1, resp) = 0x00;
                    if (rx > 2) MEM_B(2, resp) = has_pak ? 0x01 : 0x00; /* bit0 = pak present */
                    break;
                case 0x01: { /* controller read: canonical order, byte0 = button HIGH */
                    uint16_t btn = (channel == 0) ? pads[0].button : 0;
                    if (rx > 0) MEM_B(0, resp) = (signed char)((btn >> 8) & 0xFF);
                    if (rx > 1) MEM_B(1, resp) = (signed char)(btn & 0xFF);
                    if (rx > 2) MEM_B(2, resp) = (channel == 0) ? pads[0].stick_x : 0;
                    if (rx > 3) MEM_B(3, resp) = (channel == 0) ? pads[0].stick_y : 0;
                    break;
                }
                case 0x02: { /* pak block read (real frame: rx = 0x21 = 32 data + 1 data-CRC) */
                    int k;
                    /* A present-pak read is only well-defined for the full 33-byte frame; guarding
                     * on rx==33 keeps lambo_joybus_data_crc (which always folds 32 bytes) from
                     * reading past the ndata bytes actually written on a degenerate short frame. */
                    if (has_pak && rx == 33) {
                        /* addr = 2 tx bytes after the cmd; low 5 bits are the address-CRC */
                        unsigned addr = ((MEM_BU(pos + 3, buf) << 8) | MEM_BU(pos + 4, buf)) & 0xFFE0u;
                        if (addr >= 0x8000u) {                 /* bank/rumble-detect region */
                            for (k = 0; k < 32; k++) MEM_B(k, resp) = 0;
                            MEM_B(31, resp) = (signed char)0x80; /* motor-init echo (byte31==0x80) */
                        } else {                               /* RAM: serve the .mpk image */
                            for (k = 0; k < 32; k++)
                                MEM_B(k, resp) = (signed char)g_lambo_pak_image[(addr + k) & 0x7FFFu];
                        }
                        /* present pak -> REAL (non-inverted) data CRC over the 32-byte block */
                        MEM_B(32, resp) = (signed char)lambo_joybus_data_crc(rdram, resp);
                    } else {                                   /* empty socket / short frame: zero + inverted CRC */
                        for (k = 0; k + 1 < rx; k++) MEM_B(k, resp) = 0;
                        if (rx > 0) MEM_B(rx - 1, resp) = (signed char)0xFF;
                    }
                    if (lambo_pak_trace())
                        /* gate the addr bytes on tx>=3 (same as the WRITE tracer): a degenerate
                         * short read frame near the end of the 0x40 buffer passes the pos+2+tx+rx
                         * guard but has no addr bytes, so reading pos+3/pos+4 would run off the end */
                        fprintf(stderr, "[paktrc] READ  ch=%d tx=%d rx=%d addr=%04x served=%s crc=%02x\n",
                                channel, tx, rx,
                                (unsigned)(tx >= 3 ? (((MEM_BU(pos + 3, buf) << 8) | MEM_BU(pos + 4, buf)) & 0xFFE0u) : 0xFFFFu),
                                (has_pak && rx == 33) ? "image" : "nopak",
                                (unsigned)(rx > 0 ? MEM_BU(rx - 1, resp) : 0));
                    break;
                }
                case 0x03: { /* pak block write (32-byte payload after the 2 addr bytes) */
                    gpr data = buf + (gpr)(pos + 5);
                    unsigned char crc = (tx >= 3) ? lambo_joybus_data_crc(rdram, data) : 0;
                    if (has_pak && tx >= 3) {
                        unsigned addr = ((MEM_BU(pos + 3, buf) << 8) | MEM_BU(pos + 4, buf)) & 0xFFE0u;
                        if (addr == 0xC000u) {                 /* MOTOR control block -> SDL rumble */
                            lambo_pak_set_rumble(MEM_BU(0, data) != 0);
                        } else if (addr < 0x8000u) {           /* RAM: persist into the .mpk image */
                            int k;
                            for (k = 0; k < 32; k++) g_lambo_pak_image[(addr + k) & 0x7FFFu] = (uint8_t)MEM_BU(k, data);
                            lambo_pak_save();
                        }
                        /* else bank-select (0x8000): accept, no store */
                        if (rx > 0) MEM_B(0, resp) = (signed char)crc;          /* accepted */
                    } else if (rx > 0 && tx >= 3) {
                        MEM_B(0, resp) = (signed char)(crc ^ 0xFF);             /* no pak */
                    }
                    if (lambo_pak_trace())
                        fprintf(stderr, "[paktrc] WRITE ch=%d tx=%d rx=%d addr=%04x data0=%02x served=%s ack=%02x\n",
                                channel, tx, rx,
                                (unsigned)(tx >= 3 ? (((MEM_BU(pos + 3, buf) << 8) | MEM_BU(pos + 4, buf)) & 0xFFE0u) : 0xFFFFu),
                                (unsigned)(tx >= 3 ? MEM_BU(0, data) : 0),
                                (has_pak && tx >= 3) ? "image" : "nopak",
                                (unsigned)(rx > 0 ? MEM_BU(0, resp) : 0));
                    break;
                }
                default: /* unknown command: no response */
                    if (lambo_pak_trace())
                        fprintf(stderr, "[paktrc] UNKN  ch=%d tx=%d rx=%d cmd=%02x (no response)\n", channel, tx, rx, cmd);
                    MEM_B(pos + 1, buf) = (signed char)(rx | 0x80);
                    break;
                }
            } else { /* channels beyond the 4 controller sockets: nothing plugged in */
                MEM_B(pos + 1, buf) = (signed char)(rx | 0x80);
            }
            pos += 2 + tx + rx;
            channel++;
        }
    }
}

// Permanent test knob (LAMBO_INPUT_PROBE, same knob as the button probe below; asserted by
// tests/pivot/test_pak_status_channel.py): one-shot report of the FIRST joybus frame in the
// status/pak SI buffer (D_8011C6D0) AFTER the bridge has handled the call. It scans the buffer
// the way the PIF does (0x00 = channel skip, 0xFF = pad, 0xFE = end, else [tx][rx][cmd..][resp..])
// and prints the command byte plus the first three response bytes -- for a joybus STATUS frame
// (cmd 0x00) those are the device type (0x05 0x00) and the pak-status byte (bit0 = pak present).
static void lambo_pak_channel_probe(uint8_t* rdram, gpr buf) {
    static int pak_probe_logged = 0;
    int pos = 0;
    if (pak_probe_logged || !getenv("LAMBO_INPUT_PROBE")) return;
    while (pos < 0x40) {
        unsigned t = MEM_BU(pos, buf);
        if (t == 0xFE) return;
        if (t == 0xFF || t == 0x00 || t == 0xFD) { pos++; continue; }
        {
            int tx = (int)(t & 0x3F);
            int rx = (int)(MEM_BU(pos + 1, buf) & 0x3F);
            unsigned cmd = MEM_BU(pos + 2, buf);
            gpr resp = buf + (gpr)(pos + 2 + tx);
            if (pos + 2 + tx + rx > 0x40) return;
            pak_probe_logged = 1;
            fprintf(stderr, "[pak] first frame in status/pak buffer: cmd=%02x resp=%02x %02x %02x\n",
                    cmd, (unsigned)MEM_BU(0, resp), (unsigned)MEM_BU(1, resp), (unsigned)MEM_BU(2, resp));
            return;
        }
    }
}

// func_8007FFF0 (runtime 0x8007F3F0) = alSynAddPlayer -- NATIVE OVERRIDE (W134, #53; ignored via
// NATIVE_OVERRIDES in gen_syms_toml.py). Reproduces the ROM body's RDRAM ops exactly (see
// asm/race_full_functions/func_8007FFF0.s: samplesLeft=curSamples; client->next=synth->head;
// synth->head=client; bracketed by osSetIntMask -- a native C body is atomic w.r.t. the
// cooperative game-thread scheduler, strictly stronger than that bracket) EXCEPT that the new
// client's first voice-handler dispatch is DEFERRED BY ONE AUDIO FRAME
// (samplesLeft = curSamples + ~16ms of samples). NAMED TIMING ADAPTATION: the SFX-player
// constructor (func_80076EE4) links the handler and only primes the current-event buffer a few
// dozen instructions later; real hardware never dispatches inside that ~us window (16.6ms
// retrace period), but the cooperative scheduler delivers backlogged retraces at dispatch points
// inside the window and dispatched the handler against an unprimed event -> SIGSEGV in
// func_8007699C (measured W134). The <=1-frame deferral is the same slack the hardware retrace
// cadence provides. Remove if ultramodern gains preemptive external-message delivery.
void func_8007FFF0(uint8_t* rdram, recomp_context* ctx) {
    gpr synth  = ctx->r4; /* a0 */
    gpr client = ctx->r5; /* a1 */
    int32_t cur  = MEM_W(0X20, synth);  /* synth->curSamples */
    int32_t rate = MEM_W(0X44, synth);  /* synth->outputRate (samples/sec) */
    /* FOUR audio frames (~64ms), not one: under the cooperative scheduler each audio frame hands
     * the registering thread exactly ONE dispatch-gap of progress (retrace backlog ping-pong),
     * and the constructor needs two more gaps after registration (post-exit, nextEvent-complete)
     * before the current-event buffer is primed. Measured W134: a 1-frame deferral still crashed
     * (dispatch on the 2nd frame, boot had only reached the post). Boot-time one-shot, inaudible. */
    int32_t defer = (int32_t)(((int64_t)rate * 64000) / 1000000);
    if (defer <= 0) defer = 4096;       /* rate not yet set: any plausible 4-frame slack */
    MEM_W(0X10, client) = cur + defer;  /* ROM: = cur (immediate first dispatch) */
    MEM_W(0X0, client) = MEM_W(0X0, synth);
    MEM_W(0X0, synth) = client;
}

// NOTE: named func_8007F780 (NOT _recomp): a RACE-ignored GAME function's call sites keep the bare
// name (n64recomp only renames libultra symbol_lists names to _recomp); we supply the bare symbol.
void func_8007F780(uint8_t* rdram, recomp_context* ctx) {
    gpr buf;
    LamboPad pads[4];
    int c, k;

    buf = ctx->r5;                          /* a1 = game controller read buffer (D_8011C6D0) */
    osContStartReadData_recomp(rdram, ctx); /* native: poll_input + send_si (unblocks osRecvMesg) */

    /* Force the rumble pak present flag to 1 on controller 0 every frame, coexisting with Controller Pak */
    MEM_W(0, (gpr)(int32_t)0x80110F08u) = 1;

    /* Zero ALL fields (incl. stick_x/y): osContGetReadData only fills pads[c] for
     * c < max_controllers, which is 0 until osContInit runs. Early boot reads (vi~51)
     * hit this before init, so uninitialized stick_x/y would otherwise leak stack garbage
     * into the buffer bytes 6,7 below and non-deterministically corrupt the state machine. */
    for (c = 0; c < 4; c++) { pads[c].button = 0; pads[c].stick_x = 0; pads[c].stick_y = 0; pads[c].err_no = 0; }
    /* This game never calls osContInit (it reads the SI directly through this bridge; its caller
     * func_8007A1E4 is an ignored no-op), so ultramodern's max_controllers stayed 0 -- and
     * osContGetReadData loops `controller < max_controllers`, so get_input was NEVER polled and
     * pads[] stayed zero. That silently dropped ALL input on every platform (#68). Seed the count
     * here (idempotent; osContInit's only side effect we depend on) so get_input is actually read. */
    osContSetCh(rdram, 4);
    osContGetReadData(pads);

    /* controller-init globals (the ROM's SI-probe func_8007A1E4 sets these; ignored in the pivot) */
    MEM_B(0, (gpr)(int32_t)0x8011C680u) = 0x01; /* mode  */
    MEM_B(0, (gpr)(int32_t)0x8011C681u) = 0x04; /* count = 4 (ares) */
    for (c = 0; c < 4; c++) {
        gpr s = (gpr)(int32_t)0x8011C640u + (gpr)(c * 8);
        MEM_B(0, s) = (signed char)0xFF; MEM_B(1, s) = 0x01; MEM_B(2, s) = 0x04; MEM_B(3, s) = 0x01;
        MEM_B(4, s) = 0; MEM_B(5, s) = 0; MEM_B(6, s) = 0; MEM_B(7, s) = 0;
    }
    /* The game reads the SI through TWO buffers carrying DIFFERENT joybus commands (#68 W120 got
     * the menu buffer right but misread the second one; corrected #69):
     *  - D_8011C640 (BOOT/MENU): controller READ frames (FF 01 04 01; cmd 0x01). Decoder
     *    func_80074DF4 does button = lhu(bytes 4,5) -- byte4 = HIGH, standard joybus order. The
     *    ROM function that stages these frames (func_8007A1E4) is an ignored no-op in the pivot,
     *    so the init-globals loop above stages them on the bridge's behalf every call.
     *  - D_8011C6D0: the STATUS + PAK command channel, NOT an in-race pad buffer. func_8007A6D0
     *    stages STATUS frames (FF 01 03 00; response = type 05 00 + pak-status byte, decoded by
     *    func_8007A7CC / func_80083100), and func_8008418C / func_800850E4 stage 32-byte pak
     *    READ/WRITE frames (FF 03 21 02 / FF 23 01 03). The game stages these itself -- the
     *    bridge must ANSWER them per command, not overwrite them with pad data.
     * lambo_joybus_answer parses whichever buffer the game passed and responds like the PIF. */
    int std_read = (buf == (gpr)(int32_t)0x8011C640u);
    (void)k;
    lambo_joybus_answer(rdram, buf, pads);
    /* One-shot diagnostic (env-gated -- LAMBO_INPUT_PROBE -- so it stays OUT of the default/play
     * build's hot path). It reports two things the pivot regression test asserts on:
     *   1. that live input REACHES the game at all (buttons != 0)      -> guards the #68 W119 fix
     *      (osContSetCh, without which max_controllers stayed 0 and get_input was never polled), and
     *   2. the bytes AS WRITTEN to the destination buffer (byte4/byte5) -> guards the W120 byte-order
     *      fix (a revert would flip them, which the reach-only check could not catch). */
    if (!std_read) lambo_pak_channel_probe(rdram, buf);
    if (getenv("LAMBO_INPUT_PROBE") &&
        (pads[0].button != 0 || pads[0].stick_x != 0 || pads[0].stick_y != 0)) {
        static int input_probe_logged = 0;
        if (!input_probe_logged) {
            input_probe_logged = 1;
            fprintf(stderr, "[input] pad read reached game: buttons=%04x dest=%s bytes45=%02x %02x stick=(%d,%d)\n",
                    (unsigned)pads[0].button, std_read ? "menu" : "race",
                    (unsigned char)MEM_BU(4, buf), (unsigned char)MEM_BU(5, buf),
                    (int)pads[0].stick_x, (int)pads[0].stick_y);
        }
    }
    ctx->r2 = 0; /* osContStartReadData returns 0 on success */
}

void func_8007AC78(uint8_t* rdram, recomp_context* ctx) {
    /* osMotorStart / __osMotorAccess: VRAM 0x8007a078
     * Natively bypasses the uninitialized pfs->queue and pfs->channel (which remain zero/uninitialized
     * since osMotorInit is skipped when a Controller Pak is detected) and directly drives the motor. */
    int channel = ((int32_t)ctx->r4 - 0x80110D68) / 40;
    if (lambo_pak_trace()) fprintf(stderr, "[pak] MOTOR START ch=%d\n", channel);
    if (channel == 0) {
        lambo_pak_set_rumble(1);
    }
    ctx->r2 = 0; /* return 0 (success) */
}

void func_8007AB10(uint8_t* rdram, recomp_context* ctx) {
    /* osMotorStop: VRAM 0x80079f10 */
    int channel = ((int32_t)ctx->r4 - 0x80110D68) / 40;
    if (lambo_pak_trace()) fprintf(stderr, "[pak] MOTOR STOP ch=%d\n", channel);
    if (channel == 0) {
        lambo_pak_set_rumble(0);
    }
    ctx->r2 = 0; /* return 0 (success) */
}
