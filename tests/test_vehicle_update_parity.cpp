// Complete linked-object update parity against the independently generated ROM.
// Services are deterministic test doubles; this is not a physics/playthrough test.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include "recomp.h"
#include "guest_patch_data.inc"
#include "vehicle_reference/lambo_analog_throttle.h"
#include "vehicle_reference/lambo_analog_brake.h"
#include "vehicle_reference/lambo_steering.h"
#include "lambo_log.h"

extern "C" void func_80019D20(uint8_t*, recomp_context*);
extern "C" void func_80019D20_reference(uint8_t*, recomp_context*);
extern "C" float vehicle_reference_brake_ramp(unsigned);
extern "C" { volatile int g_log_threshold = -1; }
extern "C" void lambo_log_write(LamboLogLevel, const char*, const char*, ...) {}
static std::vector<std::array<uint32_t, 4>> calls;
static uint32_t samples[4][3]{};
static gpr addr(uint32_t a) { return (gpr)(int32_t)a; }
static void half(uint8_t* rdram, uint32_t a, int v) { MEM_H(0, addr(a)) = v; }
static void word(uint8_t* rdram, uint32_t a, uint32_t v) { MEM_W(0, addr(a)) = v; }
static void byte(uint8_t* rdram, uint32_t a, int v) { MEM_B(0, addr(a)) = v; }
static void floating(uint8_t* rdram, uint32_t a, float v) {
    uint32_t bits; std::memcpy(&bits, &v, 4); word(rdram, a, bits);
}
static uint32_t vehicle(uint8_t* rdram) {
    return 0x80910000u + int16_t(MEM_H(0, addr(0x80098398))) * 0x10c;
}
extern "C" int vehicle_reference_field_car(uint8_t* rdram, int index) {
    return index >= 0 && index < 64 && (MEM_HU(0, addr(0x80910000u + index * 0x10c)) & 8);
}
extern "C" void lambo_vehicle_input(uint8_t*, recomp_context* c) {
    c->r2 = (gpr)(int32_t)(c->r4 < 4 && c->r5 < 3 ? samples[c->r4][c->r5] : 0);
}
extern "C" void lambo_vehicle_observe(uint8_t*, recomp_context*) {}
extern "C" void switch_error(const char* name, uint32_t a, uint32_t v) {
    std::cerr << name << " invalid switch " << std::hex << a << ' ' << v << '\n'; std::abort();
}
static void service(int id, recomp_context* c, int argc = 0) {
    calls.push_back({uint32_t(id), argc > 0 ? uint32_t(c->r4) : 0,
                    argc > 1 ? uint32_t(c->r5) : 0, 0});
}
extern "C" void func_80014D50(uint8_t*, recomp_context* c) { service(1,c,2); }
extern "C" void func_80025834(uint8_t*, recomp_context* c) { service(2,c,1); }
extern "C" void func_8002622C(uint8_t*, recomp_context* c) { service(3,c); }
extern "C" void func_80028250(uint8_t*, recomp_context* c) { service(4,c); }
extern "C" void func_80029DCC(uint8_t* rdram, recomp_context* c) {
    service(5,c); const auto a=vehicle(rdram); float demand;
    const uint32_t bits=MEM_W(0,addr(a+0xa0)); std::memcpy(&demand,&bits,4);
    floating(rdram,a+0xa0,std::min(demand+1.f,16.f));
    half(rdram,a+0xac,MEM_HU(0,addr(a+0xac))|1);
}
extern "C" void func_8002A070(uint8_t* rdram, recomp_context* c) {
    service(6,c); const auto a=vehicle(rdram); floating(rdram,a+0xa0,0);
    half(rdram,a+0xac,MEM_HU(0,addr(a+0xac))&~1);
}
extern "C" void func_8002D37C(uint8_t*, recomp_context* c) { service(7,c); }
extern "C" void func_80037BC0(uint8_t*, recomp_context* c) { service(8,c,1); }
static void math_service(int id, recomp_context* c) {
    calls.push_back({uint32_t(id),c->f12.u32l,0,0});
    if(id==9) c->f0.fl=std::sqrt(c->f12.fl);
    else if(id==10) c->f0.fl=std::sin(c->f12.fl);
    else c->f0.fl=std::cos(c->f12.fl);
}
extern "C" void func_80075720(uint8_t*, recomp_context* c) { math_service(9,c); }
extern "C" void func_80075730(uint8_t*, recomp_context* c) { math_service(10,c); }
extern "C" void func_800758F0(uint8_t*, recomp_context* c) { math_service(11,c); }

static bool ignored(size_t offset) {
    return (offset >= 0x7fe000 && offset < 0x7ff020) ||
           (offset >= 0x901440 && offset < 0x901450);
}
static bool compare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int n) {
    if(std::memcmp(a.data(),b.data(),0x7fe000)==0 &&
       std::memcmp(a.data()+0x7ff020,b.data()+0x7ff020,0x901440-0x7ff020)==0 &&
       std::memcmp(a.data()+0x901450,b.data()+0x901450,a.size()-0x901450)==0) return true;
    for (size_t i=0;i<a.size();++i) if(a[i]!=b[i] && !ignored(i)) {
        std::cerr << "vehicle case " << std::dec << n << " RAM " << std::hex << 0x80000000u+i
                  << " reference=" << unsigned(a[i]) << " source=" << unsigned(b[i]) << '\n';
        uint32_t left,right; std::memcpy(&left,a.data()+(i&~size_t(3)),4);
        std::memcpy(&right,b.data()+(i&~size_t(3)),4);
        std::cerr << "word reference=" << left << " source=" << right << '\n';
        return false;
    }
    return true;
}
static recomp_context context(int first) {
    recomp_context c{}; c.r29=addr(0x807ff000); c.r4=(gpr)(int32_t)first; return c;
}
static void reset_reference(uint8_t* rdram) {
    lambo::reference_analog_brake::set_probe(true);
    for(unsigned p=0;p<4;++p) {
        lambo::reference_analog_brake::publish(p,false,0);
        half(rdram,0x800CE6AA,p+1); half(rdram,0x80098398,0);
        vehicle_reference_lambo_analog_brake_apply(rdram);
    }
    lambo::reference_analog_brake::set_probe(false);
}
int main(int argc, char** argv) {
    const char* path=argc>1?argv[1]:"Automobili Lamborghini (USA).z64";
    std::ifstream stream(path,std::ios::binary);
    if(!stream) { std::cerr << "Cannot read ROM: " << path << '\n'; return 2; }
    const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(stream),{}};
    if(rom.size()<0x100000) return 2;
    std::vector<uint8_t> base(0x1000000);
    for(size_t i=0x1000;i<std::min(rom.size(),size_t(0x90000));++i)
        byte(base.data(),0x80000400u+i-0x1000,rom[i]);
    for(size_t i=0;i<sizeof(guest_patch_data);++i) byte(base.data(),0x80801000u+i,guest_patch_data[i]);
    unsigned count=0;
    // All dispatch values, input schemes, difficulty, live settings and service stages.
    for(int channel=0;channel<=15;++channel) for(int variant=0;variant<48;++variant) {
        const bool toggle=variant>=40 && variant<44;
        auto initial=base; auto* rdram=initial.data(); reset_reference(rdram);
        half(rdram,0x800CE6A4,1+variant%4); half(rdram,0x800CE6B4,toggle?2:1+variant%4);
        half(rdram,0x800CE6B0,variant%8==0?2:3); half(rdram,0x800CE79C,variant%2);
        half(rdram,0x800CE7A8,(variant/2)%2); half(rdram,0x800B6758,0);
        half(rdram,0x800B675A,-1); word(rdram,0x800BF1C4,0x80100000);
        floating(rdram,0x800A2BF0,1e9f);
        const unsigned buttons[]={0,0x8000,0x2000,0x4000,0xe000,0x0010};
        const int sticks[]={-80,-20,-4,0,4,20,80};
        const int deadzones[]={4,0,10,20};
        const bool direct=(variant/4)&1;
        for(unsigned p=0;p<4;++p) {
            half(rdram,0x800A39E0+p*6,toggle?0:buttons[variant%6]);
            byte(rdram,0x800A39E2+p*6,sticks[variant%7]);
            byte(rdram,0x800A39E3+p*6,sticks[(variant+3)%7]);
            half(rdram,0x800A3A0Eu+(p+1)*2,variant%3);
            const uint16_t normalized=uint16_t((variant%5)*65535/4);
            const bool analog=variant%4!=0;
            samples[p][0]=samples[p][1]=(analog?0x80000000u:0)|normalized;
            samples[p][2]=unsigned(deadzones[variant%4])|(direct?256u:0u);
            lambo::reference_analog_throttle::publish(p,analog,float(normalized)/65535.f);
            lambo::reference_analog_brake::publish(p,analog,float(normalized)/65535.f);
        }
        lambo::reference_steering::publish({direct,deadzones[variant%4]});
        const int records=variant%2?3:1;
        for(int i=0;i<records;++i) {
            const uint32_t a=0x80910000+i*0x10c;
            half(rdram,a,(channel>=13 && variant%4==3)?8:0);
            half(rdram,a+6,i+1<records?i+1:-1); half(rdram,a+0xe,channel);
            half(rdram,a+0x12,0); half(rdram,a+0xaa,variant*3);
            half(rdram,a+0xb4,variant-24);
            word(rdram,a+0x90,variant%8==1?-137:variant*137);
            floating(rdram,a+0x14,float(i*100)); floating(rdram,a+0x18,10);
            floating(rdram,a+0x1c,float(variant)); floating(rdram,a+0x68,1);
        }
        for(int ch=0;ch<17;++ch) {
            const uint32_t a=0x80902000+ch*0x84;
            const int limits[]={-1,0,10,100,32767};
            half(rdram,a+0x44,limits[variant%5]); half(rdram,a+0x50,toggle?0:(variant/12)%4);
            half(rdram,a+0x02,!toggle && variant>=36?variant%3:0);
            half(rdram,a+0x24,0); half(rdram,a+0x52,variant%3);
        }
        auto reference=initial, source=initial;
        const int first=channel==0 && variant==0?-1:0;
        // Repeated updates exercise the host and guest ramp persistence.
        for(int tick=0;tick<3;++tick) {
            // Disable and restore analog input with/without diagnostics. The
            // original digital no-probe path intentionally retains its ramp.
            const bool probe=toggle && (variant&1);
            lambo::reference_analog_throttle::set_probe(probe);
            lambo::reference_analog_brake::set_probe(probe);
            for(unsigned p=0;p<4;++p) {
                const uint16_t normalized=uint16_t((variant%5)*65535/4);
                const bool analog=variant%4!=0 && !(toggle && tick==1);
                samples[p][0]=samples[p][1]=(analog?0x80000000u:0)|(probe?0x40000000u:0)|normalized;
                lambo::reference_analog_throttle::publish(p,analog,float(normalized)/65535.f);
                lambo::reference_analog_brake::publish(p,analog,float(normalized)/65535.f);
            }
            const bool restore_check=channel==1 && variant==1 && tick==1;
            const auto saved=restore_check?source:std::vector<uint8_t>{};
            if(std::getenv("LAMBO_VEHICLE_PARITY_TRACE"))
                std::cerr << "case " << count << " channel " << channel << " variant " << variant << " tick " << tick << '\n';
            calls.clear(); auto c=context(first); c.f_odd=&c.f0.u32h;
            func_80019D20_reference(reference.data(),&c);
            const auto expected=calls;
            if(std::getenv("LAMBO_VEHICLE_PARITY_TRACE")) std::cerr << "reference returned\n";
            calls.clear(); c=context(first); c.f_odd=&c.f0.u32h; func_80019D20(source.data(),&c);
            if(calls!=expected) { std::cerr << "vehicle service mismatch case " << count << '\n'; return 1; }
            if(!compare(reference,source,count)) return 1;
            for(unsigned p=0;p<4;++p) {
                const float ramp=vehicle_reference_brake_ramp(p); uint32_t bits;
                std::memcpy(&bits,&ramp,4); auto* rdram=source.data();
                if(uint32_t(MEM_W(0,addr(0x80901440+p*4)))!=bits) {
                    std::cerr << "vehicle brake ramp mismatch case " << count << " port " << p << '\n';
                    return 1;
                }
            }
            if(restore_check) {
                const auto after=source;
                const auto after_calls=calls;
                if(std::memcmp(saved.data()+0x901440,after.data()+0x901440,4)==0) {
                    std::cerr << "snapshot fixture did not advance the guest brake ramp\n"; return 1;
                }
                source=saved; calls.clear(); c=context(first); c.f_odd=&c.f0.u32h;
                func_80019D20(source.data(),&c);
                if(calls!=after_calls || !compare(after,source,count) ||
                   std::memcmp(after.data()+0x901440,source.data()+0x901440,16)!=0) {
                    std::cerr << "vehicle guest ramp snapshot replay failed\n"; return 1;
                }
            }
            ++count;
        }
    }
    std::cout << "Vehicle update parity: " << count << " cases; guest ramp snapshot replay passed\n";
}
