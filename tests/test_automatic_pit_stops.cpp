#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
#include "recomp.h"

extern "C" void switch_error(const char*, uint32_t, uint32_t) { std::abort(); }
extern "C" void func_8006B470(uint8_t*, recomp_context*);
extern "C" void lambo_pit_start(uint8_t*, recomp_context*);
extern "C" void lambo_pit_fuel(uint8_t*, recomp_context*);
extern "C" void lambo_pit_tyres(uint8_t*, recomp_context*);
namespace { bool enabled = false; int errors = 0; }
namespace lambo::config { bool automatic_pit_stops() { return enabled; } }
// Sound calls have no effect on service progress. The ROM sine call uses f12/f0.
extern "C" void func_80066F84(uint8_t*, recomp_context*) {}
extern "C" void func_800670BC(uint8_t*, recomp_context*) {}
extern "C" void func_800673A0(uint8_t*, recomp_context*) {}
extern "C" void func_80067428(uint8_t*, recomp_context* ctx) { ctx->r2 = 1; }
extern "C" void func_8006B320(uint8_t*, recomp_context*) { ++errors; }
extern "C" void func_80075730(uint8_t*, recomp_context* ctx) { ctx->f0.fl = std::sin(ctx->f12.fl); }
constexpr gpr a(uint32_t value) { return (gpr)(int32_t)value; }
int failures = 0;
void expect(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void constant(uint8_t* rdram, uint32_t address, double value) {
    union { double d; uint64_t u; } bits{value};
    MEM_W(0, a(address)) = bits.u >> 32;
    MEM_W(4, a(address)) = bits.u;
}
int main() {
    for (unsigned slot = 0; slot < 2; ++slot) {
        std::vector<uint8_t> ram(0x800000);
        auto* rdram = ram.data();
        recomp_context ctx{};
        ctx.f_odd = &ctx.f0.u32h;
        ctx.r29 = a(0x807FF000);
        auto tick = [&](int x = 0, int y = 0) {
            ctx.r4 = x; ctx.r5 = y; ctx.r6 = ctx.r7 = 0;
            MEM_W(0x10, ctx.r29) = slot;
            func_8006B470(rdram, &ctx);
        };
        constant(rdram, 0x8008EB10, .7);
        constant(rdram, 0x8008EB18, .8);
        constant(rdram, 0x8008EB20, .8);
        constant(rdram, 0x8008EB28, .4);
        constant(rdram, 0x8008EB30, 1.57079628);
        constant(rdram, 0x8008EB38, .0339559);
        MEM_B(0, a(0x8020FBC0)) = 1; // waiting for fuel input
        MEM_B(1, a(0x8020FBC0)) = 2; // fuel
        MEM_B(2, a(0x8020FBC0)) = 3; // tyres
        enabled = false;
        tick();
        expect(MEM_BU(slot, a(0x8020FBC8)) == 0, "manual waiting state does not advance");
        enabled = true;
        tick();
        expect(MEM_BU(slot, a(0x8020FBC8)) == 1, "assistance starts refuelling");
        MEM_H(slot * 24, a(0x801FBCF4)) = 13; // enable stock spill detection
        int frames = 0;
        while (MEM_BU(slot, a(0x8020FBC8)) == 1 && frames++ < 200) {
            tick(-80, -80); // hostile physical input must not impede assistance
            expect(MEM_H(slot * 2, a(0x80229CB0)) == 0, "no fuel spill penalty");
        }
        expect(frames < 100, "refuelling completes at near-maximum stock rate");
        expect(MEM_BU(slot, a(0x8020FBC8)) == 2, "stock full-fuel transition executes");
        expect(MEM_W(slot * 4, a(0x8020FCA4)) >= 46 * 64, "tank filled by stock code");
        expect(MEM_W((1 - slot) * 4, a(0x8020FCA4)) == 0, "other slot untouched");
        // Each rotation reads this authored direction order. Four quarter turns
        // fit one tyre, with the ROM's progress counter stopping at sixteen.
        for (int i = 0; i < 4; ++i) MEM_W(i * 4, a(0x8020FB90)) = i + 1;
        // Geometry pointers touched by the stock tyre-completion routine.
        for (int i = 0; i < 16; ++i)
            MEM_W(slot * 64 + i * 4, a(0x8020FC1C)) = a(0x80300000 + i * 16);
        for (int i = 0; i < 16; ++i) tick(80, 80);
        expect(MEM_W(slot * 4, a(0x8020FBA0)) == 16, "four tyres fitted by stock rotation logic");
        expect(errors == 0, "no incorrect-direction reset");
        // Mid-stop disable immediately returns control to the player.
        MEM_W(slot * 4, a(0x8020FBA0)) = 0;
        enabled = false;
        tick();
        expect(MEM_W(slot * 4, a(0x8020FBA0)) == 0, "disabled assistance leaves tyres manual");
        // Disabled hooks are byte-for-byte no-ops, including hostile slot values.
        MEM_B(0x5B, ctx.r29) = 255;
        const auto before = ram;
        lambo_pit_start(rdram, &ctx); lambo_pit_fuel(rdram, &ctx); lambo_pit_tyres(rdram, &ctx);
        expect(before == ram, "disabled hooks preserve memory");
        enabled = true;
        lambo_pit_start(rdram, &ctx); lambo_pit_fuel(rdram, &ctx); lambo_pit_tyres(rdram, &ctx);
        expect(before == ram, "invalid slot fails closed");
    }
    return failures ? 1 : 0;
}
