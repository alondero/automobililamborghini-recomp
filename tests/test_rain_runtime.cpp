#include "lambo_rain.h"
#include "lambo_mod_gameplay.h"
#include "recomp.h"
#include "rt64_extended_gbi.h"
#include "lambo_log.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

extern "C" float lambo_sky_target_aspect() { return 16.0f/9; }
volatile int g_log_threshold=LAMBO_LEVEL_WARN;
void lambo_log_write(LamboLogLevel,const char*,const char*,...) {}
namespace recomp {
void* alloc(uint8_t* ram,size_t size) {
    static size_t cursor=0x1000000;
    size=(size+15)&~size_t(15);
    if (cursor+size>0x8000000) return nullptr;
    void* result=ram+cursor; cursor+=size; return result;
}
}
void require(bool good,const char* message) {
    if (!good) { std::cerr<<message<<'\n'; std::exit(1); }
}
int main() {
    std::vector<uint8_t> memory(0x8000000);
    uint8_t* rdram=memory.data();
    auto w=[&](uint32_t a)->int32_t& {return MEM_W(0,int32_t(a));};
    auto h=[&](uint32_t a)->int16_t& {return MEM_H(0,int32_t(a));};
    recomp_context ctx{};ctx.r4=int32_t(0x80000100);
    lambo_register_rain_v1(rdram,&ctx);
    w(0x80000100)=2|(70<<8)|0x10000;
    h(0x800CE6AC)=8;h(0x800CE6B4)=2;h(0x800CE6A4)=1;h(0x800CE6A6)=1;
    w(0x800BF1D0)=int32_t(0x80200000);h(0x800BF1CC)=0;
    w(0x80200004)=int32_t(0x80210000);
    w(0x80210000)=0x04000C2F;w(0x80210004)=int32_t(0x80211000);
    w(0x80210008)=int32_t(0xBF000000);w(0x8021000C)=0x00000204;
    w(0x80210010)=int32_t(0xB8000000);
    h(0x80211010)=2000;h(0x80211024)=2000;
    uint32_t copies[2]{};
    for (int slot=0;slot<2;++slot) {
        uint32_t task=0x800BF240+slot*0x7A50,root=task+0x1C0;
        w(0x800A2BFC)=int32_t(task);w(0x800A39CC)=int32_t(root+16);
        for (int i=0;i<8;++i) {w(root+i*8)=int32_t(0xE7000000);w(root+i*8+4)=0;}
        w(root+56)=int32_t(0xB8000000);
        lambo_rain_frame(rdram);
        auto before=std::vector<uint8_t>(memory.begin(),memory.begin()+0x800000);
        lambo_rain_road(rdram,0x80200001);
        require(std::memcmp(before.data(),memory.data(),before.size())==0,"unaligned records leave native RAM untouched");
        lambo_rain_road(rdram,0x80200000);
        lambo_rain_camera(rdram);
        require(std::memcmp(before.data(),memory.data(),before.size())==0,"producers consume no native task space");
        lambo_rain_publish(rdram,root,0xFFFF);
        require(std::memcmp(before.data(),memory.data(),before.size())==0,"invalid publication size fails closed");
        lambo_rain_publish(rdram,root,64);
        require(uint32_t(w(root+16))==0x06000000 && uint32_t(w(root+32))==0xB8000000,"native root becomes a bounded call/end wrapper");
        require(uint32_t(w(0x800A39CC))==root+16,"publication preserves the builder cursor");
        uint32_t copy=w(root+20);copies[slot]=copy;
        require(copy>=0x81000000 && copy<0x88000000,"copied root uses extended-address heap");
        require(uint32_t(w(copy+8))==0xE7000000 && uint32_t(w(copy+16))==0xE7000000,"native prefix survives in copied root");
        require(uint32_t(w(copy+32))==0x06000000,"road is inserted at its recorded boundary");
        uint32_t road=w(copy+36);
        int geometry=0,other=0,combine=0,prim=0,triangles=0;
        bool fog=false;
        for (unsigned i=0;i<20000;++i) {
            uint32_t a=w(road+i*8),b=w(road+i*8+4);
            if (a==0xB8000000) break;
            if (a==0xB7000000) fog=(b&0x10000)!=0;
            if (a==0xBF000000) {++triangles;require(b==((32u<<17)|(33u<<9)|(34u<<1)),"water preserves native vertex cache");}
            if ((a>>24)!=RT64_EXTENDED_OPCODE) continue;
            switch(a&0xFFFFFF) {
            case G_EX_PUSHGEOMETRYMODE_V1:++geometry;break;case G_EX_POPGEOMETRYMODE_V1:--geometry;break;
            case G_EX_PUSHOTHERMODE_V1:++other;break;case G_EX_POPOTHERMODE_V1:--other;break;
            case G_EX_PUSHCOMBINE_V1:++combine;break;case G_EX_POPCOMBINE_V1:--combine;break;
            case G_EX_PUSHPRIMCOLOR_V1:++prim;break;case G_EX_POPPRIMCOLOR_V1:--prim;break;
            }
            require(geometry>=0 && other>=0 && combine>=0 && prim>=0,"render state cannot underflow");
        }
        require(!geometry && !other && !combine && !prim && triangles>0 && fog,"puddles restore state and use native fog");
        require(uint32_t(w(copy+56))==0x06000000,"camera has its own insertion after the road");
        uint32_t camera=w(copy+60);
        int scissor=0,rectangles=0;
        for (unsigned i=0;i<5000;++i) {
            uint32_t a=w(camera+i*8);
            if (a==0xB8000000) break;
            if ((a>>24)!=RT64_EXTENDED_OPCODE) continue;
            switch(a&0xFFFFFF) {
            case G_EX_PUSHSCISSOR_V1:++scissor;break;case G_EX_POPSCISSOR_V1:--scissor;break;
            case G_EX_PUSHOTHERMODE_V1:++other;break;case G_EX_POPOTHERMODE_V1:--other;break;
            case G_EX_PUSHCOMBINE_V1:++combine;break;case G_EX_POPCOMBINE_V1:--combine;break;
            case G_EX_PUSHPRIMCOLOR_V1:++prim;break;case G_EX_POPPRIMCOLOR_V1:--prim;break;
            case G_EX_FILLRECT_V1:++rectangles;++i;break;
            case G_EX_SETSCISSOR_V1:++i;break;
            }
            require(scissor>=0 && other>=0 && combine>=0 && prim>=0,"camera cannot underflow state stacks");
        }
        require(rectangles>0 && !scissor && !other && !combine && !prim,"camera restores every changed stack");
        auto published=std::vector<uint8_t>(memory.begin(),memory.begin()+0x800000);
        lambo_rain_publish(rdram,root,64);
        require(std::memcmp(published.data(),memory.data(),published.size())==0,"publication consumes insertions exactly once");
    }
    require(copies[0]!=copies[1],"in-flight task slots have independent root storage");
    auto before=memory;
    w(0x80000100)=70<<8;
    require(lambo_rain_wet(rdram) && !lambo_rain_active(rdram),"Wet + visual Off preserves the weather fog policy");
    w(0x80000100)=0;
    lambo_rain_frame(rdram);lambo_rain_road(rdram,0x80200000);lambo_rain_camera(rdram);
    lambo_rain_publish(rdram,0x800C6E50,64);
    std::memcpy(before.data()+0x100,memory.data()+0x100,4);
    require(before==memory,"Off renders no effects or writes");
}
