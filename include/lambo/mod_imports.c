/* Compile once into a MIPS mod. RecompModTool binds these weak import stubs
 * to the port exports. Host-native modules use the runtime guest-context ABI. */
#include "mod_api.h"
#define IMPORT __attribute__((noinline, weak, used, section(".recomp_import.*")))
IMPORT uint32_t lambo_mod_api_version(void) { return 0; }
IMPORT int32_t lambo_get_game_value_v1(uint32_t key) { return 0; }
IMPORT uint32_t lambo_get_racer_v1(uint32_t entrant) { return 0; }
IMPORT uint32_t lambo_get_racer_body_v1(uint32_t racer) { return 0; }
IMPORT uint32_t lambo_register_racer_extension_v1(uint32_t bytes) { return 0; }
IMPORT void* lambo_get_racer_extension_v1(uint32_t racer, uint32_t extension) { return 0; }
IMPORT uint32_t lambo_store_create_v1(uint32_t kind) { return 0; }
IMPORT int32_t lambo_store_put_v1(uint32_t store, uint32_t key, uint32_t value) { return 0; }
IMPORT uint32_t lambo_store_insert_v1(uint32_t store, uint32_t value) { return 0; }
IMPORT int32_t lambo_store_get_v1(uint32_t store, uint32_t key, uint32_t* value) { return 0; }
IMPORT int32_t lambo_store_erase_v1(uint32_t store, uint32_t key) { return 0; }
IMPORT int32_t lambo_store_destroy_v1(uint32_t store) { return 0; }
