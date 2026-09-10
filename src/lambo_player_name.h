#ifndef LAMBO_PLAYER_NAME_H
#define LAMBO_PLAYER_NAME_H

#include <stdint.h>

#ifdef __cplusplus
#include <string>

namespace lambo {
namespace player {

// Player one's persisted identity (player.json "name", shared with the ROM's
// Championship name editor which auto-saves on confirm). Uppercase A-Z plus
// space, 1-12 characters; lowercase input is uppercased. Used by the Driver
// name options page and the launcher so the name is visible outside the game.
std::string saved_name();
bool set_saved_name(const std::string& name);
void clear_saved_name();

} // namespace player
} // namespace lambo

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
