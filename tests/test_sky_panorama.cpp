#include <cstdint>
#include <cstdlib>
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
                half(source + vertex * 16 + 8) = static_cast<int16_t>(mirror * 100 + vertex);
                word(source + vertex * 16 + 12) = -1;
            }
        }
    }
    std::vector<uint32_t> task_lists;
    for (int frame = 0; frame < 2; ++frame) {
        // Exercise the actual neighboring s16 fields: a word read of count
        // would accidentally use viewport index 2 as the two-player selector.
        half(0x800CE6A4) = static_cast<int16_t>(frame == 0 ? 2 : 4);
        word(0x800A2BFC) = static_cast<int32_t>(0x800BF1D8u + frame * 0x7A50);
        for (int player = 0; player < 4; ++player) {
            half(0x800CE6A6) = static_cast<int16_t>(player);
            uint32_t first_list = 0;
            for (int center = 0; center < 8; ++center) {
                half(0x800987E0) = static_cast<int16_t>(center);
                word(0x800A39CC) = static_cast<int32_t>(0x80400000u);
                lambo_sky_backdrop_begin(rdram);
                const uint32_t start = word(0x800A39CC);
                lambo_sky_extend_panorama(rdram);
                require(static_cast<uint32_t>(word(0x800A39CC)) == start + 24,
                    "Extension must consume only three commands in the game's task arena");
                require(static_cast<uint32_t>(word(start + 8)) == 0x06000000u,
                    "Extension must call its task-owned sublist");
                const uint32_t list = word(start + 12);
                if (center == 0) first_list = list;
                require(first_list == list, "A task/viewport must reuse its storage");
                uint32_t cursor = list;
                const int radius = lambo_sky_column_radius(100.0, 32.0 / 9.0, frame == 0);
                for (int row = 0; row < 2; ++row) {
                    for (int column = -radius; column <= radius; ++column) {
                        if (column >= -1 && column <= 1) continue;
                        const int tile = lambo_sky_tile_index(center, column);
                        require(static_cast<uint32_t>(word(cursor)) == 0xFD100000u &&
                            word(cursor + 4) == word(0x80200254 + row * 32 + tile * 4),
                            "Extension must select the adjacent panorama texture with eight-tile wrapping");
                        require(static_cast<uint32_t>(word(cursor + 56)) == 0x0400103Fu &&
                            static_cast<uint32_t>(word(cursor + 64)) == 0xB1000204u &&
                            static_cast<uint32_t>(word(cursor + 68)) == 0x00040600u,
                            "Each quad must use a fresh four-vertex load and matching triangles");
                        const uint32_t vertices = word(cursor + 60);
                        const bool mirror = (half(0x80089054 + row * 2) & (1 << tile)) != 0;
                        for (int v = 0; v < 4; ++v) {
                            require(half(vertices + v * 16) == column * 128 + ((v == 0 || v == 3) ? 128 : 0),
                                "Columns must extend geometry at the original spacing");
                            require(half(vertices + v * 16 + 4) == -220 &&
                                half(vertices + v * 16 + 8) == int(mirror) * 100 + v &&
                                word(vertices + v * 16 + 12) == -1,
                                "Extension must preserve depth, texture mirroring and colour");
                        }
                        cursor += 72;
                    }
                }
                require(static_cast<uint32_t>(word(cursor)) == 0xB8000000u,
                    "Sublist must return before the original emitter restores render state");
                lambo_sky_backdrop_end(rdram);
            }
            for (uint32_t other : task_lists) require(other != first_list, "In-flight tasks and viewports must not alias storage");
            task_lists.push_back(first_list);
        }
    }
    std::cout << "Sky display-list geometry, textures and task storage passed\n";
}
