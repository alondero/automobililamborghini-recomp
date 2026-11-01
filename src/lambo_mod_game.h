#ifndef LAMBO_MOD_GAME_H
#define LAMBO_MOD_GAME_H

#include <stdint.h>
#include "recomp.h"

#ifdef __cplusplus
namespace lambo::mods {
void register_game_api();
void register_storage_api();
void reset_storage_api();
}
extern "C" {
#endif
void lambo_mod_boot(uint8_t* rdram, recomp_context* ctx);
void lambo_mod_frame(uint8_t* rdram, recomp_context* ctx);
void lambo_mod_scene_reset(uint8_t* rdram, recomp_context* ctx);
/* SI bridge only publishes flags; callbacks are deferred to the game thread. */
void lambo_mod_pak_loaded(void);
void lambo_mod_pak_written(void);
/* Developer RAM restore invalidates host-side race handles/extensions. */
void lambo_mod_state_restored(void);
#ifdef __cplusplus
}
#endif
#endif
