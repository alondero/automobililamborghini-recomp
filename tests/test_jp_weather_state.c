#include <assert.h>
#include <limits.h>
#include "../mods/jp_weather/weather_state.h"

int main(void) {
    unsigned slots[JP_WEATHER_SLOT_COUNT] = {0};
    unsigned weather;
    int players, mode, round;

    /* Defaults remain dry for every native mode and every custom round. */
    for (mode = 0; mode <= 4; ++mode)
        for (round = 1; round <= JP_WEATHER_MAX_ROUNDS; ++round)
            assert(jp_weather_resolve(1, mode, 1, round, slots) == JP_WEATHER_DRY);

    for (weather = JP_WEATHER_DRY; weather <= JP_WEATHER_NIGHT; ++weather) {
        slots[0] = weather;
        for (players = 1; players <= 4; ++players) {
            /* Time trials copy slot zero without the single-race player gate. */
            assert(jp_weather_resolve(1, 0, players, 0, slots) == (int)weather);
            assert(jp_weather_resolve(1, 2, players, 0, slots) ==
                   (players < 3 ? (int)weather : JP_WEATHER_DRY));
            assert(jp_weather_resolve(1, 1, players, 0, slots) == JP_WEATHER_DRY);
            assert(jp_weather_resolve(1, 4, players, 0, slots) == JP_WEATHER_DRY);
            for (mode = 0; mode <= 4; ++mode)
                assert(jp_weather_resolve(0, mode, players, 1, slots) == JP_WEATHER_US_DEFAULT);
        }
    }

    /* A round uses its own entry regardless of slot zero or player count. The
     * resolver receives the already-advanced native counter, never increments.
     */
    for (round = 1; round <= JP_WEATHER_MAX_ROUNDS; ++round) {
        slots[round] = (unsigned)(round % 3);
        for (players = 1; players <= 4; ++players)
            assert(jp_weather_resolve(1, 3, players, round, slots) == round % 3);
    }
    slots[1] = JP_WEATHER_WET;
    slots[2] = JP_WEATHER_NIGHT;
    assert(jp_weather_resolve(1, 3, 1, 1, slots) == JP_WEATHER_WET);
    assert(jp_weather_resolve(1, 3, 1, 2, slots) == JP_WEATHER_NIGHT);

    /* Malformed states must not index outside the caller's slot array. */
    assert(jp_weather_resolve(1, 3, 1, -1, slots) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, 3, 1, 0, slots) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, 3, 1, 13, slots) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, 3, 1, INT_MAX, slots) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, -1, 1, 1, slots) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, 5, 1, 1, slots) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, 2, 0, 0, slots) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, 2, 5, 0, slots) == JP_WEATHER_US_DEFAULT);
    slots[0] = UINT_MAX;
    assert(jp_weather_resolve(1, 2, 1, 0, slots) == JP_WEATHER_US_DEFAULT);
    slots[12] = 3;
    assert(jp_weather_resolve(1, 3, 1, 12, slots) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, 0, 1, 0, 0) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, 2, 1, 0, 0) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, 3, 1, 1, 0) == JP_WEATHER_US_DEFAULT);
    assert(jp_weather_resolve(1, 1, 1, 0, 0) == JP_WEATHER_DRY);
    assert(jp_weather_resolve(1, 2, 3, 0, 0) == JP_WEATHER_DRY);
    assert(jp_weather_resolve(0, 3, 1, INT_MAX, 0) == JP_WEATHER_US_DEFAULT);
    return 0;
}
