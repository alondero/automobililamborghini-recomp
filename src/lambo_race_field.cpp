#include "lambo_race_field.h"
#include "lambo_config.h"
#include "lambo_vehicle.h"
#include "recomp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
constexpr uint32_t count_addr = 0x800CE7AC;
constexpr uint32_t meta = LAMBO_FIELD_BASE + 0x1400;
constexpr uint32_t grid = LAMBO_FIELD_BASE + 0x3000;
int16_t& half(uint8_t* rdram, uint32_t a) { return MEM_H(0, (gpr)(int32_t)a); }
int32_t& word(uint8_t* rdram, uint32_t a) { return MEM_W(0, (gpr)(int32_t)a); }
int requested_count() {
    const char* value = std::getenv("LAMBO_RACE_CARS");
    if (!value) return lambo::config::race_cars();
    char* end;
    long n = std::strtol(value, &end, 10);
    return *end || n < 2 || n > 16 ? 6 : (int)n;
}
bool customized(uint8_t* rdram) {
    return half(rdram, meta) != 6 && half(rdram, meta) >= 2 && half(rdram, meta) <= 16;
}
void text(uint8_t* rdram, const char* value) {
    for (unsigned i = 0; ; ++i) {
        MEM_B(0, (gpr)(int32_t)(0x801797E0 + i)) = value[i];
        if (!value[i]) break;
    }
}
}

// Game-thread setup hooks. All addresses are USA guest addresses; MEM_* handles
// word-swizzled RAM. The reserved extension is below the mod heap (0x81000000)
// and above the runtime PI handles (0x80800000). See docs/race-fields.md.
extern "C" void lambo_field_count(uint8_t* rdram) {
    const int mode = half(rdram, 0x800CE6B4);
    const int round = half(rdram, 0x800CE6BA);
    if (half(rdram, 0x800CE6A4) != 1 || mode < 1 || mode > 3) {
        half(rdram, meta) = 6;
        return;
    }
    const bool continuation = (mode == 3 && (round > 1 || half(rdram, meta + 8))) || (mode == 1 && round > 0);
    int n = continuation && half(rdram, meta + 2) == mode ? half(rdram, meta) : requested_count();
    half(rdram, meta + 8) = 0;
    if (n < 2 || n > 16) n = 6;
    half(rdram, meta) = n;
    half(rdram, meta + 2) = mode;
    half(rdram, count_addr) = n;
}
extern "C" int lambo_field_roster(uint8_t* rdram) {
    if (!customized(rdram)) return 0;
    const int player = half(rdram, 0x800CE7E8);
    if (player < 0 || player >= 24) return 0;
    for (int i = 0; i < half(rdram, count_addr); ++i)
        half(rdram, LAMBO_FIELD_ROSTER + i * 2) = ((player / 3 + i) % 8) * 3 + (player % 3 + i / 8) % 3;
    return 1;
}
extern "C" void lambo_field_grid_begin(uint8_t* rdram, recomp_context*) {
    if (!customized(rdram)) return;
    const uint32_t original = word(rdram, 0x80098238);
    word(rdram, meta + 4) = original;
    // Preserve all track metadata; only one start record is substituted per car.
    std::memcpy(rdram + (grid & 0xFFFFFF), rdram + (original & 0xFFFFFF), 0x400);
    word(rdram, 0x80098238) = grid;
}
extern "C" void lambo_field_grid_slot(uint8_t* rdram, recomp_context* ctx) {
    if (!customized(rdram)) return;
    const int i = MEM_H(0x5E, ctx->r29);
    const int direction = half(rdram, 0x800CE7A8) ? 8 : 0;
    const uint32_t original = word(rdram, meta + 4);
    // Keep larger fields inside the authored four-row starting area. Interpolate
    // each lane separately, including the reverse grid's changing heading.
    // Do not extrapolate road geometry beyond the authored starting area.
    const int rows = (half(rdram, count_addr) + 1) / 2;
    const int denominator = rows > 4 ? rows - 1 : 1;
    const int numerator = rows > 4 ? (i / 2) * 3 : i / 2;
    const int row = numerator / denominator;
    const int remainder = numerator % denominator;
    const uint32_t source = original + 0x14 + (direction + row * 2 + (i & 1)) * 28;
    const uint32_t target = grid + 0x14 + direction * 28;
    std::memcpy(rdram + (target & 0xFFFFFF), rdram + (source & 0xFFFFFF), 28);
    if (remainder) {
        for (int offset : {0, 4, 8, 12}) {
            int delta = word(rdram, source + 56 + offset) - word(rdram, source + offset);
            if (offset == 12) {
                if (delta > 180) delta -= 360;
                if (delta < -180) delta += 360;
            }
            word(rdram, target + offset) += delta * remainder / denominator;
        }
    }
    MEM_H(0x5C, ctx->r29) = direction;
}
extern "C" void lambo_field_grid_end(uint8_t* rdram) {
    if (!customized(rdram)) return;
    word(rdram, 0x80098238) = word(rdram, meta + 4);
}
extern "C" void lambo_field_rank_text(uint8_t* rdram, int rank, int with_count) {
    char buf[16];
    if (with_count) std::snprintf(buf, sizeof(buf), "%d/%d", rank, half(rdram, count_addr));
    else std::snprintf(buf, sizeof(buf), "%d", rank);
    text(rdram, buf);
}

// Game-thread large-font texture fallback. The native 24x24 RGBA32 atlas only
// contains 1-6. Reuse the complete 16x16 RGBA32 HUD font at 0x8013F820 for
// 0/7/8/9, retaining its shading with a gold tint. Scratch tiles occupy
// 0x80904000..0x80909A00, inside the snapshotted race extension.
extern "C" uint32_t lambo_field_digit_texture(uint8_t* rdram, int digit, uint32_t original) {
    if (digit < 0 || digit > 9 || (digit >= 1 && digit <= 6)) return original;
    const uint32_t target = LAMBO_FIELD_BASE + 0x4000 + digit * 0x900;
    const uint32_t source = 0x8013F820 + digit * 0x400;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 24; ++x) {
            const uint32_t pixel = word(rdram, source + ((y * 2 / 3) * 16 + x * 2 / 3) * 4);
            const uint32_t shade = (pixel >> 8) & 255;
            word(rdram, target + (y * 24 + x) * 4) =
                ((shade * 234 / 255) << 24) | ((shade * 223 / 255) << 16) |
                ((shade * 130 / 255) << 8) | (pixel & 255);
        }
    }
    return target;
}

extern "C" int lambo_field_car(uint8_t* rdram, int object) {
    return object >= 0 && object < LAMBO_VEHICLE_CAPACITY &&
        (half(rdram, LAMBO_VEHICLE_BASE + object * sizeof(LamboVehicleRecord)) & 8) != 0;
}
extern "C" void lambo_field_prepare(uint8_t* rdram) {
    const int mode = half(rdram, 0x800CE6B4);
    if (half(rdram, 0x800CE6A4) != 1 || mode < 1 || mode > 3)
        half(rdram, meta) = 6;
}
extern "C" void lambo_field_standings(uint8_t* rdram, recomp_context* ctx, int column) {
    const int count = half(rdram, count_addr);
    if (count <= 6) return;
    if (column == 4) {
        if (count <= 8) return;
        text(rdram, "POS NAME    P POS NAME    P");
        ctx->r4 = (gpr)(int32_t)0x801797E0;
        ctx->r5 = 48;
        return;
    }
    const int rank = half(rdram, 0x800986C8);
    ctx->r6 = 94 + ((rank - 1) % 8) * 11;
    if (count <= 8) return;
    const int x = rank > 8 ? 112 : 0;
    ctx->r5 = x + (column == 0 ? 60 : column == 1 ? 148 : 76);
    if (column == 3) {
        char name[16];
        std::snprintf(name, sizeof(name), "CPU %d", half(rdram, 0x800CE6AA) - 1);
        text(rdram, name);
        ctx->r4 = (gpr)(int32_t)0x801797E0;
    } else if (column == 2) {
        char name[8]{};
        for (int i = 0; i < 7; ++i) {
            name[i] = MEM_BU(i, ctx->r4);
            if (!name[i]) break;
        }
        text(rdram, name);
        ctx->r4 = (gpr)(int32_t)0x801797E0;
    }
}

// Native pause-restart decrements the championship round before rebuilding it.
// Remember that transition so restarting round one does not become a new session.
extern "C" void lambo_field_restart(uint8_t* rdram) {
    half(rdram, meta + 8) = half(rdram, 0x800CE6B4) == 3 && half(rdram, meta + 2) == 3;
}
