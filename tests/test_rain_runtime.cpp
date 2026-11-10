#include "lambo_rain.h"
#include "lambo_render_aspect.h"
#include "lambo_mod_gameplay.h"
#include "recomp.h"
#include "rt64_extended_gbi.h"
#include "lambo_log.h"
#include "lambo_vehicle.h"
#include "rain_reference.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
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

static float target_aspect=16.0f/9;
static unsigned aspect_reads=0,aspect_calls=0;
extern "C" float lambo_sky_target_aspect() {
    ++aspect_calls;
    return lambo::render::frame_aspects.get([] {
        ++aspect_reads;
        return lambo::render::Aspects{target_aspect,4.0f/3};
    }).sky;
}
volatile int g_log_threshold=LAMBO_LEVEL_WARN;
void lambo_log_write(LamboLogLevel,const char*,const char*,...) {}
void require(bool good,const char* message) {
    if (!good) { std::cerr<<message<<'\n'; std::exit(1); }
}
// Guest heap stand-in. Blocks never move or get reused, so a stale pointer can
// never look valid, and 64 canary bytes follow each request to catch overruns.
namespace heap {
struct Block { size_t size; bool live; };
std::map<uint8_t*,Block> blocks;
size_t cursor=0x1000000,frees=0;
bool refuse=false; // simulate an exhausted heap
constexpr size_t guard=64;
size_t live_bytes() {
    size_t total=0;
    for (auto& [p,b]:blocks) if (b.live) total+=b.size;
    return total;
}
size_t live_blocks() {
    size_t count=0;
    for (auto& [p,b]:blocks) count+=b.live;
    return count;
}
void check_canaries() {
    for (auto& [p,b]:blocks) {
        if (!b.live) continue;
        size_t end=((b.size+15)&~size_t(15))+guard;
        for (size_t i=b.size;i<end;++i) require(p[i]==0xA5,"allocation overran its requested size");
    }
}
}
namespace recomp {
void* alloc(uint8_t* ram,size_t size) {
    size_t rounded=(size+15)&~size_t(15);
    if (heap::refuse || heap::cursor+rounded+heap::guard>0x8000000) return nullptr;
    uint8_t* result=ram+heap::cursor;
    std::memset(result+size,0xA5,rounded-size+heap::guard);
    heap::cursor+=rounded+heap::guard;
    heap::blocks[result]={size,true};
    return result;
}
void free(uint8_t*,void* mem) {
    auto found=heap::blocks.find(static_cast<uint8_t*>(mem));
    require(found!=heap::blocks.end() && found->second.live,"free of a block that is not live");
    found->second.live=false; ++heap::frees;
}
}

// Cached producers against the frozen e053389 producer (tests/rain_reference.h).
namespace cache {
uint8_t* rdram;
int32_t& w(uint32_t a) { return MEM_W(0,int32_t(a)); }
int16_t& h(uint32_t a) { return MEM_H(0,int32_t(a)); }
constexpr uint32_t settings_at=0x80000100, records=0x80300000;
// A grid of flat road faces at a per-record offset, loaded in 30-vertex batches.
void write_list(uint32_t list,uint32_t vertices,const std::vector<lambo::rain::Triangle>& faces) {
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
void road_list(uint32_t list,uint32_t vertices,int offset,int height) {
    std::vector<lambo::rain::Triangle> faces;
    for (int i=0;i<4;++i) for (int j=0;j<4;++j) {
        float x=float(offset+i*1500),z=float(offset/3+j*1500),y=float(height+(i*7+j*3)%11);
        lambo::rain::Point a{x,y,z},b{x+1500,y,z},c{x,y+5,z+1500},d{x+1500,y+3,z+1500};
        faces.push_back({{a,b,c}}); faces.push_back({{b,d,c}});
    }
    using lambo::rain::Point;
    faces.push_back({{Point{0,0,0},Point{0,900,0},Point{900,0,0}}}); // wall
    write_list(list,vertices,faces);
}
// The decoder's 2048-triangle limit, with faces large enough to hold puddles:
// the biggest mesh a road list can produce.
void dense_road_list(uint32_t list,uint32_t vertices) {
    std::vector<lambo::rain::Triangle> faces;
    for (int i=0;faces.size()<2048;++i) {
        float x=float(i%32*500-8000),z=float(i/32*500-8000);
        using lambo::rain::Point;
        Point a{x,0,z},b{x+500,0,z},c{x,0,z+500},d{x+500,0,z+500};
        faces.push_back({{a,b,c}});
        faces.push_back({{b,d,c}});
    }
    write_list(list,vertices,faces);
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
    unsigned reads_before=aspect_reads;
    lambo_rain_frame(rdram);
    float seconds=lambo::rain::animation_seconds();
    for (int p=0;p<players;++p) {
        h(0x800CE6A6)=int16_t(p+1);
        for (int i:order) lambo_rain_road(rdram,record(i));
        lambo_rain_camera(rdram);
    }
    lambo_rain_publish(rdram,root,64);
    require(aspect_reads==reads_before+(h(0x800CE6AC)==8 ? 1u : 0u),
        "all Wet rain hooks share one aspect read per task build");
    Frame out{{},{},seconds,allocations-before};
    // The counters must account for every guest heap block, and none may overrun.
    heap::check_canaries();
    require(lambo::rain::counters().heap_bytes==heap::live_bytes(),"heap counter matches live guest blocks");
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
std::vector<lambo::rain::Vertex> reference_mesh(int i,unsigned settings,float seconds) {
    uint32_t r=record(i);
    std::vector<lambo::rain::Triangle> road;
    if (!reference::road_mesh(rdram,uint32_t(w(r+4)),road)) return {};
    return reference::puddles(road,reference::roofs(rdram,uint32_t(w(r+8)),uint32_t(w(r+12))),
        (settings&3)==2,seconds,float((settings>>8)&127)/100);
}
// Bytes a sublist needs: the vertex block, then every command word pair.
size_t payload(int i,unsigned settings,float seconds) {
    auto vertices=reference_mesh(i,settings,seconds);
    return vertices.empty() ? 0 : vertices.size()*16+reference::encode(vertices,0).commands.size()*4;
}
bool matches(uint32_t target,int i,unsigned settings,float seconds) {
    auto vertices=reference_mesh(i,settings,seconds);
    if (vertices.empty()) return false;
    // The commands follow the vertex block directly: nothing is reserved past the mesh.
    uint32_t buffer=target-uint32_t(vertices.size())*16;
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
    // Disabled visuals and zero intensity never reach the renderer, including
    // when there is no presenter. Exercise road, camera and every sky slice.
    for (unsigned disabled:{0u,80u<<8,2u}) {
        w(settings_at)=int32_t(disabled);
        unsigned before=aspect_reads,calls_before=aspect_calls;
        lambo_rain_frame(rdram);
        for (int i=0;i<32;++i) {
            lambo_rain_road(rdram,record(i%4));
            lambo_rain_camera(rdram);
            require(lambo_rain_sky(rdram,i%8,i%2,123)==123,"disabled rain preserves sky texture");
        }
        require(aspect_reads==before && aspect_calls==calls_before,
            "dry and visual Off rain do not query renderer aspect");
    }
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
        // Each drawn record owns one exactly sized buffer per slot; the roofed record owns none.
        size_t expected=0;
        for (int k=0;k<3;++k) expected+=payload(k,s,frames.back().seconds);
        require(steady.road_buffers[0]==3 && steady.road_buffers[1]==3,"one buffer per drawn record per slot");
        require(steady.road_bytes==2*expected && expected>0,"road buffers are sized to the mesh and nothing more");
        require(steady.peak_road_bytes>=steady.road_bytes && steady.peak_heap_bytes>=steady.heap_bytes,
            "peaks bound the live totals");
        if (style==2) require(frames[2].seconds!=frames[4].seconds && steady.ripple_writes>start.ripple_writes,
            "modern ripples animate");
        else require(steady.ripple_writes==start.ripple_writes,"retro has no time-dependent writes");
        for (int f=0;f<4;++f) require(frame(f%2,order).allocations==0,"steady-state frames do not allocate");
        // Pause freezes the clock: ripples hold their current vertices.
        h(0x800CE808)=1;
        auto a=frame(0,order); pause_tick(); auto b=frame(0,order);
        require(a.seconds==b.seconds && matches(b.roads[1],1,s,b.seconds),"pause stops ripples");
        h(0x800CE808)=0;
        // Buffers belong to records, so a changed emission order moves nothing.
        auto writes=counters().full_writes;
        auto shuffled=frame(1,{2,0,3,1});
        require(matches(shuffled.roads[0],2,s,shuffled.seconds) && matches(shuffled.roads[1],0,s,shuffled.seconds) &&
            matches(shuffled.roads[2],1,s,shuffled.seconds),"reordered sublists match their records");
        require(shuffled.roads[0]==frames[5].roads[2] && shuffled.roads[1]==frames[5].roads[0] &&
            shuffled.roads[2]==frames[5].roads[1],"each record keeps its buffer when the order changes");
        require(counters().full_writes==writes,"reordering rewrites no geometry");
        require(counters().builds==steady.builds,"reordering does not rebuild geometry");
        // Four viewports share one build per record and one sublist per task.
        unsigned quad_reads=aspect_reads,quad_calls=aspect_calls;
        auto quad=frame(0,order,4);
        require(aspect_calls==quad_calls+21,"each camera reuses its settings aspect query");
        std::cout<<"rain aspect queries: style="<<style<<" dry=0 four_view_wet="
                 <<aspect_reads-quad_reads<<" snapshot / "<<aspect_calls-quad_calls<<" helpers\n";
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
    require(frame(1,{0,1,2}).roads.empty(),"loading emits nothing in either slot");
    // Each slot frees its own buffers once its previous task has finished.
    require(counters().road_bytes==0 && counters().road_buffers[0]==0 && counters().road_buffers[1]==0,
        "leaving the race returns every road buffer to the heap");
    h(0x800CE6AC)=8; check("race after a load rebuilds");
    require(counters().builds==built+3,"leaving the race clears the cache");
    // Shelter is decided from the cached camera segment.
    auto decodes=counters().decodes;
    h(0x800BF1CC)=3; require(frame(1,{3}).cameras.empty(),"covered camera segment has no screen rain");
    h(0x800BF1CC)=0; require(frame(1,{0}).cameras.size()==1,"open camera segment has screen rain");
    require(counters().decodes==decodes+3,"shelter reuses cached segments");

    // A mesh at the 12,000-vertex limit fits its exact buffer (canaries guard the end).
    dense_road_list(0x80380000,0x80390000);
    w(record(4)+4)=int32_t(0x80380000); w(record(4)+8)=0; w(record(4)+12)=0;
    {
        auto f=frame(0,{0,1,2,4});
        require(f.roads.size()==4,"the full-size mesh draws beside the others");
        require(reference_mesh(4,s,f.seconds).size()==12000,"the dense road reaches the vertex limit");
        require(matches(f.roads[3],4,s,f.seconds),"the full-size mesh is intact");
        for (int k=0;k<3;++k) require(matches(f.roads[k],k,s,f.seconds),"neighbours of the full-size mesh are intact");
    }
    // A rebuilt record that needs more room moves to a new block and frees the old one.
    auto freed=heap::frees;
    auto roads=counters().road_buffers[0];
    w(record(1)+4)=int32_t(0x80380000);
    {
        auto f=frame(0,{0,1,2});
        require(f.roads.size()==3 && matches(f.roads[1],1,s,f.seconds) && matches(f.roads[0],0,s,f.seconds),
            "a grown record draws its new mesh");
        require(heap::frees==freed+1 && counters().road_buffers[0]==roads,"growth frees the outgrown block");
        require(counters().road_bytes>=payload(1,s,f.seconds),"growth allocates the new mesh size");
    }
    // A smaller rebuild fits the block it already has.
    freed=heap::frees; auto blocks=heap::live_blocks();
    w(record(1)+4)=int32_t(0x80310000+1*0x4000);
    {
        auto f=frame(0,{0,1,2});
        require(matches(f.roads[1],1,s,f.seconds),"a shrunken record draws its new mesh");
        require(heap::frees==freed && heap::live_blocks()==blocks,"a smaller rebuild reuses its block");
    }
    // A refused allocation draws nothing for that record and leaves the rest intact.
    lambo_rain_invalidate();
    heap::refuse=true;
    {
        auto f=frame(1,{0,1,2});
        require(f.roads.empty(),"no heap, no rain geometry");
        require(uint32_t(w(0x800BF240+0x7A50+0x1C0+16))==0x06000000,"the root still publishes");
    }
    heap::refuse=false;
    {
        auto f=frame(1,{0,1,2});
        require(f.roads.size()==3 && matches(f.roads[2],2,s,f.seconds),"rain recovers when the heap does");
    }
    // The slot never holds more than 200 road buffers, however many records the track cycles through.
    constexpr int many=300;
    for (int i=10;i<10+many;++i) {
        w(record(i)+4)=int32_t(0x80310000); w(record(i)+8)=0; w(record(i)+12)=0;
    }
    for (int first=10;first<10+many;first+=100) {
        std::vector<int> order;
        for (int i=first;i<first+100;++i) order.push_back(i);
        auto f=frame(0,order);
        require(f.roads.size()<=100 && counters().road_buffers[0]<=200,"road buffers per slot stay capped");
    }
    require(counters().road_buffers[0]==200,"the cap is reached, not exceeded");
    // The next race starts from nothing.
    h(0x800CE6AC)=7; frame(0,{}); frame(1,{});
    require(counters().road_bytes==0 && counters().road_buffers[0]==0 && counters().road_buffers[1]==0,
        "an epoch change frees every road buffer");
}
}

// Camera rain replayed the way RT64 draws it. RT64 rounds every rectangle edge
// up to a whole native pixel before scaling it to the output (FixedRect::left(true)
// in convertViewportRect), so a rectangle cannot carry sub-pixel art at any
// resolution. Modern rain is therefore triangles, which RT64 rasterizes at the
// output resolution; Retro keeps its native-pixel rectangles.
namespace camera {
uint8_t* rdram;
int32_t& w(uint32_t a) { return MEM_W(0,int32_t(a)); }
int16_t& h(uint32_t a) { return MEM_H(0,int32_t(a)); }
struct Point { float x=0,y=0; unsigned alpha=0; };
using Triangle=std::array<Point,3>;
struct Drawn {
    std::vector<std::array<int,4>> rects; // 10.2 native coordinates
    std::vector<Triangle> triangles;      // native screen pixels after RT64's widescreen scale
    std::vector<unsigned> slots;          // every vertex cache slot written
    unsigned origin=0,left=0,right=0;     // viewport origin, scissor edge origins
    bool balanced=true,stale=false;
};
using Matrix=std::array<float,16>;
Matrix matrix(uint32_t a) { // N64 s15.16: sixteen integer halves, then sixteen fractions
    Matrix m{};
    for (int k=0;k<16;++k)
        m[k]=float(int32_t((uint32_t(uint16_t(h(a+k*2)))<<16)|uint16_t(h(a+32+k*2))))/65536;
    return m;
}
Matrix multiply(const Matrix& a,const Matrix& b) { // row vectors: v*a*b
    Matrix m{};
    for (int r=0;r<4;++r) for (int c=0;c<4;++c) for (int k=0;k<4;++k) m[r*4+c]+=a[r*4+k]*b[k*4+c];
    return m;
}
// ratio: RT64's horizontal projection scale for a wide viewport (adjustProjectionMatrix).
Drawn replay(uint32_t list,float ratio) {
    Drawn out;
    std::map<unsigned,int> depth;
    Matrix projection{},modelview{};
    float scale[2]{},translate[2]{};
    struct Slot { Point p; unsigned load; };
    std::map<unsigned,Slot> cache;
    unsigned loads=0;
    auto stack=[&](unsigned id,int d) { if ((depth[id]+=d)<0) out.balanced=false; };
    for (uint32_t at=list;;at+=8) {
        require(at<list+0x20000,"camera list terminates");
        uint32_t a=w(at),b=w(at+4);
        if (a==0xB8000000) break;
        switch (a>>24) {
        case 0x01: {
            unsigned params=(a>>16)&0xFF;
            if (params&1) projection=matrix(b);
            else { if (params&4) stack(100,1); modelview=matrix(b); }
            break;
        }
        case 0xBD: stack(100,-1); break;
        case 0x04: {
            unsigned n=(a>>10)&63,first=(a>>17)&127;
            Matrix mvp=multiply(modelview,projection);
            ++loads;
            for (unsigned i=0;i<n;++i) {
                uint32_t v=b+i*16;
                float in[4]{float(h(v)),float(h(v+2)),float(h(v+4)),1},clip[4]{};
                for (int c=0;c<4;++c) for (int k=0;k<4;++k) clip[c]+=in[k]*mvp[k*4+c];
                Point p{clip[0]/clip[3]/ratio*scale[0]+translate[0],-clip[1]/clip[3]*scale[1]+translate[1],
                    unsigned(w(v+12)&255)};
                cache[first+i]={p,loads}; out.slots.push_back(first+i);
            }
            break;
        }
        case 0xB1: case 0xBF: {
            auto tri=[&](uint32_t word) {
                unsigned v[3]{(word>>17)&127,(word>>9)&127,(word>>1)&127};
                Triangle t{};
                for (int k=0;k<3;++k) {
                    auto found=cache.find(v[k]);
                    if (found==cache.end() || found->second.load!=loads) out.stale=true;
                    else t[k]=found->second.p;
                }
                out.triangles.push_back(t);
            };
            if ((a>>24)==0xB1) tri(a);
            tri(b);
            break;
        }
        case RT64_EXTENDED_OPCODE:
            switch (a&0xFFFFFF) {
            case G_EX_PUSHSCISSOR_V1:stack(1,1);break;case G_EX_POPSCISSOR_V1:stack(1,-1);break;
            case G_EX_PUSHOTHERMODE_V1:stack(2,1);break;case G_EX_POPOTHERMODE_V1:stack(2,-1);break;
            case G_EX_PUSHCOMBINE_V1:stack(3,1);break;case G_EX_POPCOMBINE_V1:stack(3,-1);break;
            case G_EX_PUSHPRIMCOLOR_V1:stack(4,1);break;case G_EX_POPPRIMCOLOR_V1:stack(4,-1);break;
            case G_EX_PUSHGEOMETRYMODE_V1:stack(5,1);break;case G_EX_POPGEOMETRYMODE_V1:stack(5,-1);break;
            case G_EX_PUSHVIEWPORT_V1:stack(6,1);break;case G_EX_POPVIEWPORT_V1:stack(6,-1);break;
            case G_EX_PUSHPROJMATRIX_V1:stack(7,1);break;case G_EX_POPPROJMATRIX_V1:stack(7,-1);break;
            case G_EX_SETSCISSOR_V1: out.left=(b>>2)&0xFFF; out.right=(b>>14)&0xFFF; at+=8; break;
            case G_EX_FILLRECT_V1: {
                at+=8; uint32_t c=w(at),d=w(at+4);
                out.rects.push_back({int(c>>16),int(c&0xFFFF),int(d>>16),int(d&0xFFFF)});
                break;
            }
            case G_EX_SETVIEWPORT_V1: {
                out.origin=b&0xFFF;
                at+=8; uint32_t vp=w(at+4);
                scale[0]=h(vp)/4.0f; scale[1]=h(vp+2)/4.0f;
                translate[0]=h(vp+8)/4.0f; translate[1]=h(vp+10)/4.0f;
                break;
            }
            }
            break;
        }
    }
    for (auto& entry:depth) if (entry.second) out.balanced=false;
    return out;
}
float altitude(const Triangle& t) { // shortest height: how thin the triangle is
    auto edge=[](Point a,Point b) { return std::hypot(b.x-a.x,b.y-a.y); };
    float area=std::abs((t[1].x-t[0].x)*(t[2].y-t[0].y)-(t[2].x-t[0].x)*(t[1].y-t[0].y));
    float longest=std::max({edge(t[0],t[1]),edge(t[1],t[2]),edge(t[2],t[0])});
    return longest>0 ? area/longest : 0;
}
std::vector<uint32_t> cameras(unsigned settings,int players,int slot=0) {
    w(cache::settings_at)=int32_t(settings);
    return cache::frame(slot,{},players).cameras;
}
float ratio() { return target_aspect/(4.0f/3); }
// Every vertex lies in its view (native px). Streaks enter above and leave
// below, and running drops slide off the bottom; the scissor clips those.
void require_inside(const Drawn& d,float x0,float y0,float width,float height,const char* message) {
    float sx=0,sy=0; size_t n=0;
    for (const auto& t:d.triangles) for (const auto& p:t) {
        require(p.x>=x0-12 && p.x<=x0+width+12 && p.y>=y0-height*0.2f-8 && p.y<=y0+height*1.3f+8,message);
        sx+=p.x; sy+=p.y; ++n;
    }
    require(n && std::abs(sx/n-(x0+width/2))<width*0.2f && std::abs(sy/n-(y0+height/2))<height*0.25f,message);
}
void run(uint8_t* ram) {
    rdram=ram;
    cache::setup();
    const unsigned modern=2|(70<<8)|0x10000,retro=1|(70<<8)|0x10000;

    auto one=cameras(modern,1);
    require(one.size()==1,"Modern 1P draws one camera list");
    Drawn d=replay(one[0],ratio());
    require(d.rects.empty(),"Modern camera rain draws no rectangles: RT64 snaps their edges to whole native pixels");
    require(d.triangles.size()>=200,"Modern camera rain is triangles that RT64 rasterizes at output resolution");
    require(d.balanced,"Modern camera rain restores every state stack it pushes");
    require(!d.stale,"every triangle uses vertices from its own vertex load");
    for (unsigned s:d.slots) require(s>=32 && s<=94,"camera rain loads slots 32..94, preserving the native bank 0..31");
    require(d.origin==G_EX_ORIGIN_NONE && d.left==G_EX_ORIGIN_LEFT && d.right==G_EX_ORIGIN_RIGHT,
        "a full-width view keeps the native viewport origin and scissors the whole output");
    size_t thin=0,graded=0;
    for (const auto& t:d.triangles) {
        thin+=altitude(t)<0.5f;
        unsigned lo=std::min({t[0].alpha,t[1].alpha,t[2].alpha}),hi=std::max({t[0].alpha,t[1].alpha,t[2].alpha});
        graded+=hi-lo>=40;
    }
    require(thin>=100,"Modern streaks are thinner than half a native pixel");
    require(graded>=100,"Modern streaks and drops fade across each shape");
    require_inside(d,0,0,320,240,"Modern 1P rain fills the whole widened view");

    cache::pause_tick();
    Drawn later=replay(cameras(modern,1,1)[0],ratio());
    require(later.triangles.size()>=200 && (later.triangles[0][0].x!=d.triangles[0][0].x ||
        later.triangles[0][0].y!=d.triangles[0][0].y),"Modern rain falls over time");

    Drawn old=replay(cameras(retro,1)[0],ratio());
    require(!old.rects.empty() && old.triangles.empty() && old.balanced,"Retro keeps native-pixel rectangles");

    auto two=cameras(modern,2);
    require(two.size()==2,"each 2P view draws camera rain");
    for (int p=0;p<2;++p) {
        Drawn view=replay(two[p],ratio());
        require(view.balanced && !view.stale && view.rects.empty(),"2P camera rain is balanced triangles");
        require_inside(view,0,float(p*120),320,120,"2P rain stays in its own half");
    }
    auto four=cameras(modern,4);
    require(four.size()==4,"each 4P view draws camera rain");
    for (int p=0;p<4;++p) {
        Drawn view=replay(four[p],ratio());
        require(view.balanced && !view.stale && view.rects.empty(),"4P camera rain is balanced triangles");
        require_inside(view,float(p%2*160),float(p/2*120),160,120,"4P rain stays in its own quadrant");
        require(view.origin==G_EX_ORIGIN_WIDE,"quadrant rain is widened like the game's quadrant scenes");
        require(view.left==(p%2 ? G_EX_ORIGIN_CENTER : G_EX_ORIGIN_LEFT) &&
            view.right==(p%2 ? G_EX_ORIGIN_RIGHT : G_EX_ORIGIN_CENTER),"quadrant rain is scissored to its half of the output");
    }

    // Every shape fits one vertex load, at every intensity and view size.
    lambo::rain::ScreenMesh mesh;
    lambo::rain::Flow fast;
    for (int i=0;i<60;++i) lambo::rain::advance(fast,{80,0.4f},1.0f/60);
    for (float wide:{213.0f,427.0f,853.0f}) for (float tall:{120.0f,240.0f}) for (float when:{0.0f,5.5f,9.9f})
    for (const auto& flow:{lambo::rain::Flow{},fast}) {
        lambo::rain::screen_rain(wide,tall,1,when,flow,1.0f,true,mesh);
        for (const auto& shape:mesh.shapes)
            require(shape.count<=lambo::rain::screen_shape_vertices && shape.index_count%3==0,"rain shapes fit one vertex load");
    }
    // The densest setting at the widest supported output still fits its buffer.
    target_aspect=32.0f/9;
    Drawn wide=replay(cameras(2|(100<<8)|0x10000,1)[0],ratio());
    require(wide.triangles.size()>=200 && wide.balanced,"Heavy Modern rain fits at 32:9");
    require_inside(wide,0,0,320,240,"32:9 rain fills the widened view");
    target_aspect=16.0f/9;

    // Each view's streaks follow the car on its controller channel: speed at
    // vehicle+0x90 (km/h) and steering demand at vehicle+0xB2.
    auto car=[](uint32_t slot,int channel,int kmh,int steer,bool populated=true) {
        uint32_t v=LAMBO_VEHICLE_BASE+slot*uint32_t(sizeof(LamboVehicleRecord));
        h(v)=populated ? 8 : 0;
        h(v+offsetof(LamboVehicleRecord,channel))=int16_t(channel);
        w(v+offsetof(LamboVehicleRecord,speed))=kmh;
        h(v+offsetof(LamboVehicleRecord,steering_demand))=int16_t(steer);
    };
    // A free slot keeps a stale channel; the populated car after it is the one followed.
    car(1,1,300,-35,false); car(3,2,216,35); car(5,1,0,0);
    for (int f=0;f<30;++f) { cameras(modern,2,f%2); cache::pause_tick(); }
    const auto& driving=lambo::rain::view_flow(1);
    require(lambo::rain::view_flow(0).motion.forward==0 && lambo::rain::view_flow(0).motion.yaw==0,
        "a parked car leaves its view's rain at rest, whatever a free slot holds");
    require(driving.motion.forward>40 && driving.motion.forward<=60 && driving.motion.yaw>0.3f,
        "the second view follows the car on channel 2: 216 km/h is 60 m/s, steering right turns right");
    // Pause holds the rain where it is.
    h(0x800CE808)=1;
    auto held=driving.slabs;
    cameras(modern,2); cache::pause_tick(); cameras(modern,2,1);
    require(driving.slabs[2].approach==held[2].approach && driving.slabs[0].fall==held[0].fall,"pause stops the streaks");
    h(0x800CE808)=0;
    // Leaving the race resets every view.
    h(0x800CE6AC)=7; cameras(modern,2);
    require(lambo::rain::view_flow(1).motion.forward==0,"leaving the race settles the rain");
    h(0x800CE6AC)=8;
    car(1,0,0,0,false); car(3,0,0,0,false); car(5,0,0,0,false);
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
        int scissor=0,draws=0;
        for (unsigned i=0;i<5000;++i) {
            uint32_t a=w(camera+i*8);
            if (a==0xB8000000) break;
            if ((a>>24)==0xB1 || (a>>24)==0xBF) ++draws;
            if ((a>>24)!=RT64_EXTENDED_OPCODE) continue;
            switch(a&0xFFFFFF) {
            case G_EX_PUSHSCISSOR_V1:++scissor;break;case G_EX_POPSCISSOR_V1:--scissor;break;
            case G_EX_PUSHOTHERMODE_V1:++other;break;case G_EX_POPOTHERMODE_V1:--other;break;
            case G_EX_PUSHCOMBINE_V1:++combine;break;case G_EX_POPCOMBINE_V1:--combine;break;
            case G_EX_PUSHPRIMCOLOR_V1:++prim;break;case G_EX_POPPRIMCOLOR_V1:--prim;break;
            case G_EX_FILLRECT_V1:++draws;++i;break;
            case G_EX_SETSCISSOR_V1:case G_EX_SETVIEWPORT_V1:++i;break;
            }
            require(scissor>=0 && other>=0 && combine>=0 && prim>=0,"camera cannot underflow state stacks");
        }
        require(draws>0 && !scissor && !other && !combine && !prim,"camera restores every changed stack");
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
    // A dry task must not query RT64 through any rain producer.
    aspect_reads=0;
    lambo_rain_frame(rdram);
    lambo_rain_active(rdram);
    lambo_rain_sky(rdram,0,0,123);
    require(aspect_reads==0,"dry rain hooks query no renderer aspect");
    w(0x80000100)=1|(70<<8);
    aspect_reads=0;
    lambo_rain_frame(rdram);
    heap::refuse=true;
    require(lambo_rain_sky(rdram,0,0,123)==123,"sky allocation failure keeps the native tile");
    heap::refuse=false;
    const auto blocks_before=heap::live_blocks();
    h(0x800CE794)=6;
    require(lambo_rain_sky(rdram,0,0,123)==123,"invalid circuit keeps native sky");
    h(0x800CE794)=0;
    require(lambo_rain_sky(rdram,8,0,123)==123 && lambo_rain_sky(rdram,0,2,123)==123 &&
        heap::live_blocks()==blocks_before,"invalid tile allocates nothing");
    // All style/circuit/tile combinations, including mirror and vertical order.
    for (unsigned style=1;style<=2;++style) for (int track=0;track<6;++track) {
        w(0x80000100)=int32_t(style|(70<<8)); h(0x800CE794)=int16_t(track);
        for (int row=0;row<2;++row) h(0x80089054+track*4+row*2)=int16_t(0xA5^track);
        uint32_t first=lambo_rain_sky(rdram,3,1,123),base=first-11*4096;
        require(first!=123,"sky tile allocated");
        require(h(base)==0,"unrequested sky tile stays ungenerated");
        for (int row=0;row<2;++row) for (int tile=0;tile<8;++tile) {
            uint32_t at=lambo_rain_sky(rdram,tile,row,123);
            require(at==base+uint32_t(row*8+tile)*4096,"lazy sky preserves tile addresses");
            bool mirror=(uint16_t(h(0x80089054+track*4+row*2))&(1u<<tile))!=0;
            for (int y=0;y<32;++y) for (int x=0;x<64;++x)
                require(uint16_t(h(at+uint32_t(y*64+x)*2))==lambo::rain::cloud_pixel(
                    tile*64+(mirror?63-x:x),row*32+31-y,style==2),"lazy sky pixel matches full generation");
        }
        // Repeated requests must not rewrite a published tile.
        auto saved=h(first); h(first)=1234;
        require(lambo_rain_sky(rdram,3,1,123)==first && h(first)==1234,"published sky tile stays immutable");
        h(first)=saved;
    }
    require(aspect_reads<=1,"wet rain hooks sample aspect at most once per task");
    h(0x800CE794)=0;
    cache::run(rdram);
    camera::run(rdram);
}
