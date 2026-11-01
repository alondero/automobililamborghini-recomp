#ifndef LAMBO_JP_WEATHER_STATE_H
#define LAMBO_JP_WEATHER_STATE_H

/* Pure guest-independent resolution of the JP race-loader weather choice.
 * Native selectors are big-endian signed halfwords; the package boundary must
 * decode them before calling this function. No memory, hooks, or config API is
 * owned here. See docs/jp-weather.md for the cartridge identity and evidence.
 */
enum jp_weather_value {
    JP_WEATHER_US_DEFAULT = -1,
    JP_WEATHER_DRY = 0,
    JP_WEATHER_WET = 1,
    JP_WEATHER_NIGHT = 2
};

/* Both regions permit at most 12 custom championship rounds. Slot zero belongs
 * to time trials/single races; championship slots are numbered 1 through 12.
 * Zero-initialized selections reproduce the native Dry default. They are race
 * slots, not circuit indices: repeating a circuit may use different weather.
 */
#define JP_WEATHER_MAX_ROUNDS 12
#define JP_WEATHER_SLOT_COUNT (JP_WEATHER_MAX_ROUNDS + 1)

/* Call after the native loader has advanced the championship round counter.
 * JP switch ROM 0x82B78: mode 0 copies slot zero at 0x800072E0; modes 1/4
 * clear active weather at 0x80007170/0x8000751C; mode 2 copies slot zero at
 * 0x80007360 or forces Dry for 3+ players at 0x80007368; mode 3 increments the
 * round at 0x800073D4 and copies that slot at 0x80007474, without a player gate.
 * US mode/round selectors are 0x800CE6B4/0x800CE6BA. Its round-count cap is
 * checked against 12 at 0x8003E4EC (JP at 0x802014C4).
 *
 * Disabled or malformed inputs return US_DEFAULT, meaning the caller must
 * preserve the base behavior. Validation is a package safety rule, not a claim
 * that the cartridge handles invalid selectors this way. A forced-Dry mode
 * does not read selections. This function has no side effects.
 */
static inline enum jp_weather_value jp_weather_resolve(
    int enabled, int race_mode, int players, int loaded_round,
    const unsigned weather_by_slot[JP_WEATHER_SLOT_COUNT]) {
    unsigned slot;
    unsigned weather;
    if (!enabled || race_mode < 0 || race_mode > 4 || players < 1 || players > 4)
        return JP_WEATHER_US_DEFAULT;
    if (race_mode == 1 || race_mode == 4 || (race_mode == 2 && players >= 3))
        return JP_WEATHER_DRY;
    if (race_mode == 3) {
        if (loaded_round < 1 || loaded_round > JP_WEATHER_MAX_ROUNDS)
            return JP_WEATHER_US_DEFAULT;
        slot = (unsigned)loaded_round;
    } else {
        slot = 0;
    }
    if (!weather_by_slot)
        return JP_WEATHER_US_DEFAULT;
    weather = weather_by_slot[slot];
    return weather <= JP_WEATHER_NIGHT ? (enum jp_weather_value)weather
                                       : JP_WEATHER_US_DEFAULT;
}

#endif
