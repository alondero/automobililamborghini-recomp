#ifndef LAMBO_PLAYER_NAME_H
#define LAMBO_PLAYER_NAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hooks at the ROM's name-screen setup and Done handler. Both operate only on
// player one; multiplayer guests keep the game's independent DRIVER 2-4 names.
void lambo_player_name_seed(uint8_t* rdram);
void lambo_player_name_save(uint8_t* rdram);

#ifdef __cplusplus
}
#endif

#endif
