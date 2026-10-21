#include "lambo_reset.h"
#include "lambo_log.h"
#include "lambo_vehicle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile int g_log_threshold = LAMBO_LEVEL_ERROR;
void lambo_log_write(LamboLogLevel level, const char* tag, const char* format, ...) {
    (void)level; (void)tag; (void)format;
}

static int stopped;
static int drained;
static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; \
} } while (0)

// Audio must quiesce before the title state is published, in retail order.
void func_800676B4(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    CHECK(MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_GAME_STATE_ADDR) == 8);
    CHECK(drained == stopped);
    ++stopped;
}
void func_80008ECC(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    CHECK(MEM_H(0, (gpr)(int32_t)LAMBO_GUEST_GAME_STATE_ADDR) == 8);
    CHECK(stopped == drained + 1);
    ++drained;
}

int main(void) {
    uint8_t* rdram = calloc(1, 8 * 1024 * 1024);
    uint8_t* expected = calloc(1, 8 * 1024 * 1024);
    if (!rdram || !expected) return 1;
    recomp_context ctx = {0};
    const gpr state = (gpr)(int32_t)LAMBO_GUEST_GAME_STATE_ADDR;
    const gpr tasks = (gpr)(int32_t)0x80098270u;

    MEM_H(0, state) = 8;
    CHECK(lambo_reset_tick(rdram, &ctx) == 0);
    CHECK(MEM_H(0, state) == 8 && stopped == 0);
    CHECK(lambo_reset_generation() == 0);
    lambo_reset_request();
    lambo_reset_request();
    const int unsafe_states[] = {0, 1, 2, 7, -1, 17};
    for (size_t i = 0; i < sizeof(unsafe_states) / sizeof(unsafe_states[0]); ++i) {
        MEM_H(0, state) = unsafe_states[i];
        memcpy(expected, rdram, 8 * 1024 * 1024);
        CHECK(lambo_reset_tick(rdram, &ctx) == 0);
        CHECK(memcmp(expected, rdram, 8 * 1024 * 1024) == 0 && stopped == 0);
    }
    MEM_H(0, state) = 8;
    for (int count = 2; count > 0; --count) {
        MEM_H(0, tasks) = count;
        memcpy(expected, rdram, 8 * 1024 * 1024);
        CHECK(lambo_reset_tick(rdram, &ctx) == 1);
        CHECK(memcmp(expected, rdram, 8 * 1024 * 1024) == 0 && stopped == 0);
    }
    MEM_H(0, tasks) = 0;
    MEM_H(0, state) = 3;
    memcpy(expected, rdram, 8 * 1024 * 1024);
    MEM_H(0, state) = 8;
    CHECK(lambo_reset_tick(rdram, &ctx) == 0);
    CHECK(stopped == 1 && drained == 1);
    CHECK(lambo_reset_generation() == 1);
    CHECK(memcmp(expected, rdram, 8 * 1024 * 1024) == 0);
    MEM_H(0, state) = 8;
    CHECK(lambo_reset_tick(rdram, &ctx) == 0);
    CHECK(MEM_H(0, state) == 8 && stopped == 1);
    // Another reset works in the same session after the first was consumed.
    lambo_reset_request();
    CHECK(lambo_reset_tick(rdram, &ctx) == 0);
    CHECK(MEM_H(0, state) == 3 && stopped == 2 && drained == 2);
    CHECK(lambo_reset_generation() == 2);
    free(expected);
    free(rdram);
    if (!failures) puts("In-window reset safety checks passed");
    return failures ? 1 : 0;
}
