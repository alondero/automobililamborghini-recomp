// Draw the sky panorama in 3P/4P split screen. The frame dispatcher
// func_800030F8 calls the per-viewport sky emitter (vram 0x8000F6D8) only when
// the player count at 0x800CE6A4 is < 3 (`slti $at, players, 3` / `beq $at, $zero`
// at 0x80004E90/0x80004E94). A [[patches.hook]] before the beq routes $at through
// here: returning 1 takes the branch the way 1P/2P take it, so the game's own
// emitter runs. Additional columns continue its panorama at the authored scale.

#include <cstdint>
#include <map>

#include "lambo_camera_projection.h"
#include "lambo_config.h"
#include "lambo_sky_panorama.h"
#include "librecomp/addresses.hpp"
#include "recomp.h"
#include "rt64_extended_gbi.h"

#define LAMBO_DL_CURSOR 0x800A39CCu

// The game's task arena is roughly 31 KiB. The hook emits three commands
// (24 bytes) plus the original instruction's 8 bytes that follow. Cap the
// cursor at the documented arena end so a stale pointer cannot run off the
// end of the buffer or into an unrelated RDRAM region.
static constexpr uint32_t LAMBO_TASK_ARENA_END = 0x800A39CCu + 0x8000u;

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

// Called on the guest game thread at 0x8001025C, after the six original tiles
// and before the emitter restores texture perspective. All addresses below are
// USA guest addresses, accessed through the runtime's word-swapped MEM macros.
// Reuse storage by the game's graphics-task buffer (0x800A2BFC, u32 pointer)
// and viewport (0x800CE6A6, s16). Its task-buffer reuse fence also protects this
// sublist until RT64 has consumed it. Keeping data outside the original 31 KiB
// task arena avoids stealing space from track geometry, especially in 4P.
// Layout/evidence and the DP-completion fence are recorded in docs/sky-panorama.md.
extern "C" void lambo_sky_extend_panorama(uint8_t* rdram) {
    if (!s_backdrop_group_open) {
        return;
    }
    const float target_aspect = lambo_sky_target_aspect();
    if (!(target_aspect > 0.0f)) {
        return; // The diagnostic renderer has no extended-address sublists.
    }
    const auto word = [&](uint32_t address) -> int32_t& {
        return MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address)));
    };
    const auto half = [&](uint32_t address) -> int16_t& {
        return MEM_H(0, static_cast<gpr>(static_cast<int32_t>(address)));
    };
    const int player = half(0x800CE6A6u);
    const int track = half(0x800CE794u);
    const int center = half(0x800987E0u);
    const uint32_t textures = word(0x80098238u);
    if (player < 0 || player >= 4 || track < 0 || track >= 6 ||
        center < 0 || center >= 8 || textures < 0x80000000u || textures >= 0x80800000u - 0x294u) {
        return;
    }

    // At most 2*506 extra quads, each with 64 vertex bytes and nine commands.
    constexpr uint32_t buffer_size = 2 * 506 * (64 + 9 * 8) + 8;
    static std::map<std::pair<uint32_t, int>, uint32_t> buffers;
    auto& buffer = buffers[{static_cast<uint32_t>(word(0x800A2BFCu)), player}];
    if (buffer == 0) {
        auto* allocation = static_cast<uint8_t*>(recomp::alloc(rdram, buffer_size));
        if (allocation == nullptr) {
            return; // Preserve the original sky if the extension cannot be allocated.
        }
        buffer = 0x80000000u + static_cast<uint32_t>(allocation - rdram);
    }
    const int radius = lambo_sky_column_radius(lambo_camera_sky_vertical_fov(),
        target_aspect, half(0x800CE6A4u) == 2);
    const int quad_count = 2 * (2 * radius - 2);
    // Refuse to write if the game's display-list cursor is at or beyond the
    // documented task arena end. The original cursor advance plus the original
    // instruction's 8 bytes must still fit, so the early cursor cap is 32 bytes
    // before LAMBO_TASK_ARENA_END.
    const uint32_t cursor_before = MEM_W(0, static_cast<gpr>(static_cast<int32_t>(LAMBO_DL_CURSOR)));
    if (cursor_before == 0 || cursor_before + 32u > LAMBO_TASK_ARENA_END) {
        return;
    }
    uint32_t vertex = buffer;
    const uint32_t list = buffer + quad_count * 64;
    uint32_t cursor = list;
    const auto command = [&](uint32_t w0, uint32_t w1) {
        word(cursor) = static_cast<int32_t>(w0);
        word(cursor + 4) = static_cast<int32_t>(w1);
        cursor += 8;
    };
    for (int row = 0; row < 2; ++row) {
        const uint16_t mirrored = static_cast<uint16_t>(half(0x80089054u + track * 4 + row * 2));
        for (int column = -radius; column <= radius; ++column) {
            if (column >= -1 && column <= 1) {
                continue; // Already emitted by the ROM.
            }
            const int tile = lambo_sky_tile_index(center, column);
            // The two ROM vertex banks differ only in horizontal texture
            // orientation. Copy the middle quad, including colour and UVs.
            const uint32_t source = ((mirrored & (1u << tile)) ? 0x8011F5D0u : 0x8011F450u)
                + row * 192 + 64;
            for (int offset = 0; offset < 64; offset += 4) {
                word(vertex + offset) = word(source + offset);
            }
            for (int v = 0; v < 4; ++v) {
                half(vertex + v * 16) = static_cast<int16_t>(half(vertex + v * 16) + column * 128);
            }
            // Same RGBA16 tile load and two F3DEX triangles as the original
            // emitter (0x8000FA48..0x8000FCDC), using vertex-cache slots 0..3.
            // The texture pointer lives in the loaded track data; if a future
            // ROM bump left a 0 there, the SetTextureImage would point at ROM
            // offset 0 and the subsequent LoadTile would fault. Skip the quad
            // and leave the original 3-column sky to cover the viewport.
            const uint32_t tile_image = word(textures + 0x254 + row * 32 + tile * 4);
            if (tile_image < 0x80000000u || tile_image >= 0x80800000u) {
                vertex += 64;
                continue;
            }
            command(0xFD100000u, tile_image);
            command(0xF5100000u, 0x07094060u);
            command(0xE6000000u, 0);
            command(0xF3000000u, 0x077FF080u);
            command(0xE7000000u, 0);
            command(0xF5102000u, 0x00094060u);
            command(0xF2000000u, 0x000FC07Cu);
            command(0x0400103Fu, vertex);
            command(0xB1000204u, 0x00040600u);
            vertex += 64;
        }
    }
    command(0xB8000000u, 0);

    // The recomp heap is beyond the N64's 8 MiB. Bracket the sublist's absolute
    // KSEG0 pointers with RT64's extended-address mode; normal guest DLs retain
    // their original segmented addressing after the call.
    GfxCommand extended{};
    gEXSetRDRAMExtended(&extended, 1);
    emit_group_commands(rdram, &extended, 1);
    emit_cmd(rdram, 0x06000000u, list);
    gEXSetRDRAMExtended(&extended, 0);
    emit_group_commands(rdram, &extended, 1);
}

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
