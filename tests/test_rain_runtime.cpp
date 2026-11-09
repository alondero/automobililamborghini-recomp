#include "lambo_rain.h"
#include "lambo_mod_gameplay.h"
#include "recomp.h"
#include "rt64_extended_gbi.h"
#include "lambo_log.h"
#include "rain_reference.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <thread>
#include <vector>

static size_t allocations=0;
void* operator new(size_t size) {
    ++allocations;
    if (void* p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }

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

// Cached producers against the frozen e053389 producer (tests/rain_reference.h).
namespace cache {
uint8_t* rdram;
int32_t& w(uint32_t a) { return MEM_W(0,int32_t(a)); }
int16_t& h(uint32_t a) { return MEM_H(0,int32_t(a)); }
constexpr uint32_t settings_at=0x80000100, records=0x80300000;
// A grid of flat road faces at a per-record offset, loaded in 30-vertex batches.
void road_list(uint32_t list,uint32_t vertices,int offset,int height) {
    std::vector<lambo::rain::Triangle> faces;
    for (int i=0;i<4;++i) for (int j=0;j<4;++j) {
        float x=float(offset+i*1500),z=float(offset/3+j*1500),y=float(height+(i*7+j*3)%11);
        lambo::rain::Point a{x,y,z},b{x+1500,y,z},c{x,y+5,z+1500},d{x+1500,y+3,z+1500};
        faces.push_back({{a,b,c}}); faces.push_back({{b,d,c}});
    }
    using lambo::rain::Point;
    faces.push_back({{Point{0,0,0},Point{0,900,0},Point{900,0,0}}}); // wall
    uint32_t at=list;
    for (size_t first=0;first<faces.size();first+=10) {
        size_t n=std::min<size_t>(10,faces.size()-first);
        w(at)=int32_t(0x04000000u|(uint32_t(n*3)<<10)|uint32_t(n*3*16-1)); w(at+4)=int32_t(vertices); at+=8;
        for (size_t k=0;k<n;++k) {
            for (int v=0;v<3;++v) {
                auto p=faces[first+k].p[v]; uint32_t q=vertices+uint32_t(k*3+v)*16;
                h(q)=int16_t(p.x); h(q+2)=int16_t(p.y); h(q+4)=int16_t(p.z);
            }
            uint32_t a=uint32_t(k*3);
            w(at)=int32_t(0xBF000000u); w(at+4)=int32_t((a<<17)|((a+1)<<9)|((a+2)<<1)); at+=8;
        }
        vertices+=uint32_t(n*3*16);
    }
    w(at)=int32_t(0xB8000000u);
}
uint32_t record(int i) { return records+uint32_t(i)*64; }
void setup() {
    for (int i=0;i<4;++i) {
        uint32_t list=0x80310000+uint32_t(i)*0x4000;
        road_list(list,list+0x1000,i*700,0);
        w(record(i)+4)=int32_t(list); w(record(i)+8)=0; w(record(i)+12)=0;
    }
    // Record 3 sits under a roof: no puddles, and camera rain is sheltered.
    road_list(0x80330000,0x80331000,3*700,1200);
    w(record(3)+8)=int32_t(0x80330000);
    w(0x800BF1D0)=int32_t(records);h(0x800BF1CC)=0;
    h(0x800CE6AC)=8;h(0x800CE6B4)=2;h(0x800CE808)=0;h(0x800CE794)=0;
}
struct Frame { std::vector<uint32_t> roads,cameras; float seconds; size_t allocations; };
// One graphics task: every viewport draws `order`, then its camera rain.
Frame frame(int slot,const std::vector<int>& order,int players=1) {
    uint32_t task=0x800BF240+uint32_t(slot)*0x7A50,root=task+0x1C0;
    w(0x800A2BFC)=int32_t(task);w(0x800A39CC)=int32_t(root+16);
    for (int i=0;i<8;++i) {w(root+i*8)=int32_t(0xE7000000);w(root+i*8+4)=0;}
    w(root+56)=int32_t(0xB8000000);
    h(0x800CE6A4)=int16_t(players);
    size_t before=allocations;
    lambo_rain_frame(rdram);
    float seconds=lambo::rain::animation_seconds();
    for (int p=0;p<players;++p) {
        h(0x800CE6A6)=int16_t(p+1);
        for (int i:order) lambo_rain_road(rdram,record(i));
        lambo_rain_camera(rdram);
    }
    lambo_rain_publish(rdram,root,64);
    Frame out{{},{},seconds,allocations-before};
    if (uint32_t(w(root+16))!=0x06000000) return out;
    uint32_t copy=w(root+20);
    for (uint32_t at=copy;uint32_t(w(at))!=0xB8000000;at+=8) {
        if (uint32_t(w(at))!=0x06000000) continue;
        uint32_t target=w(at+4);
        // Road sublists follow their vertex block; camera lists start with E7.
        (uint32_t(w(target+8))==((RT64_EXTENDED_OPCODE<<24)|G_EX_PUSHOTHERMODE_V1) &&
            uint32_t(w(target+32))==((RT64_EXTENDED_OPCODE<<24)|G_EX_PUSHGEOMETRYMODE_V1)
            ? out.roads : out.cameras).push_back(target);
    }
    return out;
}
// The road sublist matches the uncached producer byte for byte.
bool matches(uint32_t target,int i,unsigned settings,float seconds) {
    uint32_t r=record(i),buffer=target-12000*16;
    std::vector<lambo::rain::Triangle> road;
    if (!reference::road_mesh(rdram,uint32_t(w(r+4)),road)) return false;
    auto vertices=reference::puddles(road,reference::roofs(rdram,uint32_t(w(r+8)),uint32_t(w(r+12))),
        (settings&3)==2,seconds,float((settings>>8)&127)/100);
    if (vertices.empty()) return false;
    auto e=reference::encode(vertices,buffer);
    for (size_t k=0;k<vertices.size();++k) {
        uint32_t v=buffer+uint32_t(k)*16;
        for (int f=0;f<4;++f) if (h(v+f*2)!=e.halves[k*4+f]) return false;
        if (w(v+8)!=0 || uint32_t(w(v+12))!=e.colors[k]) return false;
    }
    for (size_t k=0;k<e.commands.size();++k) if (uint32_t(w(target+uint32_t(k)*4))!=e.commands[k]) return false;
    return true;
}
void pause_tick() { std::this_thread::sleep_for(std::chrono::milliseconds(25)); }
void run(uint8_t* ram) {
    rdram=ram;
    using lambo::rain::counters;
    setup();
    const std::vector<int> order{0,1,2,3};
    for (unsigned style:{2u,1u}) {
        unsigned s=style|(80<<8)|0x10000;
        w(settings_at)=int32_t(s);
        lambo_rain_invalidate();
        auto start=counters();
        std::vector<Frame> frames;
        for (int f=0;f<6;++f) {
            frames.push_back(frame(f%2,order));
            require(frames.back().roads.size()==3 && frames.back().cameras.size()==1,
                "uncovered records draw puddles; covered record and camera follow shelter rules");
            for (int k=0;k<3;++k) require(matches(frames.back().roads[k],k,s,frames.back().seconds),
                "cached road sublist matches the uncached producer");
            pause_tick();
        }
        auto steady=counters();
        require(steady.builds-start.builds==4 && steady.decodes-start.decodes==12,
            "each record decodes its three lists once per epoch");
        require(steady.full_writes-start.full_writes==6,"each slot writes static geometry once");
        if (style==2) require(frames[2].seconds!=frames[4].seconds && steady.ripple_writes>start.ripple_writes,
            "modern ripples animate");
        else require(steady.ripple_writes==start.ripple_writes,"retro has no time-dependent writes");
        for (int f=0;f<4;++f) require(frame(f%2,order).allocations==0,"steady-state frames do not allocate");
        // Pause freezes the clock: ripples hold their current vertices.
        h(0x800CE808)=1;
        auto a=frame(0,order); pause_tick(); auto b=frame(0,order);
        require(a.seconds==b.seconds && matches(b.roads[1],1,s,b.seconds),"pause stops ripples");
        h(0x800CE808)=0;
        // A changed emission order rebinds task buffers to other records.
        auto shuffled=frame(1,{2,0,3,1});
        require(matches(shuffled.roads[0],2,s,shuffled.seconds) && matches(shuffled.roads[1],0,s,shuffled.seconds) &&
            matches(shuffled.roads[2],1,s,shuffled.seconds),
            "rebound buffers are rewritten for their new record");
        require(counters().builds==steady.builds,"reordering does not rebuild geometry");
        // Four viewports share one build per record and one sublist per task.
        auto quad=frame(0,order,4);
        require(quad.roads.size()==12 && quad.cameras.size()==4,"each viewport inserts rain");
        for (int v=1;v<4;++v) for (int k=0;k<3;++k) require(quad.roads[v*3+k]==quad.roads[k],"views share sublists");
        for (int k=0;k<3;++k) require(matches(quad.roads[k],k,s,quad.seconds),"four-player sublists match");
        require(counters().builds==steady.builds,"viewports do not rebuild geometry");
    }
    unsigned s=2|(80<<8);
    auto check=[&](const char* message) {
        auto f=frame(0,{0,1,2});
        require(f.roads.size()==3,message);
        for (int k=0;k<3;++k) require(matches(f.roads[k],k,s,f.seconds),message);
    };
    w(settings_at)=int32_t(s);
    check("settings change rebuilds");
    // A reused record whose road pointer changes must not serve stale puddles.
    auto built=counters().builds;
    w(record(1)+4)=int32_t(0x80340000); road_list(0x80340000,0x80341000,5000,40);
    check("changed road pointer serves the new geometry");
    require(counters().builds==built+1,"only the changed record rebuilds");
    // Same pointers, new contents: an explicit track or state load invalidates.
    road_list(0x80340000,0x80341000,9000,-70);
    lambo_rain_invalidate();
    check("invalidation regenerates in-place geometry");
    built=counters().builds;
    h(0x800CE794)=3; frame(0,{0,1,2});
    require(counters().builds==built+3,"circuit change starts a new epoch");
    h(0x800CE794)=0; check("returning to the circuit rebuilds");
    built=counters().builds;
    h(0x800CE6AC)=7; require(frame(0,{0,1,2}).roads.empty(),"loading emits nothing");
    h(0x800CE6AC)=8; check("race after a load rebuilds");
    require(counters().builds==built+3,"leaving the race clears the cache");
    // Shelter is decided from the cached camera segment.
    auto decodes=counters().decodes;
    h(0x800BF1CC)=3; require(frame(1,{3}).cameras.empty(),"covered camera segment has no screen rain");
    h(0x800BF1CC)=0; require(frame(1,{0}).cameras.size()==1,"open camera segment has screen rain");
    require(counters().decodes==decodes+3,"shelter reuses cached segments");
}
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
    cache::run(rdram);
}
