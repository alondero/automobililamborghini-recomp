// Complete menu-handler parity against an independently recompiled USA ROM.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include "recomp.h"
#include "guest_patch_data.inc"

extern "C" void menu_reference(uint8_t*, recomp_context*);
extern "C" void func_800427D4(uint8_t*, recomp_context*);
static std::array<double, 4> scales{1.0, 1.0, 1.0, 1.0};
static unsigned samples;
static std::vector<std::array<uint32_t, 8>> calls;
static gpr address(uint32_t a) { return (gpr)(int32_t)a; }
static void half(uint8_t* rdram, uint32_t a, int v) { MEM_H(0,address(a)) = v; }
static void word(uint8_t* rdram, uint32_t a, uint32_t v) { MEM_W(0,address(a)) = v; }
static void byte(uint8_t* rdram, uint32_t a, int v) { MEM_B(0,address(a)) = v; }
static double sample_scale() { return scales[(samples++) % scales.size()]; }
extern "C" void lambo_get_menu_stick_sensitivity(uint8_t*, recomp_context* ctx) {
    ctx->f0.d = sample_scale();
}
extern "C" int menu_reference_scale(int input) {
    double scale = sample_scale();
    if (!(scale > 1.0)) return input;
    long value = std::lround(input * scale);
    return value > 127 ? 127 : value < -127 ? -127 : (int)value;
}
static void record(uint32_t id, uint8_t* rdram, recomp_context* ctx, bool sound) {
    std::array<uint32_t, 8> entry{id};
    if (sound) {
        entry[1] = (uint32_t)ctx->r4; entry[2] = (uint32_t)ctx->r5;
        entry[3] = (uint32_t)ctx->r6; entry[4] = (uint32_t)ctx->r7;
        entry[5] = MEM_W(16,ctx->r29);
    }
    // Calls must observe the same menu transition and highlight state.
    entry[6] = MEM_W(0,address(0x8009858c));
    entry[7] = MEM_W(0,address(0x80098590));
    calls.push_back(entry);
}
extern "C" void func_80066F84(uint8_t* r, recomp_context* c) { record(1,r,c,true); }
extern "C" void func_8003CD84(uint8_t* rdram, recomp_context* c) {
    record(2,rdram,c,false);
    // A deterministic stand-in for the original initializer: exercise writes
    // visible after returning from a guest callee, without testing that callee.
    half(rdram,0x80098560,MEM_H(0,address(0x80098562)));
}
static void activate(uint8_t* r, recomp_context* c) {
    record(3,r,c,false);
    half(r,0x80098562,9);
}
extern "C" recomp_func_t* get_function(int32_t vram) {
    if ((uint32_t)vram == 0x8003e80c) return activate;
    std::cerr << "Unexpected menu callback " << std::hex << (uint32_t)vram << '\n';
    std::abort();
}
extern "C" void switch_error(const char*, uint32_t, uint32_t) { std::abort(); }

static bool compare(const std::vector<uint8_t>& expected, const std::vector<uint8_t>& actual, unsigned n) {
    // Only compiler-owned stack scratch is excluded. All other RAM, including
    // pads, names, menu tables, callback writes and unrelated sentinels, agrees.
    if (std::memcmp(expected.data(),actual.data(),0x7fe000) == 0 &&
        std::memcmp(expected.data()+0x7ff000,actual.data()+0x7ff000,
                    expected.size()-0x7ff000) == 0) return true;
    for (size_t i=0;i<expected.size();++i) {
        if (i>=0x7fe000 && i<0x7ff000) continue;
        if (expected[i]!=actual[i]) {
            std::cerr << "Menu RAM mismatch case " << n << " at " << std::hex
                << (0x80000000u+i) << " reference=" << unsigned(expected[i])
                << " guest=" << unsigned(actual[i]) << '\n';
            return false;
        }
    }
    return true;
}

int main() {
    std::vector<uint8_t> initial(0x924000), expected, actual;
    auto* rdram = initial.data();
    for (size_t i=0;i<sizeof(guest_patch_data);++i) byte(rdram,0x80801000+(uint32_t)i,guest_patch_data[i]);
    const double fixed_scales[] = {1.0, 1.5, 2.0, 2.5};
    unsigned count = 0;
    for (unsigned n=0;n<2048;++n) {
        std::memset(rdram+0x98560,0,0x60);
        std::memset(rdram+0xa39d8,0,0x38);
        std::memset(rdram+0xa4819,0xa5,80);
        unsigned port = (n / 256) % 4;
        half(rdram,0x800ce6a6,port+1);
        half(rdram,0x800a39da,(port+1)%4);
        int screen = n%3 == 0 ? 13 : n%3 == 1 ? 21 : 2;
        half(rdram,0x80098560,screen);
        half(rdram,0x80098562,screen);
        half(rdram,0x80098564,2);
        half(rdram,0x80098568,3);
        half(rdram,0x8009856a,4);
        half(rdram,0x80098590,255);
        word(rdram,0x80098588,0x80300000);
        half(rdram,0x8009859c,0); half(rdram,0x8009859e,0);
        half(rdram,0x800985a0,320); half(rdram,0x800985a2,240);
        half(rdram,0x800985b2,20+(n%290));
        half(rdram,0x800985b4,60+(n%145));
        half(rdram,0x800985b6,n%16 == 0 ? 0 : n%16 == 1 ? -1 : 'A'+(n%26));
        half(rdram,0x800985b8,160); half(rdram,0x800985ba,140);
        half(rdram,0x800985bc,n%13);
        half(rdram,0x800a2d08,n%2);
        half(rdram,0x800ce6ac,2);
        for (unsigned p=0;p<4;++p) {
            byte(rdram,0x800a39e2+p*6,int(n%256)-128);
            byte(rdram,0x800a39e3+p*6,127-int(n%256));
            half(rdram,0x800a39e0+p*6,(n%7==0)?0xf00:0);
            half(rdram,0x800a39f8+p*2,0x20);
            half(rdram,0x800a3a08+p*2,3);
        }
        for (unsigned i=0;i<25;++i) half(rdram,0x80089a94+i*2,n%2);
        for (unsigned i=0;i<4;++i) {
            half(rdram,0x80300002+i*32,i==1 ? -1 : 0);
            word(rdram,0x80300004+i*32,0x8003e80c);
            for (unsigned d=0;d<4;++d) half(rdram,0x80300010+i*32+d*2,(i+1)%4);
        }
        scales.fill(fixed_scales[n/256%4]);
        if (n>=1792) scales={1.0,2.5,1.5,2.0}; // Live changes between original read sites.
        unsigned chosen = screen==2 ? (port+1)%4 : port;
        if (n<1536) {
            half(rdram,0x800985b0,1);
            half(rdram,0x800a3a00+chosen*2,n%5==0 ? 0x8000 : 0);
        } else {
            const unsigned masks[] = {0,0x4000,0x9000,0x800,0x400,0x200,0x100,0xffff};
            half(rdram,0x800a3a00+chosen*2,masks[n%8]);
            if (n%11==0) half(rdram,0x80098564,0);
            if (n%13==0) half(rdram,0x80300010,100); // enter name typing
            if (n%17==0) for(unsigned d=0;d<4;++d) half(rdram,0x80300010+d*2,-1);
            if (n%7==0) {
                half(rdram,0x80098562,n%2 ? -7 : 3);
                half(rdram,0x80098590,n%3 ? 0 : 100);
            }
        }
        expected=initial; actual=initial;
        for (unsigned step=0;step<3;++step) {
            recomp_context reference_ctx{}, guest_ctx{};
            reference_ctx.r29=guest_ctx.r29=address(0x807ff000);
            reference_ctx.f_odd=&reference_ctx.f0.u32h;
            guest_ctx.f_odd=&guest_ctx.f0.u32h;
            calls.clear(); samples=0;
            menu_reference(expected.data(),&reference_ctx);
            auto reference_calls=calls; unsigned reference_samples=samples;
            calls.clear(); samples=0;
            func_800427D4(actual.data(),&guest_ctx);
            if (calls!=reference_calls || samples!=reference_samples || !compare(expected,actual,count)) {
                std::cerr << "Menu parity failed case " << n << " step " << step
                    << " calls=" << calls.size() << '/' << reference_calls.size()
                    << " samples=" << samples << '/' << reference_samples << '\n';
                return 1;
            }
            ++count;
        }
    }
    std::cout << "PASS menu input ROM parity: " << count << " updates\n";
}
