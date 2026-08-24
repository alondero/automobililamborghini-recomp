// #84: draw the sky panorama in 3P/4P split screen. The frame dispatcher
// func_800030F8 calls the per-viewport sky emitter (vram 0x8000F6D8) only when
// the player count at 0x800CE6A4 is < 3 (`slti $at, players, 3` / `beq $at, $zero`
// at 0x80004E90/0x80004E94). A [[patches.hook]] before the beq routes $at through
// here: returning 1 takes the branch the way 1P/2P take it, so the game's own
// emitter runs -- no sky geometry, matrices or textures are synthesised here.

#include <cstdint>

#include "lambo_camera_projection.h"
#include "lambo_config.h"
#include "recomp.h"
#include "rt64_extended_gbi.h"

#define LAMBO_DL_CURSOR 0x800A39CCu

namespace {

bool s_backdrop_group_open;

void emit_cmd(uint8_t* rdram, uint32_t w0, uint32_t w1) {
    const gpr cursor_address = (gpr)(int32_t)LAMBO_DL_CURSOR;
    const gpr cursor = MEM_W(0, cursor_address);
    MEM_W(0, cursor) = (int32_t)w0;
    MEM_W(4, cursor) = (int32_t)w1;
    MEM_W(0, cursor_address) = (int32_t)(cursor + 8);
}

void emit_group_commands(uint8_t* rdram, const GfxCommand* commands, int count) {
    for (int i = 0; i < count; ++i) {
        emit_cmd(rdram, commands[i].values.word0, commands[i].values.word1);
    }
}

} // namespace

extern "C" uint32_t lambo_sky_match_1p_guard(uint8_t* rdram, uint32_t at) {
    (void)rdram;
    return lambo::config::widescreen_sky_match() ? 1u : at;
}

// Bracket the game's own finite panorama with a semantic RT64 transform-group
// marker. This is deliberately explicit: the old renderer heuristic inferred a
// backdrop from a zero-translation view matrix, which could not distinguish the
// sky from every other rotation-only camera and had no knowledge of camera FOV.
extern "C" void lambo_sky_backdrop_begin(uint8_t* rdram) {
    if (s_backdrop_group_open) {
        return;
    }

    GfxCommand enable{};
    gEXEnable(&enable);
    emit_group_commands(rdram, &enable, 1);

    GfxCommand group[2]{};
    gEXMatrixGroup(group, G_EX_ID_AUTO, G_EX_INTERPOLATE_DECOMPOSE, G_EX_PUSH, 1,
                   G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,
                   G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_SKIP,
                   G_EX_COMPONENT_AUTO, G_EX_ORDER_AUTO, G_EX_EDIT_NONE,
                   G_EX_ASPECT_BACKDROP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
    gEXSetMatrixGroupProjectionFovScale(
        group, lambo_camera_backdrop_projection_scale_bits());
    emit_group_commands(rdram, group, 2);
    s_backdrop_group_open = true;
}

extern "C" void lambo_sky_backdrop_end(uint8_t* rdram) {
    if (!s_backdrop_group_open) {
        return; // first call's post-jal hook is also a branch merge label
    }

    GfxCommand pop{};
    gEXPopMatrixGroup(&pop, 1);
    emit_group_commands(rdram, &pop, 1);
    s_backdrop_group_open = false;
}
