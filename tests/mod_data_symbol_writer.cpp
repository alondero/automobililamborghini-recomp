#include <cstdint>
// A separate translation unit models the game's ownership of a live data
// symbol. The guest header must not let optimized readers assume immutability.
extern "C" {
int16_t lambo_game_state = 0;
void lambo_test_change_state() { lambo_game_state = 8; }
}
