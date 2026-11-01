/* Original mod code. Guest addresses are USA-only; see docs/jp-weather.md.
 * Runs on the guest game thread at the race loader's final audio call, after
 * selectors, championship round and fog data have been initialized. Code mods
 * require restart when toggled, so no running-game unload callback is needed.
 */
#include "weather_state.h"

#ifndef JP_WEATHER_TEST
__attribute__((noinline, weak, used, section(".recomp_import.*")))
unsigned recomp_get_config_u32(const char *key) { return 0; }
__attribute__((noinline, weak, used, section(".recomp_import.*")))
int lambo_log_v1(const char *message) { return 0; }
__attribute__((noinline, weak, used, section(".recomp_import.*")))
unsigned lambo_register_ai_maneuver_cap_v1(const unsigned *cap) { return 0; }

#define H(address) (*(volatile short *)(address))
#define B(address) (*(volatile unsigned char *)(address))
#define F(address) (*(volatile float *)(address))
#endif

static const char *const round_keys[JP_WEATHER_MAX_ROUNDS] = {
    "weather.round1", "weather.round2", "weather.round3", "weather.round4",
    "weather.round5", "weather.round6", "weather.round7", "weather.round8",
    "weather.round9", "weather.round10", "weather.round11", "weather.round12"
};
static int changed_ai;
static float saved_speed[2];
static short saved_countdown[2];
/* The base retains only this address, never a host-side copy of the cap.
 * Developer savestates currently omit the mod heap; they are not supported
 * for restoring weather across differently configured races.
 */
static unsigned maneuver_cap = 100;

/* Table writes retain the native AI routine, including port relocation fixes.
 * The original values are captured once, then restored when US default is
 * selected for a later race. No writes occur with the default from startup.
 */
static void apply_ai(enum jp_weather_value weather) {
    unsigned i;
    if (weather == JP_WEATHER_US_DEFAULT) {
        if (changed_ai) {
            for (i = 0; i < 2; ++i) {
                F(0x80089444u + i * 4) = saved_speed[i];
                H(0x80089408u + i * 2) = saved_countdown[i];
            }
            changed_ai = 0;
        }
        return;
    }
    if (!changed_ai) {
        for (i = 0; i < 2; ++i) {
            saved_speed[i] = F(0x80089444u + i * 4);
            saved_countdown[i] = H(0x80089408u + i * 2);
        }
        changed_ai = 1;
    }
    F(0x80089444u) = weather == JP_WEATHER_WET ? 220.0f : 290.0f;
    F(0x80089448u) = weather == JP_WEATHER_WET ? 180.0f : 240.0f;
    /* Apply the JP weather operation to the USA difficulty baseline. JP has
     * four difficulty tiers; transplanting its Easy/Hard rows would silently
     * change USA difficulty, which belongs to a separate mod.
     */
    for (i = 0; i < 2; ++i)
        H(0x80089408u + i * 2) = weather == JP_WEATHER_WET
            ? saved_countdown[i] >> 1 : saved_countdown[i];
}

#ifndef JP_WEATHER_TEST
__attribute__((retain, section(".recomp_hook.func_80066F84")))
#endif
void jp_weather_race_start(unsigned a0, unsigned a1, unsigned a2, unsigned a3) {
    unsigned slots[JP_WEATHER_SLOT_COUNT];
    unsigned mode, i;
    int verified;
    enum jp_weather_value weather;
    /* Generated base calls do not preserve the ROM JAL return address in r31.
     * Match the loader phase and argument tuple instead of consulting r31.
     */
    if (H(0x800CE6ACu) != 7 || a0 != 1 || a1 != 0x3F000000u ||
        a2 != 0x4002 || a3 != 0x40)
        return;
    mode = recomp_get_config_u32("weather.mode");
    slots[0] = mode - 1;
    for (i = 0; i < JP_WEATHER_MAX_ROUNDS; ++i)
        slots[i + 1] = recomp_get_config_u32(round_keys[i]);
    weather = jp_weather_resolve(mode >= 1 && mode <= 3,
        H(0x800CE6B4u), H(0x800CE6A4u), H(0x800CE6BAu), slots);
    maneuver_cap = weather == JP_WEATHER_WET ? 60 : 100;
    if (!lambo_register_ai_maneuver_cap_v1(&maneuver_cap)) {
        lambo_log_v1("[jp-weather] bridge registration failed");
        return;
    }
    apply_ai(weather);
    if (weather == JP_WEATHER_US_DEFAULT) {
        lambo_log_v1("[jp-weather] US default");
        return;
    }
    /* Dry fog was just populated by the stock loader. Wet/night override its
     * two halfword parameters and three color bytes, not the track records.
     */
    if (weather != JP_WEATHER_DRY) {
        H(0x800A39C0u) = weather == JP_WEATHER_WET ? 930 : 850;
        H(0x800A39C2u) = 999;
        B(0x800A39C4u) = weather == JP_WEATHER_WET ? 64 : 7;
        B(0x800A39C5u) = weather == JP_WEATHER_WET ? 64 : 7;
        B(0x800A39C6u) = weather == JP_WEATHER_WET ? 70 : 11;
    }
    /* Readback diagnoses package execution/data writes, not AI trajectories or
     * rendered output. US default deliberately does not read these locations.
     */
    verified = F(0x80089444u) == (weather == JP_WEATHER_WET ? 220.0f : 290.0f)
        && F(0x80089448u) == (weather == JP_WEATHER_WET ? 180.0f : 240.0f);
    for (i = 0; i < 2; ++i)
        verified = verified && H(0x80089408u + i * 2) ==
            (weather == JP_WEATHER_WET ? saved_countdown[i] >> 1 : saved_countdown[i]);
    if (weather != JP_WEATHER_DRY)
        verified = verified && H(0x800A39C0u) == (weather == JP_WEATHER_WET ? 930 : 850)
            && H(0x800A39C2u) == 999
            && B(0x800A39C4u) == (weather == JP_WEATHER_WET ? 64 : 7)
            && B(0x800A39C5u) == (weather == JP_WEATHER_WET ? 64 : 7)
            && B(0x800A39C6u) == (weather == JP_WEATHER_WET ? 70 : 11);
    lambo_log_v1(verified ? "[jp-weather] data verified" : "[jp-weather] data mismatch");
    lambo_log_v1(weather == JP_WEATHER_WET ? "[jp-weather] wet" :
                 weather == JP_WEATHER_NIGHT ? "[jp-weather] night" :
                 "[jp-weather] dry");
}
