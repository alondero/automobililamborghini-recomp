#include "recomp.h"
#include "lambo_race_field.h"
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void func_80009158(uint8_t*, recomp_context*);

// Exercise the generated speed controller, including its actual table loads.
// Synthetic race: a human leader is well ahead of the AI. The high base pace
// makes the catch-up target exceed each authored difficulty/grid speed cap.
// No save-state or ROM data file is needed at test runtime.
void test_ai_speed_limits() {
    std::vector<uint8_t> memory(0x924000);
    auto* rdram = memory.data();
    auto h = [&](uint32_t a) -> int16_t& { return MEM_H(0, (gpr)(int32_t)a); };
    auto f = [&](uint32_t a, float value) {
        std::memcpy(&MEM_W(0, (gpr)(int32_t)a), &value, sizeof(value));
    };
    auto d = [&](uint32_t a, double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        MEM_W(0, (gpr)(int32_t)a) = bits >> 32;
        MEM_W(4, (gpr)(int32_t)a) = bits;
    };
    const float gaps[] = {0, 0, 3, 8, 11, 16, 20, 25, 28};
    const float caps[2][9] = {{0, 95, 93, 91, 89, 88, 87, 85, 85},
                             {0, 103, 101, 99, 98, 97, 96, 95, 95}};
    for (int difficulty = 0; difficulty < 2; ++difficulty) {
        for (int count : {2, 6, 8, 9, 16}) {
            for (int slot = 1; slot <= count; ++slot) {
                std::fill(memory.begin(), memory.end(), 0);
                for (int i = 0; i <= 8; ++i) {
                    f(0x80088F58 + i * 4, gaps[i]);
                    for (int row = 0; row < 2; ++row)
                        f(0x80088F7C + row * 36 + i * 4, caps[row][i]);
                }
                d(0x8008D808, .01);
                d(0x8008D7F8, .1);
                d(0x8008D800, .4);
                d(0x8008D830, .0025);
                h(0x800CE6B0) = 3; // active race phase
                h(0x800CE6B4) = 2; // single race
                h(0x800CE6A4) = 1;
                h(0x800CE7AC) = count;
                h(0x800CE79C) = difficulty;
                h(0x80098736) = 1; // leader
                h(0x8009873A) = 1000; // base pace before per-grid cap
                for (int racer = 1; racer <= count; ++racer) {
                    const uint32_t ai = LAMBO_FIELD_AI + racer * 0x84;
                    h(LAMBO_FIELD_BODIES + racer * 2) = racer;
                    h(0x80910000 + racer * 0x10C + 0xE) = racer == 1 ? 1 : racer + 4;
                    h(ai + 4) = racer;
                    h(ai + 0xA) = racer == 1 ? 4 : 2;
                    f(ai + 0x1C, 60 - racer * 5);
                    h(ai + 0x36) = slot;
                    f(ai + 0x38, 0);
                }
                recomp_context ctx{};
                ctx.f_odd = &ctx.f0.u32h;
                ctx.r29 = (gpr)(int32_t)0x807FF000;
                func_80009158(rdram, &ctx);
                // The last entrant leaves its rank-based gap threshold in the
                // controller's float scratch. Check this independent lookup too;
                // a speed-cap-only test would miss its out-of-range read.
                float gap;
                std::memcpy(&gap, &MEM_W(0, (gpr)(int32_t)0x800CE790), sizeof(gap));
                const float expected_gap = gaps[count > 8 ? 8 : count];
                if (gap != expected_gap) {
                    std::fprintf(stderr, "AI gap: count=%d gap=%g expected=%g\n", count, gap, expected_gap);
                    std::exit(1);
                }
                for (int racer = 2; racer <= count; ++racer) {
                    assert(h(LAMBO_FIELD_AI + racer * 0x84 + 4) == racer);
                    assert(h(LAMBO_FIELD_AI + racer * 0x84 + 0x36) == slot);
                    // Catch-up caps apply when a car falls behind its grid seed.
                    if (racer <= slot) continue;
                    float target;
                    const uint32_t address = LAMBO_FIELD_AI + racer * 0x84 + 0x3C;
                    std::memcpy(&target, &MEM_W(0, (gpr)(int32_t)address), sizeof(target));
                    const float expected = caps[difficulty][slot > 8 ? 8 : slot];
                    if (target != expected) {
                        std::fprintf(stderr, "AI cap: difficulty=%d count=%d slot=%d rank=%d target=%g expected=%g\n",
                                     difficulty, count, slot, racer, target, expected);
                        std::exit(1);
                    }
                }
            }
        }
    }
}
