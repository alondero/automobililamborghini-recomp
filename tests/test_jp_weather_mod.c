/* Run the actual package hook against sparse, typed guest locations. An
 * unexpected address or width aborts rather than landing in anonymous RAM.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct half_cell { unsigned address; short value; };
struct byte_cell { unsigned address; unsigned char value; };
struct float_cell { unsigned address; float value; };
static struct half_cell halves[] = {
    {0x800CE6ACu, 7}, {0x800CE6B4u, 2}, {0x800CE6A4u, 1}, {0x800CE6BAu, 0},
    {0x80089408u, 40}, {0x8008940Au, 25},
    {0x800A39C0u, 995}, {0x800A39C2u, 1000}
};
static struct byte_cell bytes[] = {
    {0x800A39C4u, 57}, {0x800A39C5u, 48}, {0x800A39C6u, 55}
};
static struct float_cell floats[] = {{0x80089444u, 290}, {0x80089448u, 240}};
static unsigned table_accesses, fog_accesses, config_reads, log_count;
static unsigned verified_logs, mismatch_logs;
static unsigned inject_readback_fault, fault_accesses;
static float invalid_readback;
static unsigned config_mode, config_rounds[12];
static char last_log[64];

static short *half_at(unsigned address) {
    unsigned i;
    if (address == 0x80089408u || address == 0x8008940Au) ++table_accesses;
    if (address == 0x800A39C0u || address == 0x800A39C2u) ++fog_accesses;
    for (i = 0; i < sizeof(halves) / sizeof(halves[0]); ++i)
        if (halves[i].address == address) return &halves[i].value;
    fprintf(stderr, "Unexpected halfword address %08x\n", address);
    assert(0);
    return 0;
}

static unsigned char *byte_at(unsigned address) {
    unsigned i;
    ++fog_accesses;
    for (i = 0; i < sizeof(bytes) / sizeof(bytes[0]); ++i)
        if (bytes[i].address == address) return &bytes[i].value;
    fprintf(stderr, "Unexpected byte address %08x\n", address);
    assert(0);
    return 0;
}

static float *float_at(unsigned address) {
    unsigned i;
    ++table_accesses;
    /* Capture, write, then readback: inject a bad observation only at the last
     * step, so the diagnostic cannot pass merely because it ran the writes.
     */
    if (inject_readback_fault && address == 0x80089444u && ++fault_accesses == 3)
        return &invalid_readback;
    for (i = 0; i < sizeof(floats) / sizeof(floats[0]); ++i)
        if (floats[i].address == address) return &floats[i].value;
    fprintf(stderr, "Unexpected float address %08x\n", address);
    assert(0);
    return 0;
}

unsigned recomp_get_config_u32(const char *key) {
    unsigned i;
    char round_key[32];
    ++config_reads;
    if (strcmp(key, "weather.mode") == 0) return config_mode;
    for (i = 0; i < 12; ++i) {
        snprintf(round_key, sizeof(round_key), "weather.round%u", i + 1);
        if (strcmp(key, round_key) == 0) return config_rounds[i];
    }
    assert(!"Unexpected configuration key");
    return 0;
}

int lambo_log_v1(const char *message) {
    ++log_count;
    if (strcmp(message, "[jp-weather] data verified") == 0) ++verified_logs;
    if (strcmp(message, "[jp-weather] data mismatch") == 0) ++mismatch_logs;
    snprintf(last_log, sizeof(last_log), "%s", message);
    return (int)strlen(message);
}

#define JP_WEATHER_TEST
#define H(address) (*half_at(address))
#define B(address) (*byte_at(address))
#define F(address) (*float_at(address))
#include "../mods/jp_weather/weather.c"

static void start_race(void) {
    jp_weather_race_start(1, 0x3F000000u, 0x4002, 0x40);
}

static void stock_fog(void) {
    halves[6].value = 995;
    halves[7].value = 1000;
    bytes[0].value = 57;
    bytes[1].value = 48;
    bytes[2].value = 55;
    fog_accesses = 0;
}

static void expect_ai(float speed0, float speed1, short countdown0, short countdown1) {
    assert(floats[0].value == speed0 && floats[1].value == speed1);
    assert(halves[4].value == countdown0 && halves[5].value == countdown1);
}

static void expect_fog(short near, short far, unsigned r, unsigned g, unsigned b) {
    assert(halves[6].value == near && halves[7].value == far);
    assert(bytes[0].value == r && bytes[1].value == g && bytes[2].value == b);
}

int main(void) {
    unsigned argument;

    /* Default from process startup never even accesses the patched tables or
     * fog. It cannot silently rewrite the baseline with the same values.
     */
    start_race();
    assert(table_accesses == 0 && fog_accesses == 0);
    expect_ai(290, 240, 40, 25);
    expect_fog(995, 1000, 57, 48, 55);
    assert(strcmp(last_log, "[jp-weather] US default") == 0);
    assert(verified_logs == 0 && mismatch_logs == 0);

    config_mode = 2;
    for (argument = 0; argument < 4; ++argument) {
        unsigned args[] = {1, 0x3F000000u, 0x4002, 0x40};
        args[argument] ^= 1;
        config_reads = log_count = table_accesses = fog_accesses = 0;
        jp_weather_race_start(args[0], args[1], args[2], args[3]);
        assert(config_reads == 0 && log_count == 0 && table_accesses == 0 && fog_accesses == 0);
    }
    halves[0].value = 8;
    start_race();
    assert(config_reads == 0 && log_count == 0 && table_accesses == 0 && fog_accesses == 0);
    halves[0].value = 7;

    start_race();
    expect_ai(220, 180, 20, 12);
    expect_fog(930, 999, 64, 64, 70);
    assert(strcmp(last_log, "[jp-weather] wet") == 0);
    assert(verified_logs == 1 && mismatch_logs == 0);
    /* A second wet race halves the captured baseline, not the previous result. */
    start_race();
    expect_ai(220, 180, 20, 12);

    config_mode = 1;
    stock_fog();
    start_race();
    expect_ai(290, 240, 40, 25);
    expect_fog(995, 1000, 57, 48, 55);
    assert(fog_accesses == 0);

    config_mode = 3;
    start_race();
    expect_ai(290, 240, 40, 25);
    expect_fog(850, 999, 7, 7, 11);

    config_mode = 0;
    stock_fog();
    start_race();
    expect_ai(290, 240, 40, 25);
    assert(fog_accesses == 0);
    table_accesses = 0;
    start_race();
    assert(table_accesses == 0);

    /* Changed baseline proves that countdown halving and restoration use the
     * captured USA data, rather than transplanted Japanese difficulty tiers.
     */
    floats[0].value = 301;
    floats[1].value = 251;
    halves[4].value = 80;
    halves[5].value = 31;
    config_mode = 2;
    start_race();
    expect_ai(220, 180, 40, 15);
    config_mode = 1;
    stock_fog();
    start_race();
    expect_ai(290, 240, 80, 31);
    config_mode = 0;
    start_race();
    expect_ai(301, 251, 80, 31);
    floats[0].value = 290;
    floats[1].value = 240;
    halves[4].value = 40;
    halves[5].value = 25;

    /* Only single-race mode applies the three-player weather restriction. */
    config_mode = 2;
    halves[2].value = 3;
    stock_fog();
    start_race();
    expect_ai(290, 240, 40, 25);
    expect_fog(995, 1000, 57, 48, 55);
    assert(fog_accesses == 0);

    /* A championship uses its current slot, including the final custom slot,
     * independently of the single-race selection and without incrementing.
     */
    halves[1].value = 3;
    halves[3].value = 1;
    config_rounds[0] = JP_WEATHER_NIGHT;
    start_race();
    expect_fog(850, 999, 7, 7, 11);
    assert(halves[3].value == 1);
    halves[3].value = 12;
    config_rounds[11] = JP_WEATHER_WET;
    start_race();
    expect_ai(220, 180, 20, 12);
    expect_fog(930, 999, 64, 64, 70);
    assert(halves[3].value == 12);
    config_mode = 0;
    stock_fog();
    start_race();
    expect_ai(290, 240, 40, 25);
    assert(fog_accesses == 0);
    assert(verified_logs == 9 && mismatch_logs == 0);
    config_mode = 2;
    inject_readback_fault = 1;
    start_race();
    assert(verified_logs == 9 && mismatch_logs == 1);
    inject_readback_fault = 0;
    config_mode = 0;
    start_race();
    expect_ai(290, 240, 40, 25);
    return 0;
}
