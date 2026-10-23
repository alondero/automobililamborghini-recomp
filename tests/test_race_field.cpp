#include "recomp.h"
#include "lambo_race_field.h"
#undef NDEBUG
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>
#include <cstdio>
extern "C" {
void lambo_field_count(uint8_t*);
void lambo_field_restart(uint8_t*);
int lambo_field_roster(uint8_t*);
void lambo_field_prepare(uint8_t*);
void lambo_field_grid_begin(uint8_t*, recomp_context*);
void lambo_field_grid_slot(uint8_t*, recomp_context*);
void lambo_field_grid_end(uint8_t*);
void lambo_field_rank_text(uint8_t*, int, int);
void lambo_field_standings(uint8_t*, recomp_context*, int);
int lambo_field_car(uint8_t*, int);
}
extern "C" void lambo_player_name_seed(uint8_t*, recomp_context*) {}
extern "C" int lambo_cheat_reverse_tracks() { return 0; }
extern "C" void switch_error(const char*, uint32_t, uint32_t) { std::abort(); }
extern "C" void do_break(uint32_t) { std::abort(); }
extern "C" void func_8003CD84(uint8_t*, recomp_context*);
extern "C" void func_80054FFC(uint8_t*, recomp_context*);
extern "C" void func_8004C070(uint8_t*, recomp_context*);
extern "C" void func_80037F5C(uint8_t*, recomp_context* ctx) { ctx->f0.fl = 0; }
extern "C" void func_800751E4(uint8_t*, recomp_context*) {}
extern "C" void func_80075278(uint8_t*, recomp_context*) {}
extern "C" void func_80076024(uint8_t*, recomp_context*) {}
// Results initialization redraw/asset setup has no effect on points or ranking.
extern "C" void func_80011710(uint8_t*, recomp_context*) {}
extern "C" void func_80013328(uint8_t*, recomp_context*) {}
extern "C" void func_80054BCC(uint8_t*, recomp_context*) {}
namespace { int requested = 6; }
namespace lambo::config { int race_cars() { return requested; } }
void test_ai_speed_limits();
int main() {
    test_ai_speed_limits();
    std::vector<uint8_t> memory(0x924000);
    auto* rdram = memory.data();
    auto h = [&](uint32_t a) -> int16_t& { return MEM_H(0, (gpr)(int32_t)a); };
    auto w = [&](uint32_t a) -> int32_t& { return MEM_W(0, (gpr)(int32_t)a); };
    recomp_context ctx{};
    ctx.r29 = (gpr)(int32_t)0x807FF000;
    // The real large-font emitter must draw every decimal digit, not just the
    // native 1-6. Counting texrects catches unsupported glyphs independently of
    // the string-formatting hook.
    for (int digit : {0, 7, 8, 9}) {
        for (int pixel = 0; pixel < 256; ++pixel)
            w(0x8013F820 + digit * 0x400 + pixel * 4) = 0x1122FF7F;
    }
    w(0x80909A00) = 0x12345678;
    for (int rank = 1; rank <= 16; ++rank) {
        lambo_field_rank_text(rdram, rank, 0);
        w(0x800A39CC) = 0x80400000;
        ctx.r4 = (gpr)(int32_t)0x801797E0;
        ctx.r5 = 100; ctx.r6 = 60; ctx.r7 = 0x3F800000;
        w((uint32_t)ctx.r29 + 0x10) = 255;
        func_8004C070(rdram, &ctx);
        int glyphs = 0;
        for (uint32_t a = 0x80400000; a < (uint32_t)w(0x800A39CC); a += 8)
            if ((uint32_t)w(a) >> 24 == 0xE4) ++glyphs;
        std::fprintf(stderr, "rank %d: %d glyphs\n", rank, glyphs);
        assert(glyphs == (rank >= 10 ? 2 : 1));
    }
    for (int digit : {0, 7, 8, 9})
        assert((uint32_t)w(0x80904000 + digit * 0x900) == 0xEADF827F);
    assert(w(0x80909A00) == 0x12345678);
    // Execute the actual minimap emitter with separated route points and stale
    // native AI storage. Every entrant must produce a distinct dot rectangle.
    for (int n : {2, 6, 10, 16}) {
        h(0x800CE7AC) = n;
        w(0x800A39CC) = 0x80400000;
        w(0x800BF1C8) = 0x80300000;
        double factor = 1;
        uint64_t bits;
        std::memcpy(&bits, &factor, 8);
        SD(bits, 0, (gpr)(int32_t)0x8008E5C0);
        SD(bits, 0, (gpr)(int32_t)0x8008E5C8);
        for (int id = 1; id <= n; ++id) {
            h(LAMBO_FIELD_AI + id * 0x84 + 0xE) = id;
            h(LAMBO_FIELD_BODIES + id * 2) = id;
            h(0x80910000 + id * 0x10C + 0xE) = id == 1 ? 1 : id + 4;
            float x = id * 4.0f;
            std::memcpy(&w(0x80300000 + id * 16), &x, 4);
        }
        ctx.r6 = 0x3F800000;
        ctx.r7 = 0;
        ctx.f12.fl = 100;
        ctx.f14.fl = 100;
        w((uint32_t)ctx.r29 + 0x10) = 0x3F800000;
        w((uint32_t)ctx.r29 + 0x14) = 0;
        func_80054FFC(rdram, &ctx);
        std::set<uint32_t> dots;
        int labels = 0;
        for (uint32_t a = 0x80400000; a < (uint32_t)w(0x800A39CC); a += 8) {
            if ((uint32_t)w(a) >> 24 != 0xE4) continue;
            const int width = ((w(a) >> 12) & 0xFFF) - ((w(a + 4) >> 12) & 0xFFF);
            if (width == 8) dots.insert(w(a));
            else ++labels;
        }
        std::fprintf(stderr, "map %d: %zu distinct rectangles\n", n, dots.size());
        assert(dots.size() == (size_t)n);
        assert(labels == 1);
    }
    for (int n = 2; n <= 16; ++n) {
        requested = n;
        h(0x800CE6A4) = 1; h(0x800CE6B4) = 2; h(0x800CE6BA) = 0;
        lambo_field_count(rdram);
        assert(h(0x800CE7AC) == n);
        for (int player = 0; player < 24; ++player) {
            h(0x800CE7E8) = player;
            const int changed = lambo_field_roster(rdram);
            assert(changed == (n != 6));
            if (!changed) continue;
            std::set<int> models;
            for (int i = 0; i < n; ++i) {
                int model = h(LAMBO_FIELD_ROSTER + i * 2);
                assert(model >= 0 && model < 24);
                models.insert(model);
            }
            assert(models.size() == n && h(LAMBO_FIELD_ROSTER) == player);
        }
        // Synthetic authored grids: four rows per direction, 2500-unit pitch.
        for (int reverse = 0; reverse < 2; ++reverse) {
            h(0x800CE7A8) = reverse;
            w(0x80098238) = (int32_t)0x80200000;
            for (int i = 0; i < 16; ++i) {
                uint32_t a = 0x80200014 + i * 28;
                w(a) = (i & 1) * 1000; w(a + 4) = 900;
                w(a + 8) = (i % 8 / 2) * 2500; w(a + 12) = reverse ? 350 : 70;
                w(a + 20) = reverse ? 36 : 38;
            }
            if (n == 6) continue;
            lambo_field_grid_begin(rdram, &ctx);
            int previous[2] = {-2500, -2500};
            for (int i = 0; i < n; ++i) {
                MEM_H(0x5E, ctx.r29) = i;
                lambo_field_grid_slot(rdram, &ctx);
                uint32_t a = (uint32_t)w(0x80098238) + 0x14 + MEM_H(0x5C, ctx.r29) * 28;
                assert(w(a) == (i & 1) * 1000 && w(a+4) == 900);
                assert(w(a+8) >= 0 && w(a+8) <= 7500);
                assert(w(a+8) - previous[i & 1] >= 1000);
                assert(w(a+20) == (reverse ? 36 : 38));
                previous[i & 1] = w(a+8);
            }
            lambo_field_grid_end(rdram);
            assert((uint32_t)w(0x80098238) == 0x80200000);
        }
    }
    // Execute the actual generated standings initializer, retaining production
    // relocation/scoring hooks. Reverse finish order exercises high racer IDs.
    for (int n = 2; n <= 16; ++n) {
        h(0x800CE7AC) = n; h(0x80098562) = 26;
        for (int place = 1; place <= 6; ++place) h(0x801EA164 + place * 2) = 7 - place;
        for (int id = 1; id <= n; ++id) {
            h(LAMBO_FIELD_AI + id * 0x84 + 6) = n + 1 - id;
            h(LAMBO_FIELD_POINTS + id * 2) = 0;
        }
        for (int round = 1; round <= 6; ++round) {
            func_8003CD84(rdram, &ctx);
            std::set<int> ranks;
            for (int id = 1; id <= n; ++id) {
                const int place = n + 1 - id;
                assert(h(LAMBO_FIELD_POINTS + id * 2) == round * (place <= 6 ? 7 - place : 0));
                ranks.insert(h(LAMBO_FIELD_RANKS + id * 2));
            }
            assert(ranks.size() == n && *ranks.begin() == 1 && *ranks.rbegin() == n);
        }
    }
    requested = 16; h(0x800CE6B4) = 3; h(0x800CE6BA) = 1;
    lambo_field_count(rdram);
    requested = 2; h(0x800CE6BA) = 2;
    lambo_field_count(rdram);
    assert(h(0x800CE7AC) == 16);
    lambo_field_restart(rdram); // Native pause handler marks the restart.
    h(0x800CE6BA) = 1; lambo_field_count(rdram);
    assert(h(0x800CE7AC) == 16); // First-round restart retains its session.
    h(0x800CE6BA) = 1; lambo_field_count(rdram);
    assert(h(0x800CE7AC) == 2);
    for (int mode : {0, 4}) {
        h(0x800CE6B4) = mode; h(0x800CE7AC) = 1;
        lambo_field_prepare(rdram); lambo_field_count(rdram);
        assert(h(0x800CE7AC) == 1 && !lambo_field_roster(rdram));
    }
    h(0x800CE6A4) = 2; h(0x800CE6B4) = 2; h(0x800CE7AC) = 4;
    lambo_field_prepare(rdram); lambo_field_count(rdram);
    assert(h(0x800CE7AC) == 4 && !lambo_field_roster(rdram));
    h(0x800CE7AC) = 16;
    // Both result menus must keep all sixteen rows inside the native panel,
    // including a full-width player name beside three-digit points.
    for (int rank = 1; rank <= 16; ++rank) {
        h(0x800986C8) = rank;
        for (int column = 0; column < 3; ++column) {
            ctx.r4 = (gpr)(int32_t)0x80200000;
            for (int i = 0; i < 12; ++i) MEM_B(i, ctx.r4) = 'A' + i;
            MEM_B(12, ctx.r4) = 0;
            lambo_field_standings(rdram, &ctx, column);
            assert(ctx.r5 >= 48 && ctx.r5 <= 268);
            assert(ctx.r6 >= 90 && ctx.r6 + 8 <= 180);
            if (column == 2) {
                assert(MEM_BU(7, ctx.r4) == 0);
                assert(ctx.r5 + 7 * 8 <= (rank <= 8 ? 136 : 248));
                assert(MEM_BU(7, (gpr)(int32_t)0x80200000) == 'H');
            }
        }
    }
    lambo_field_rank_text(rdram, 16, 1);
    const char* expected = "16/16";
    for (int i = 0; i <= 5; ++i) assert(MEM_BU(i, (gpr)(int32_t)0x801797E0) == expected[i]);
    h(0x80910000 + 46 * 0x10C) = 8;
    assert(lambo_field_car(rdram, 46) && !lambo_field_car(rdram, 0));
    assert(!lambo_field_car(rdram, -1) && !lambo_field_car(rdram, 64));
}
