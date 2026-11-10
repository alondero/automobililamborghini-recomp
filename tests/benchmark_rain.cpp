// Opt-in CPU and allocation measurement of the rain producers (#397).
// Synthetic track geometry; no game data, renderer or frame-time claim.
//
// Isolated functions compare the frozen e053389 implementation
// (tests/rain_reference.h) with the cached path. The producer section uses
// only the extern "C" hooks, so building this file with
// -DLAMBO_RAIN_BENCH_PRODUCERS_ONLY against an older lambo_rain*.cpp measures
// that revision with the same workload.
#include "lambo_rain.h"
#include "lambo_mod_gameplay.h"
#include "lambo_log.h"
#include "recomp.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
#ifndef LAMBO_RAIN_BENCH_PRODUCERS_ONLY
#include "rain_reference.h"
#endif

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
    if (cursor+size>0x10000000) return nullptr;
    void* result=ram+cursor; cursor+=size; return result;
}
void free(uint8_t*,void*) {} // the bump heap never reuses space
}

namespace {
using lambo::rain::Point;
using lambo::rain::Triangle;
using Clock=std::chrono::steady_clock;
uint8_t* rdram;
int32_t& w(uint32_t a) { return MEM_W(0,int32_t(a)); }
int16_t& h(uint32_t a) { return MEM_H(0,int32_t(a)); }
double since(Clock::time_point start) {
    return std::chrono::duration<double,std::micro>(Clock::now()-start).count();
}
// Flat road cells with a gentle crown, two faces per cell.
std::vector<Triangle> faces(unsigned count,int offset,int height) {
    std::vector<Triangle> out;
    for (unsigned i=0;out.size()<count;++i) {
        float x=float(offset+int(i%10)*700),z=float(offset/2+int(i/10)*700),y=float(height+int(i*7%13));
        Point a{x,y,z},b{x+700,y+4,z},c{x,y+2,z+700},d{x+700,y+6,z+700};
        out.push_back({{a,b,c}});
        if (out.size()<count) out.push_back({{b,d,c}});
    }
    return out;
}
void write_list(uint32_t list,uint32_t vertices,const std::vector<Triangle>& t) {
    uint32_t at=list;
    for (size_t first=0;first<t.size();first+=10) {
        size_t n=std::min<size_t>(10,t.size()-first);
        w(at)=int32_t(0x04000000u|(uint32_t(n*3)<<10)|uint32_t(n*3*16-1)); w(at+4)=int32_t(vertices); at+=8;
        for (size_t k=0;k<n;++k) {
            for (int v=0;v<3;++v) {
                auto p=t[first+k].p[v]; uint32_t q=vertices+uint32_t(k*3+v)*16;
                h(q)=int16_t(p.x); h(q+2)=int16_t(p.y); h(q+4)=int16_t(p.z);
            }
            uint32_t a=uint32_t(k*3);
            w(at)=int32_t(0xBF000000u); w(at+4)=int32_t((a<<17)|((a+1)<<9)|((a+2)<<1)); at+=8;
        }
        vertices+=uint32_t(n*3*16);
    }
    w(at)=int32_t(0xB8000000u);
}
constexpr uint32_t records=0x80300000;
// Each record: a road list, and a wall list whose raised floor faces roof a
// quarter of the road.
void track(unsigned segments,unsigned per_segment) {
    for (unsigned i=0;i<segments;++i) {
        uint32_t road=0x80320000+i*0x8000,wall=road+0x6000;
        write_list(road,road+0x1000,faces(per_segment,int(i)*9000,0));
        auto roof=faces(per_segment/4,int(i)*9000,1500);
        roof.push_back({{Point{0,0,0},Point{0,900,0},Point{900,0,0}}});
        write_list(wall,wall+0x400,roof);
        w(records+i*64+4)=int32_t(road); w(records+i*64+8)=int32_t(wall); w(records+i*64+12)=0;
    }
}
struct Cost { double cold_us,steady_us; double cold_allocations,steady_allocations,road_kib; };
Cost producers(unsigned segments,unsigned per_segment,unsigned style,int players) {
    track(segments,per_segment);
    w(0x80000100)=int32_t(style|(80<<8)|0x10000);
    h(0x800CE6B4)=2;h(0x800CE808)=0;h(0x800CE794)=0;h(0x800CE6A4)=int16_t(players);
    w(0x800BF1D0)=int32_t(records);h(0x800BF1CC)=0;
    auto frame=[&](int slot) {
        uint32_t task=0x800BF240+uint32_t(slot)*0x7A50,root=task+0x1C0;
        w(0x800A2BFC)=int32_t(task);w(0x800A39CC)=int32_t(root+16);
        for (int i=0;i<8;++i) {w(root+i*8)=int32_t(0xE7000000);w(root+i*8+4)=0;}
        w(root+56)=int32_t(0xB8000000);
        lambo_rain_frame(rdram);
        for (int p=0;p<players;++p) {
            h(0x800CE6A6)=int16_t(p+1);
            for (unsigned i=0;i<segments;++i) lambo_rain_road(rdram,records+i*64);
            lambo_rain_camera(rdram);
        }
        lambo_rain_publish(rdram,root,64);
    };
    // A non-race frame ends the previous epoch (ignored by older revisions).
    h(0x800CE6AC)=7; frame(0); frame(1); h(0x800CE6AC)=8;
    Cost cost{};
    size_t before=allocations; auto start=Clock::now();
    frame(0); frame(1);
    cost.cold_us=since(start)/2; cost.cold_allocations=double(allocations-before)/2;
    constexpr int frames=200;
    before=allocations; start=Clock::now();
    for (int f=0;f<frames;++f) frame(f%2);
    cost.steady_us=since(start)/frames; cost.steady_allocations=double(allocations-before)/frames;
#ifndef LAMBO_RAIN_BENCH_PRODUCERS_ONLY
    cost.road_kib=double(lambo::rain::counters().road_bytes)/1024; // both task slots, live
#endif
    return cost;
}
#ifndef LAMBO_RAIN_BENCH_PRODUCERS_ONLY
template<class F> double each(int repeats,F f) {
    auto start=Clock::now();
    for (int i=0;i<repeats;++i) f();
    return since(start)/repeats;
}
volatile size_t sink;
void isolated() {
    std::printf("isolated function                         e053389_us   cached_us\n");
    for (unsigned n:{36u,120u}) {
        write_list(0x80320000,0x80321000,faces(n,0,0));
        std::vector<Triangle> reused;
        double before=each(2000,[&]{ std::vector<Triangle> t; reference::road_mesh(rdram,0x80320000,t); sink=t.size(); });
        double after=each(2000,[&]{ lambo::rain::road_mesh(rdram,0x80320000,reused); sink=reused.size(); });
        std::printf("road_mesh %3u faces                       %10.2f  %10.2f\n",n,before,after);
    }
    for (unsigned n:{100u,300u}) {
        auto road=faces(n,0,0),roof=faces(n,0,1500);
        std::vector<lambo::rain::RoofPlane> planes; lambo::rain::roof_planes(roof,planes);
        double before=each(20,[&]{ size_t c=0; for (auto& t:road) c+=reference::covered(t.p[0],roof); sink=c; });
        double after=each(20,[&]{ size_t c=0; for (auto& t:road) c+=lambo::rain::covered(t.p[0],planes); sink=c; });
        std::printf("covered %3u road x %3u roof               %10.2f  %10.2f\n",n,n,before,after);
    }
    for (unsigned n:{36u,120u}) for (bool modern:{false,true}) {
        auto road=faces(n,0,0);
        lambo::rain::PuddleMesh mesh;
        lambo::rain::puddle_mesh(road,{},modern,0,0.8f,mesh);
        float t=0;
        double before=each(500,[&]{ sink=reference::puddles(road,{},modern,t+=0.016f,0.8f).size(); });
        // Per-frame work once cached: Modern rewrites ripple vertices only.
        double after=each(500,[&]{
            t+=0.016f;
            for (const auto& p:mesh.ripples) for (int side=0;side<lambo::rain::ripple_sides;++side)
                lambo::rain::ripple(p,side,lambo::rain::ripple_phase(p,t),
                    &mesh.vertices[lambo::rain::ripple_vertex(p,side)]);
            sink=mesh.vertices.size();
        });
        std::printf("puddles %3u faces %-6s (%6zu B)         %10.2f  %10.2f\n",n,modern?"Modern":"Retro",
            mesh.vertices.size()*16,before,after);
    }
}
#endif
}

int main() {
    std::vector<uint8_t> memory(0x10000000);
    rdram=memory.data();
    recomp_context ctx{}; ctx.r4=int32_t(0x80000100);
    lambo_register_rain_v1(rdram,&ctx);
    h(0x800CE6AC)=8; h(0x800CE6B4)=2;
    std::printf("sky first tile and remaining 15 tiles (microseconds)\n");
    for (unsigned style:{1u,2u}) for (int circuit=0;circuit<6;++circuit) {
        w(0x80000100)=int32_t(style|(80<<8)); h(0x800CE794)=int16_t(circuit);
        lambo_rain_frame(rdram);
        auto start=Clock::now();
        volatile auto first=lambo_rain_sky(rdram,0,0,0);
        const double first_us=since(start);
        start=Clock::now();
        for (int tile=1;tile<16;++tile) first=lambo_rain_sky(rdram,tile%8,tile/8,0);
        std::printf("sky %-6s circuit=%d first_us=%.1f rest_us=%.1f\n",
            style==2?"Modern":"Retro",circuit+1,first_us,since(start));
    }
#ifndef LAMBO_RAIN_BENCH_PRODUCERS_ONLY
    isolated();
#endif
    std::printf("\nproducers per task (road+camera+publish)  cold_us  steady_us  cold_allocs  steady_allocs  road_heap_KiB\n");
    for (unsigned per:{36u,120u}) for (unsigned style:{1u,2u}) for (int players:{1,4}) {
        auto c=producers(25,per,style,players);
        std::printf("25 x %3u faces %-6s %dP              %9.1f  %9.1f  %11.1f  %13.1f  %13.1f\n",per,
            style==2?"Modern":"Retro",players,c.cold_us,c.steady_us,c.cold_allocations,c.steady_allocations,c.road_kib);
    }
}
