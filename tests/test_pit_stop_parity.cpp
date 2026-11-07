// Compare the reconstructed guest function with freshly recompiled ROM code.
// Both execute against the same fixture. Only scratch stack/register contents
// may differ; game RAM and the ordered calls to game services must agree.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <array>
#include "recomp.h"
#include "guest_patch_data.inc"

extern "C" void pit_reference(uint8_t*, recomp_context*);
extern "C" void func_8006B470(uint8_t*, recomp_context*);
static bool assistance = false;
static int sound_playing = 0;
static std::vector<std::array<uint32_t, 7>> calls;
extern "C" void lambo_get_automatic_pit_stops(uint8_t*, recomp_context* ctx) { ctx->r2 = assistance; }
// Frozen pre-migration input substitutions, confined to the ROM oracle.
extern "C" void pit_reference_start(uint8_t* rdram, recomp_context* ctx) {
    if (assistance) MEM_B(0x4f, ctx->r29) = 8;
}
extern "C" void pit_reference_fuel(uint8_t* rdram, recomp_context* ctx) {
    if (!assistance) return;
    unsigned slot = MEM_BU(0x5b,ctx->r29);
    int limit = MEM_W(0x229ca8 + slot*4,(gpr)(int32_t)0x80000000u);
    if (limit<1 || limit>44) return;
    int stick=7;
    for (int candidate=8;candidate<=70;++candidate) {
        if ((int)((candidate-7)*0.7)>=limit-1) break;
        stick=candidate;
    }
    MEM_B(0x4f,ctx->r29)=stick;
}
extern "C" void pit_reference_tyres(uint8_t* rdram, recomp_context* ctx) {
    if (!assistance) return;
    int x=0,y=0;
    switch(ctx->r16) {
        case 1: x=-60; break; case 2: x=60; break;
        case 3: y=60; break; case 4: y=-60; break; default: return;
    }
    MEM_B(0x4b,ctx->r29)=x; MEM_B(0x4f,ctx->r29)=y;
}
extern "C" void switch_error(const char* name, uint32_t address, uint32_t value) {
    std::cerr << name << " bad switch " << std::hex << address << ' ' << value << '\n'; std::abort();
}
static void call(int id, uint8_t* rdram, recomp_context* ctx, int argc) {
    std::array<uint32_t, 7> record{};
    record[0] = id;
    const gpr regs[] = {ctx->r4, ctx->r5, ctx->r6, ctx->r7};
    for (int i = 0; i < argc; ++i)
        record[i + 1] = i < 4 ? uint32_t(regs[i]) : uint32_t(MEM_W(16 + (i - 4) * 4, ctx->r29));
    calls.push_back(record);
}
extern "C" void func_80066F84(uint8_t* r, recomp_context* c) { call(1,r,c,5); }
extern "C" void func_800670BC(uint8_t* r, recomp_context* c) { call(2,r,c,6); }
extern "C" void func_800673A0(uint8_t* r, recomp_context* c) { call(3,r,c,1); }
extern "C" void func_80067428(uint8_t* r, recomp_context* c) { call(4,r,c,1); c->r2 = sound_playing; }
extern "C" void func_8006B320(uint8_t* r, recomp_context* c) { call(5,r,c,1); }
extern "C" void func_80075730(uint8_t*, recomp_context* c) {
    calls.push_back({6, c->f12.u32l}); c->f0.fl = std::sin(c->f12.fl);
}

static gpr addr(uint32_t a) { return (gpr)(int32_t)a; }
static void half(uint8_t* rdram, uint32_t a, int v) { MEM_H(0,addr(a)) = v; }
static void word(uint8_t* rdram, uint32_t a, uint32_t v) { MEM_W(0,addr(a)) = v; }
static void byte(uint8_t* rdram, uint32_t a, int v) { MEM_B(0,addr(a)) = v; }
static uint32_t rng = 0xc001d00d;
static uint32_t random_value() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static recomp_context context(uint8_t* rdram, int slot, int x, int y) {
    recomp_context ctx{};
    ctx.r29 = addr(0x807ff000);
    ctx.r4 = x; ctx.r5 = y; ctx.r6 = 0xaaaa; ctx.r7 = 0x5555;
    word(rdram,0x807ff010,slot);
    return ctx;
}
static bool same_ram(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int iteration) {
    // Caller/callee stack scratch is not persistent game state. Everything else,
    // including the other pit slot, animation data and relocated racers, is checked.
    if (std::memcmp(a.data(), b.data(), 0x7fe000) == 0 &&
        std::memcmp(a.data()+0x7ff020, b.data()+0x7ff020, a.size()-0x7ff020) == 0) return true;
    for (size_t i = 0; i < a.size(); ++i) {
        if (i >= 0x7fe000 && i < 0x7ff020) continue;
        if (a[i] != b[i]) {
            std::cerr << "RAM mismatch case " << iteration << " at " << std::hex
                      << (0x80000000u + i) << " reference=" << unsigned(a[i])
                      << " patch=" << unsigned(b[i]) << '\n';
            return false;
        }
    }
    return true;
}

int main() {
    std::vector<uint8_t> initial(0x924000);
    auto* rdram = initial.data();
    for (size_t i=0; i<sizeof(guest_patch_data); ++i) byte(rdram,0x80801000+i,guest_patch_data[i]);
    const double constants[] = {.7,.8,.8,.4,1.57079628,.0339559,.95,.95};
    for (unsigned i=0; i<std::size(constants); ++i) {
        uint64_t bits; std::memcpy(&bits,&constants[i],8);
        word(rdram,0x8008eb10+i*8,bits>>32); word(rdram,0x8008eb14+i*8,bits);
    }
    for (int slot=0;slot<2;++slot) {
        for(int part=0;part<16;++part) {
            word(rdram,0x8020fc1c+slot*64+part*4,0x80300000+slot*256+part*16);
            byte(rdram,0x80300008+slot*256+part*16,part+1);
        }
        half(rdram,0x80901102+slot*2,slot+1);
        half(rdram,0x8091000e +(slot+1)*0x10c,slot ? 5 : 1);
        half(rdram,0x80910012+(slot+1)*0x10c,slot);
    }
    for (int i=0;i<16;++i) {
        float v=0.1f*(i+1); uint32_t bits; std::memcpy(&bits,&v,4);
        word(rdram,0x8013e290+i*4,bits); word(rdram,0x8013e2d0+i*4,bits);
    }
    for (int i=0;i<4;++i) word(rdram,0x8020fb90+i*4,i+1);
    std::vector<uint8_t> reference(initial.size()), candidate(initial.size());
    for (int iteration=0;iteration<5120;++iteration) {
        assistance = iteration >= 2560;
        reference=initial; rdram=reference.data();
        int slot=iteration%2, stage=(iteration/2)%5;
        int x=(int)(random_value()%256)-128, y=(int)(random_value()%256)-128;
        byte(rdram,0x8020fbc0,stage);
        byte(rdram,0x8020fbcc+slot,random_value()%2);
        half(rdram,0x801fbcf4+slot*24,random_value()%16);
        half(rdram,0x801fde24+slot*24,random_value()%16);
        half(rdram,0x80207158+slot*24,random_value()%32);
        word(rdram,0x8020fca4+slot*4,random_value()%3072);
        word(rdram,0x8020fc9c+slot*4,random_value()%46);
        half(rdram,0x80229cb0+slot*2,random_value()%4 ? 0 : 1+random_value()%10);
        half(rdram,0x800a3a00+slot*2,random_value()%2 ? 0x4000 : 0);
        half(rdram,0x800a3a10+slot*2,random_value()%3);
        word(rdram,0x8020fba0+slot*4,random_value()%18);
        word(rdram,0x8020fba8+slot*4,random_value()%18);
        word(rdram,0x8020fbb0+slot*4,random_value()%4);
        word(rdram,0x800a2d00,random_value()%300);
        word(rdram,0x80229c78+slot*4,random_value()%2 ? 0 : random_value()%300);
        half(rdram,0x800ce6aa,slot+1);
        half(rdram,0x800ce79c,random_value()%2);
        sound_playing=random_value()%2;
        auto a=context(rdram,slot,x,y);
        candidate=reference; auto b=a;
        a.f_odd=&a.f0.u32h; b.f_odd=&b.f0.u32h;
        calls.clear(); pit_reference(reference.data(),&a); auto expected_calls=calls;
        calls.clear(); func_8006B470(candidate.data(),&b);
        if (expected_calls!=calls) {
            std::cerr << "Call mismatch case " << iteration << " stage " << stage << '\n';
            return 1;
        }
        if (!same_ram(reference,candidate,iteration)) return 1;
    }
    std::cout << "5120 stock/assisted pit updates agree in game RAM and service calls\n";
}
