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

// Restore before the ROM copies a newly earned record. The record routines use
// zero-based indices; only player zero owns the native persisted identity.
void lambo_player_name_restore_for_record(uint8_t* rdram, int player_index);

#ifdef __cplusplus
}
#endif

#endif
