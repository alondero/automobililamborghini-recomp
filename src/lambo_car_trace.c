// Optional per-frame trace for validating the car-model evidence. It runs on the
// game thread but stays disabled unless LAMBO_CAR_TRACE is set; when enabled,
// buffered line writes keep instrumentation from changing frame timing. The
// record format and guest addresses are documented in CAR_DIFFERENCES.md.

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "recomp.h"
#include "lambo_vehicle.h"

#define TRACE_VBASE   LAMBO_VEHICLE_BASE
#define TRACE_VSTRIDE ((unsigned)sizeof(LamboVehicleRecord))
#define TRACE_VEHICLES 6
#define TRACE_E290    0x8013E290u
#define TRACE_E2D0    0x8013E2D0u
#define TRACE_TABLE_BYTES 64
#define TRACE_LINE_CAP 2048
#define TRACE_FLUSH_INTERVAL 64

static FILE* g_trace;
static unsigned g_frame;
static int g_state = -1;  // -1 = not yet initialised
static int g_trace_write_failed;

static size_t trace_hex(char* out, size_t used, size_t cap,
                        const uint32_t* words, unsigned count) {
    static const char digits[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < count; ++i) {
        if (used + 8 > cap) break;
        const uint32_t word = words[i];
        for (int shift = 28; shift >= 0; shift -= 4)
            out[used++] = digits[(word >> shift) & 0xFu];
    }
    return used;
}

static void trace_close(void) {
    if (g_trace == NULL) return;
    fflush(g_trace);
    fclose(g_trace);
    g_trace = NULL;
}

void lambo_car_trace_tick(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    if (g_state < 0) {
        g_state = getenv("LAMBO_CAR_TRACE") != NULL;
        if (g_state) {
            const char* path = getenv("LAMBO_CAR_TRACE_PATH");
            g_trace = fopen(path != NULL ? path : "lambo_car_trace.txt", "w");
            if (g_trace == NULL) {
                fprintf(stderr, "[car-trace] cannot open %s: %s\n",
                        path != NULL ? path : "lambo_car_trace.txt", strerror(errno));
                g_state = 0;
            } else {
                atexit(trace_close);
            }
        }
    }
    if (!g_state) return;

    g_frame++;
    const int st = (int16_t)MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_GAME_STATE_ADDR);
    const int sel = (int16_t)MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_RACE_DIFFICULTY_ADDR);
    const int cur = (int16_t)MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_MENU_DIFFICULTY_ADDR);
    const int slot = (int16_t)MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_CURRENT_VEHICLE_ADDR);
    char line[TRACE_LINE_CAP];
    int line_len = snprintf(line, sizeof(line), "F %u st %d sel %d cur %d slot %d\n",
                            g_frame, st, sel, cur, slot);
    if (line_len > 0 && (size_t)line_len < sizeof(line))
        fwrite(line, 1, (size_t)line_len, g_trace);
    if (st != 8) {
        return;
    }

    uint32_t e290_words[TRACE_TABLE_BYTES / sizeof(uint32_t)];
    uint32_t e2d0_words[TRACE_TABLE_BYTES / sizeof(uint32_t)];
    // MEM_W captures this hook's local RDRAM pointer; keep the read loop here
    // instead of passing a redundant pointer through a helper.
#define TRACE_READ_WORDS(words, count, addr) \
    do { \
        for (unsigned i = 0; i < (count); ++i) \
            (words)[i] = (uint32_t)MEM_W(0, (gpr)(int32_t)((addr) + i * 4)); \
    } while (0)
    TRACE_READ_WORDS(e290_words, sizeof(e290_words) / sizeof(e290_words[0]), TRACE_E290);
    TRACE_READ_WORDS(e2d0_words, sizeof(e2d0_words) / sizeof(e2d0_words[0]), TRACE_E2D0);
    for (int v = 0; v < TRACE_VEHICLES; v++) {
        const uint32_t base = TRACE_VBASE + (uint32_t)v * TRACE_VSTRIDE;
        const int ch = (int16_t)MEM_H(0, (gpr)(int32_t)(base + offsetof(LamboVehicleRecord, channel)));
        if (ch <= 0) continue;  // unpopulated slot: channel is 1-based
        const int cat = (int)MEM_HU(0, (gpr)(int32_t)(base + offsetof(LamboVehicleRecord, category)));
        const int speed = (int)MEM_W(0, (gpr)(int32_t)(base + offsetof(LamboVehicleRecord, speed)));
        const int thr = (int16_t)MEM_H(0, (gpr)(int32_t)(base + offsetof(LamboVehicleRecord, throttle_demand)));
        const unsigned brk = (unsigned)MEM_W(0, (gpr)(int32_t)(base + offsetof(LamboVehicleRecord, brake_demand)));
        const int lat = (int16_t)MEM_H(0, (gpr)(int32_t)(base + offsetof(LamboVehicleRecord, brake_latch)));
        line_len = snprintf(line, sizeof(line),
                            "R %d ch %d cat %d sp %d thr %d brk %08X lat %d rec ",
                            v, ch, cat, speed, thr, brk, lat);
        if (line_len <= 0 || (size_t)line_len >= sizeof(line)) continue;
        uint32_t record_words[TRACE_VSTRIDE / sizeof(uint32_t)];
        TRACE_READ_WORDS(record_words, sizeof(record_words) / sizeof(record_words[0]), base);
        size_t used = (size_t)line_len;
        used = trace_hex(line, used, sizeof(line) - 1, record_words,
                         sizeof(record_words) / sizeof(record_words[0]));
        const char e2[] = " e2 ";
        if (used + sizeof(e2) - 1 >= sizeof(line)) continue;
        memcpy(line + used, e2, sizeof(e2) - 1); used += sizeof(e2) - 1;
        used = trace_hex(line, used, sizeof(line) - 1, e290_words,
                         sizeof(e290_words) / sizeof(e290_words[0]));
        const char ed[] = " ed ";
        if (used + sizeof(ed) - 1 >= sizeof(line)) continue;
        memcpy(line + used, ed, sizeof(ed) - 1); used += sizeof(ed) - 1;
        used = trace_hex(line, used, sizeof(line) - 1, e2d0_words,
                         sizeof(e2d0_words) / sizeof(e2d0_words[0]));
        if (used + 1 >= sizeof(line)) continue;
        line[used++] = '\n';
        fwrite(line, 1, used, g_trace);
    }
    if ((g_frame % TRACE_FLUSH_INTERVAL) == 0 && fflush(g_trace) != 0 && !g_trace_write_failed) {
        fprintf(stderr, "[car-trace] write failed after frame %u: %s\n",
                g_frame, strerror(errno));
        g_trace_write_failed = 1;
    }
#undef TRACE_READ_WORDS
}
