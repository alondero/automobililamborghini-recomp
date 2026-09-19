// Draw the sky panorama in 3P/4P split screen. The frame dispatcher
// func_800030F8 calls the per-viewport sky emitter (vram 0x8000F6D8) only when
// the player count at 0x800CE6A4 is < 3 (`slti $at, players, 3` / `beq $at, $zero`
// at 0x80004E90/0x80004E94). A [[patches.hook]] before the beq routes $at through
// here: returning 1 takes the branch the way 1P/2P take it, so the game's own
// emitter runs. The native replacement maps its panorama tiles to bearings.

#include <cstdint>
#include <cstring>
#include <map>

#include "lambo_camera_projection.h"
#include "lambo_config.h"
#include "lambo_sky_panorama.h"
#include "librecomp/addresses.hpp"
#include "recomp.h"
#include "rt64_extended_gbi.h"

#define LAMBO_DL_CURSOR 0x800A39CCu

// USA frame builder 0x80000FDC..0x80001028: two 0x7A50-byte task slots,
// task pointer at slot + 0x68, display list at task + 0x1C0. The list ends
// before the scheduler fields at slot + 0x7A28 (initialised at 0x80000840).
static constexpr uint32_t kFirstTask = 0x800BF1D8u + 0x68u;
static constexpr uint32_t kTaskStride = 0x7A50u;

namespace {

bool s_backdrop_group_open;
uint32_t s_panorama_start;

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

extern "C" void lambo_sky_panorama_start(uint8_t* rdram) {
    s_panorama_start = s_backdrop_group_open
        ? static_cast<uint32_t>(MEM_W(0, static_cast<gpr>(static_cast<int32_t>(LAMBO_DL_CURSOR)))) : 0;
}

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

    const uint32_t task = word(0x800A2BFCu);
    const uint32_t cursor_before = word(LAMBO_DL_CURSOR);
    // Reserve our three commands plus both original restoration commands.
    // Subtract from the verified end instead of adding to an untrusted cursor.
    if ((task != kFirstTask && task != kFirstTask + kTaskStride) ||
        (cursor_before & 7u) != 0 || cursor_before < task + 0x1C0u ||
        cursor_before > task + 0x79C0u - 40u ||
        (s_panorama_start & 7u) != 0 || s_panorama_start < task + 0x1C0u ||
        s_panorama_start > cursor_before) {
        return;
    }

    // Validate every texture before replacing any stock draws. A partial list
    // cannot provide continuous coverage.
    for (int tile = 0; tile < 16; ++tile) {
        const uint32_t image = word(textures + 0x254 + tile * 4);
        if (image < 0x80000000u || image > 0x80800000u - 4096u) return;
    }
    // USA emitter 0x8000F77C..F83C stores a float panorama coordinate modulo
    // 128 here; guTranslate consumes its negative at F8F8. Game-thread only.
    const int32_t phase_bits = word(0x80098E58u);
    float phase;
    std::memcpy(&phase, &phase_bits, sizeof(phase));
    if (!std::isfinite(phase) || phase < 0.0f || phase >= 128.0f) return;

    // Less than 180 degrees is visible: at most five tiles, subdivided so
    // affine N64 UV interpolation approximates the angular mapping closely.
    constexpr int max_quads = 4 * 5 * kSkySlicesPerTile;
    constexpr uint32_t buffer_size = max_quads * (64 + 9 * 8) + 8;
    static std::map<std::pair<uint32_t, int>, uint32_t> buffers;
    auto& buffer = buffers[{task, player}];
    if (buffer == 0) {
        auto* allocation = static_cast<uint8_t*>(recomp::alloc(rdram, buffer_size));
        if (allocation == nullptr) {
            return; // Preserve the original sky if the extension cannot be allocated.
        }
        buffer = 0x80000000u + static_cast<uint32_t>(allocation - rdram);
    }
    // USA emitter passes this float to guTranslate at 0x8000F908.
    const int32_t vertical_bits = word(0x800A2F90u);
    float vertical_offset;
    std::memcpy(&vertical_offset, &vertical_bits, sizeof(vertical_offset));
    const int radius = lambo_sky_column_radius(lambo_camera_sky_vertical_fov(),
        target_aspect, half(0x800CE6A4u) == 2, vertical_offset);
    uint32_t vertex = buffer;
    const uint32_t list = buffer + max_quads * 64;
    uint32_t cursor = list;
    const auto command = [&](uint32_t w0, uint32_t w1) {
        word(cursor) = static_cast<int32_t>(w0);
        word(cursor + 4) = static_cast<int32_t>(w1);
        cursor += 8;
    };
    // Keep the existing coverage envelope independent of angular motion.
    const double left = lambo_sky_panorama_at_x(-radius * 128.0, phase);
    const double right = lambo_sky_panorama_at_x((radius + 1) * 128.0, phase);
    int quads = 0;
    for (int band = 0; band < 4; ++band) {
        const bool cap = band >= 2;
        const int row = band % 2;
        const uint16_t mirrored = static_cast<uint16_t>(half(0x80089054u + track * 4 + row * 2));
        for (int slice = static_cast<int>(std::floor(left / kSkySliceWidth));
             slice < static_cast<int>(std::ceil(right / kSkySliceWidth)); ++slice) {
            // Do not commit a partial replacement if the coverage math ever
            // exceeds the allocation. The guest cursor is rewound only after
            // this loop, so returning here leaves the stock sky list intact.
            if (++quads > max_quads) {
                return;
            }
            const int column = static_cast<int>(std::floor(slice / double(kSkySlicesPerTile)));
            const double u0 = std::max(left, slice * kSkySliceWidth);
            const double u1 = std::min(right, (slice + 1) * kSkySliceWidth);
            const int tile = lambo_sky_tile_index(center, column);
            // The two ROM vertex banks differ only in horizontal texture
            // orientation. Copy the middle quad, including colour and UVs.
            const uint32_t source = ((mirrored & (1u << tile)) ? 0x8011F5D0u : 0x8011F450u)
                + row * 192 + 64;
            for (int v = 0; v < 4; ++v) {
                // Caps duplicate only the top/bottom edge's UVs, so texture
                // detail stays at its original scale and edge pixels continue
                // beyond the artwork without wrapping another mountain/cloud.
                const int edge_vertex = row == 0 ? ((v == 0 || v == 3) ? 0 : 1)
                                                 : ((v == 0 || v == 3) ? 3 : 2);
                const int source_vertex = cap ? edge_vertex : v;
                for (int offset = 0; offset < 16; offset += 4) {
                    word(vertex + v * 16 + offset) = word(source + source_vertex * 16 + offset);
                }
                const double u = (v == 0 || v == 3) ? u1 : u0;
                half(vertex + v * 16) = static_cast<int16_t>(std::lround(lambo_sky_project_x(u, phase)));
                const double fraction = (u - column * 128.0) / 128.0;
                const int source_left = (source_vertex < 2) ? 1 : 2;
                const int source_right = (source_vertex < 2) ? 0 : 3;
                const double uv_left = half(source + source_left * 16 + 8);
                const double uv_right = half(source + source_right * 16 + 8);
                half(vertex + v * 16 + 8) = static_cast<int16_t>(std::lround(uv_left + fraction * (uv_right - uv_left)));
                if (cap && ((row == 0 && v < 2) || (row == 1 && v >= 2))) {
                    half(vertex + v * 16 + 2) = row == 0 ? kSkyEdgeExtent : -kSkyEdgeExtent;
                }
            }
            // Same RGBA16 tile load and two F3DEX triangles as the original
            // emitter (0x8000FA48..0x8000FCDC), using vertex-cache slots 0..3.
            const uint32_t tile_image = word(textures + 0x254 + row * 32 + tile * 4);
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

    word(LAMBO_DL_CURSOR) = static_cast<int32_t>(s_panorama_start);

    // The recomp heap is beyond the N64's 8 MiB. Bracket the sublist's absolute
    // KSEG0 pointers with RT64's extended-address mode; normal guest DLs retain
    // their original segmented addressing after the call.
    GfxCommand extended{};
    gEXSetRDRAMExtended(&extended, 1);
    emit_group_commands(rdram, &extended, 1);
    // F3D G_DL preserves bit 31, which RT64 needs for extended addresses.
    // gEXDisplayList stores only 28 address bits and would lose that marker.
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
    s_panorama_start = 0;
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
