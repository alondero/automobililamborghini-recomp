#ifndef LAMBO_MOD_API_H
#define LAMBO_MOD_API_H

/* USA guest ABI v1. All functions run synchronously on the game thread.
 * See docs/modding.md for timing, ownership, limits and failure behavior.
 * These are MIPS declarations, not host-native RDRAM views. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum LamboGameValue {
    LAMBO_GAME_STATE = 0,
    LAMBO_RACE_MODE = 1,
    LAMBO_PLAYER_COUNT = 2,
    LAMBO_FIELD_COUNT = 3,
    LAMBO_RACE_DIFFICULTY = 4,
    LAMBO_MENU_TRACK = 5
};
enum LamboStoreKind { LAMBO_MAP = 0, LAMBO_SET = 1, LAMBO_SLOTS = 2 };

uint32_t lambo_mod_api_version(void);
int32_t lambo_get_game_value_v1(uint32_t key);
uint32_t lambo_get_racer_v1(uint32_t entrant); /* one-based entrant -> handle */
uint32_t lambo_get_racer_body_v1(uint32_t racer); /* handle -> guest address */
uint32_t lambo_register_racer_extension_v1(uint32_t bytes);
void* lambo_get_racer_extension_v1(uint32_t racer, uint32_t extension);

uint32_t lambo_store_create_v1(uint32_t kind);
int32_t lambo_store_put_v1(uint32_t store, uint32_t key, uint32_t value);
uint32_t lambo_store_insert_v1(uint32_t store, uint32_t value);
int32_t lambo_store_get_v1(uint32_t store, uint32_t key, uint32_t* value);
int32_t lambo_store_erase_v1(uint32_t store, uint32_t key);
int32_t lambo_store_destroy_v1(uint32_t store);

/* Named, read-only USA data references, described by lamborghini.datasyms.toml.
 * Values are guest halfwords; only read them on the game thread. Prefer the
 * getters above for code that should survive a future layout revision. */
extern const volatile int16_t lambo_game_state;
extern const volatile int16_t lambo_race_mode;
extern const volatile int16_t lambo_player_count;
extern const volatile int16_t lambo_field_count;
extern const volatile int16_t lambo_race_difficulty;
extern const volatile int16_t lambo_menu_track;

/* Place callback definitions in .recomp_callback.*:<event>, e.g.
 * __attribute__((section(".recomp_callback.*:recomp_on_init"), used))
 * void on_init(void) { ... }
 * Events and their arguments are documented in docs/modding.md. */

#ifdef __cplusplus
}
#endif
#endif
