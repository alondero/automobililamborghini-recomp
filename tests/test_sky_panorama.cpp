#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "recomp.h"
#include "lambo_sky_panorama.h"

extern "C" void lambo_sky_backdrop_begin(uint8_t*);
extern "C" void lambo_sky_backdrop_end(uint8_t*);
extern "C" unsigned int lambo_camera_backdrop_projection_scale_bits() { return 0x3F800000u; }
extern "C" float lambo_camera_sky_vertical_fov() { return 100.0f; }
extern "C" float lambo_sky_target_aspect() { return 32.0f / 9.0f; }
namespace lambo::config { bool widescreen_sky_match() { return true; } }
namespace recomp {
void* alloc(uint8_t* rdram, size_t size) {
    static size_t offset = 0x1000000;
    auto* result = rdram + offset;
    offset += (size + 15) & ~size_t(15);
    if (offset >= 0x2000000) std::abort();
    return result;
}
}

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main() {
    std::vector<uint8_t> memory(32 * 1024 * 1024);
    auto* rdram = memory.data();
    const auto word = [&](uint32_t address) -> int32_t& {
        return MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address)));
    };
    const auto half = [&](uint32_t address) -> int16_t& {
        return MEM_H(0, static_cast<gpr>(static_cast<int32_t>(address)));
    };
    word(0x80098238) = static_cast<int32_t>(0x80200000u);
    half(0x800CE794) = 0;
    half(0x800CE6A4) = 4;
    for (int row = 0; row < 2; ++row) {
        half(0x80089054 + row * 2) = static_cast<int16_t>(row == 0 ? 0x55 : 0xAA);
        for (int tile = 0; tile < 8; ++tile) {
            word(0x80200254 + row * 32 + tile * 4) = static_cast<int32_t>(0x80300000u + row * 0x10000 + tile * 0x1000);
        }
        for (int mirror = 0; mirror < 2; ++mirror) {
            const uint32_t source = (mirror ? 0x8011F5D0u : 0x8011F450u) + row * 192 + 64;
            for (int vertex = 0; vertex < 4; ++vertex) {
                half(source + vertex * 16) = (vertex == 0 || vertex == 3) ? 128 : 0;
                half(source + vertex * 16 + 2) = static_cast<int16_t>(128 - row * 96 - (vertex >= 2 ? 96 : 0));
                half(source + vertex * 16 + 4) = -220;
                half(source + vertex * 16 + 8) = ((vertex == 0 || vertex == 3) != bool(mirror)) ? 2016 : 0;
                half(source + vertex * 16 + 10) = vertex < 2 ? 0 : 1984;
                word(source + vertex * 16 + 12) = -1;
            }
        }
    }
    for (int track = 0; track < 6; ++track) {
        half(0x800CE794) = static_cast<int16_t>(track);
        for (int row = 0; row < 2; ++row) {
            half(0x80089054 + track * 4 + row * 2) =
                static_cast<int16_t>((0x55u << track) ^ (row ? 0xFFu : 0u));
        }
        std::vector<uint32_t> task_lists;
        for (int frame = 0; frame < 2; ++frame) {
            // Exercise the actual neighboring s16 fields: a word read of count
            // would accidentally use viewport index 2 as the two-player selector.
            half(0x800CE6A4) = static_cast<int16_t>(frame == 0 ? 2 : 4);
            word(0x800A2BFC) = static_cast<int32_t>(0x800BF240u + frame * 0x7A50);
            for (int player = 0; player < 4; ++player) {
                half(0x800CE6A6) = static_cast<int16_t>(player);
                uint32_t first_list = 0;
                for (int center = 0; center < 8; ++center) for (float phase : {0.0f, 0.01f, 64.0f, 127.99f}) {
                    std::memcpy(&word(0x80098E58), &phase, sizeof(phase));
                    half(0x800987E0) = static_cast<int16_t>(center);
                    // USA 0x80001008..28: task = slot + 0x68, DL = task + 0x1C0.
                    // 0x800A39CC holds the pointer; it is not the arena base.
                    word(0x800A39CC) = word(0x800A2BFC) + 0x1C0;
                    lambo_sky_backdrop_begin(rdram);
                    const uint32_t start = word(0x800A39CC);
                    lambo_sky_panorama_start(rdram);
                    word(0x800A39CC) += 432;
                    lambo_sky_extend_panorama(rdram);
                    require(static_cast<uint32_t>(word(0x800A39CC)) == start + 24,
                        "Extension must consume only three commands in the game's task arena");
                    require(static_cast<uint32_t>(word(start + 8)) == 0x06000000u,
                        "F3D G_DL must preserve the extended address marker");
                    const uint32_t list = word(start + 12);
                    require(list >= 0x81000000u && list < 0x82000000u,
                        "Display-list pointer must retain bit 31 and the full heap offset");
                    if (center == 0) first_list = list;
                    require(first_list == list, "A task/viewport must reuse its storage");
                    uint32_t cursor = list;
                    const int radius = lambo_sky_column_radius(100.0, 32.0 / 9.0, frame == 0);
                    int quads = 0;
                    for (int band = 0; band < 4; ++band) {
                        const int row = band % 2;
                        const bool cap = band >= 2;
                        int previous_x = -radius * 128;
                        do {
                            require(++quads <= 320, "Angular list must fit its allocation");
                            require(static_cast<uint32_t>(word(cursor)) == 0xFD100000u &&
                                static_cast<uint32_t>(word(cursor + 56)) == 0x0400103Fu &&
                                static_cast<uint32_t>(word(cursor + 64)) == 0xB1000204u &&
                                static_cast<uint32_t>(word(cursor + 68)) == 0x00040600u,
                                "Each quad must load a texture, four vertices and two F3D triangles");
                            const uint32_t vertices = word(cursor + 60);
                            const int x0 = half(vertices + 16);
                            const int x1 = half(vertices);
                            require(x0 == previous_x && x1 > x0, "Slices must join without gaps or overlaps");
                            previous_x = x1;
                            const double mid_u = phase + std::atan(((x0 + x1) * 0.5 - phase) / 220.0)
                                * 512.0 / 3.14159265358979323846;
                            const int column = static_cast<int>(std::floor(mid_u / 128.0));
                            const int tile = ((center + column) % 8 + 8) % 8;
                            require(word(cursor + 4) == word(0x80200254 + row * 32 + tile * 4),
                                "Texture selection must follow the camera ray bearing");
                            const bool mirror = (half(0x80089054 + track * 4 + row * 2) & (1 << tile)) != 0;
                            for (int v = 0; v < 4; ++v) {
                                const double bearing = std::atan((half(vertices + v * 16) - phase) / 220.0);
                                double fraction = half(vertices + v * 16 + 8) / 2016.0;
                                if (mirror) fraction = 1.0 - fraction;
                                const double expected_bearing = ((column + fraction) * 128.0 - phase)
                                    * 3.14159265358979323846 / 512.0;
                                require(std::abs(bearing - expected_bearing) < 0.003,
                                    "Panorama must match camera bearings: 45 degrees per tile");
                                const int source_vertex = !cap ? v : row == 0
                                    ? ((v == 0 || v == 3) ? 0 : 1)
                                    : ((v == 0 || v == 3) ? 3 : 2);
                                require(half(vertices + v * 16 + 4) == -220 &&
                                    half(vertices + v * 16 + 10) == (source_vertex < 2 ? 0 : 1984) &&
                                    word(vertices + v * 16 + 12) == -1,
                                    "Depth, vertical UVs and colour must remain unchanged");
                                const int expected_y = cap && ((row == 0 && v < 2) || (row == 1 && v >= 2))
                                    ? (row == 0 ? kSkyEdgeExtent : -kSkyEdgeExtent)
                                    : 128 - row * 96 - (source_vertex >= 2 ? 96 : 0);
                                require(half(vertices + v * 16 + 2) == expected_y,
                                    "Caps must join the original edge and continue edge pixels");
                            }
                            cursor += 72;
                        } while (previous_x < (radius + 1) * 128);
                        require(previous_x == (radius + 1) * 128, "Every band must fill the coverage envelope");
                    }
                    require(static_cast<uint32_t>(word(cursor)) == 0xB8000000u,
                        "Sublist must return before the original emitter restores render state");
                    lambo_sky_backdrop_end(rdram);
                }
                for (uint32_t other : task_lists) require(other != first_list, "In-flight tasks and viewports must not alias storage");
                task_lists.push_back(first_list);
            }
        }
    }
    // Reject stale, unaligned and overflowing cursors without writing to them.
    // Open/close the group at a valid cursor so this targets the extension seam.
    const uint32_t task = 0x800BF240u;
    word(0x800A2BFC) = static_cast<int32_t>(task);
    word(0x800A39CC) = static_cast<int32_t>(task + 0x1C0u);
    lambo_sky_backdrop_begin(rdram);
    const uint32_t missing_start = word(0x800A39CC);
    lambo_sky_extend_panorama(rdram);
    require(static_cast<uint32_t>(word(0x800A39CC)) == missing_start,
            "Missing start marker must preserve the original list");
    lambo_sky_panorama_start(rdram);
    const uint32_t replacement_start = word(0x800A39CC);
    for (uint32_t invalid : {0u, 0x800A4000u, task + 0x1B8u,
                             task + 0x1C1u, task + 0x79C0u - 32u, 0xFFFFFFF8u}) {
        word(0x800A39CC) = static_cast<int32_t>(invalid);
        lambo_sky_extend_panorama(rdram);
        require(static_cast<uint32_t>(word(0x800A39CC)) == invalid,
                "Invalid cursor must be rejected without emitting a sublist");
    }
    word(0x800A39CC) = static_cast<int32_t>(task + 0x79C0u - 40u);
    lambo_sky_extend_panorama(rdram);
    require(static_cast<uint32_t>(word(0x800A39CC)) == replacement_start + 24,
            "Valid replacement must discard original tiles and retain room for cleanup");
    word(0x800A39CC) = static_cast<int32_t>(replacement_start + 432);
    const int32_t valid_phase = word(0x80098E58);
    const float invalid_phase = 128.0f;
    std::memcpy(&word(0x80098E58), &invalid_phase, sizeof(invalid_phase));
    lambo_sky_extend_panorama(rdram);
    require(static_cast<uint32_t>(word(0x800A39CC)) == replacement_start + 432,
            "Invalid phase must preserve all original tile draws");
    word(0x80098E58) = valid_phase;
    const int32_t valid_texture = word(0x80200254);
    word(0x80200254) = static_cast<int32_t>(0x807FFFF8u);
    lambo_sky_extend_panorama(rdram);
    require(static_cast<uint32_t>(word(0x800A39CC)) == replacement_start + 432,
            "Texture load crossing RDRAM bounds must preserve all original draws");
    word(0x80200254) = valid_texture;
    word(0x800A39CC) = static_cast<int32_t>(task + 0x1C0u);
    word(0x800A2BFC) = static_cast<int32_t>(0xFFFFFFF8u);
    lambo_sky_extend_panorama(rdram);
    require(static_cast<uint32_t>(word(0x800A39CC)) == task + 0x1C0u,
            "Unknown task must be rejected before allocation or pointer arithmetic");
    lambo_sky_backdrop_end(rdram);
    std::cout << "Sky display-list geometry, textures, task storage and bounds passed\n";
}
