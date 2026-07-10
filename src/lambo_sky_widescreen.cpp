// #84: draw the sky panorama in 3P/4P split screen. The frame dispatcher
// func_800030F8 calls the per-viewport sky emitter (vram 0x8000F6D8) only when
// the player count at 0x800CE6A4 is < 3 (`slti $at, players, 3` / `beq $at, $zero`
// at 0x80004E90/0x80004E94). A [[patches.hook]] before the beq routes $at through
// here: returning 1 takes the branch the way 1P/2P take it, so the game's own
// emitter runs -- no sky geometry, matrices or textures are synthesised here.

#include <cstdint>

#include "lambo_config.h"

extern "C" uint32_t lambo_sky_match_1p_guard(uint8_t* rdram, uint32_t at) {
    (void)rdram;
    return lambo::config::widescreen_sky_match() ? 1u : at;
}
