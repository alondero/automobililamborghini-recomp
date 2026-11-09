// USA game-thread producers only. No renderer thread follows weather pointers.
// Two task slots reuse heap lists only after the native DP completion fence.
// Addresses, instruction witnesses, cache lifecycle and renderer limits: docs/rain.md.
#include "lambo_rain.h"
#include "lambo_mod_gameplay.h"
#include "lambo_sky_panorama.h"
#include "lambo_log.h"
#include "librecomp/addresses.hpp"
#include "rt64_extended_gbi.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace {
using namespace lambo::rain;
int32_t& word(uint8_t* rdram,uint32_t a) { return MEM_W(0,static_cast<int32_t>(a)); }
int16_t& half(uint8_t* rdram,uint32_t a) { return MEM_H(0,static_cast<int32_t>(a)); }
// Guest words are native words at consecutive addresses.
void store(uint8_t* ram,uint32_t target,const uint32_t* words,size_t count) {
    std::memcpy(&word(ram,target),words,count*4);
}
bool native(uint32_t a,uint32_t bytes) { return (a&3)==0 && a>=0x80000000u && a<=0x80800000u-bytes; }
unsigned settings(uint8_t* ram) {
    if (!ram || half(ram,0x800CE6AC)!=8 || half(ram,0x800CE6B4)==4 || lambo_sky_target_aspect()<=0) return 0;
    unsigned s=lambo_rain_settings(ram);
    return (s&3) && ((s>>8)&127) ? s : 0;
}
int slot(uint8_t* ram) {
    uint32_t task=word(ram,0x800A2BFC);
    return task==0x800BF240u ? 0 : task==0x800BF240u+0x7A50u ? 1 : -1;
}
float seconds=0;
Counters stats;
struct Buffer {
    uint32_t address=0,capacity=0;
    uint64_t generation=0; // Segment::generation of the static geometry it holds
    float seconds=NAN;     // ripple time it holds
};
struct List {
    std::vector<std::array<uint32_t,2>> words;
    void cmd(uint32_t a,uint32_t b=0) { words.push_back({a,b}); }
    void ex(const GfxCommand& c) { cmd(c.values.word0,c.values.word1); }
    void extension(uint32_t op) { cmd((RT64_EXTENDED_OPCODE<<24)|op); }
    void save(bool geometry) {
        cmd(0xE7000000);
        extension(G_EX_PUSHOTHERMODE_V1); extension(G_EX_PUSHCOMBINE_V1);
        extension(G_EX_PUSHPRIMCOLOR_V1);
        if (geometry) extension(G_EX_PUSHGEOMETRYMODE_V1);
    }
    void restore(bool geometry) {
        cmd(0xE7000000);
        if (geometry) extension(G_EX_POPGEOMETRYMODE_V1);
        extension(G_EX_POPPRIMCOLOR_V1); extension(G_EX_POPCOMBINE_V1);
        extension(G_EX_POPOTHERMODE_V1);
    }
    void write(uint8_t* ram,uint32_t target) const {
        store(ram,target,reinterpret_cast<const uint32_t*>(words.data()),words.size()*2);
    }
};
struct Task {
    std::vector<Buffer> roads;
    uint64_t frame=1;   // Segment::frame stamps record emission into this task
    unsigned records=0; // distinct records seen this frame; each owns roads[ordinal]
    std::array<Buffer,4> cameras;
    Buffer root;
    std::vector<std::pair<uint32_t,uint32_t>> insertions;
    List copy,wrapper,camera; // command storage reused across frames
};
std::array<Task,2> tasks;

// Host copy of one segment record's static rain geometry; docs/rain.md
// "Cache lifecycle". Valid while the epoch and the identity key are unchanged.
struct Segment {
    std::array<uint32_t,3> lists{}; // road, wall, scenery list pointers (+4/+8/+12)
    unsigned style=0;               // settings bits that shape geometry
    uint64_t generation=0;          // unique per build
    bool sheltered=true;            // camera rain hidden in this segment
    std::vector<uint32_t> vertices; // encoded guest vertex block; empty: no puddles
    std::vector<Puddle> ripples;
    float animated=NAN;             // ripple time encoded in `vertices`
    std::array<uint64_t,2> frame{}; // Task::frame of the last emission per slot
    std::array<uint32_t,2> target{};
};
std::unordered_map<uint32_t,Segment> segments;
uint64_t generations=0;
// Generous for 200-record pools; beyond it, clear and rebuild.
constexpr size_t segment_limit=1024;
constexpr unsigned geometry_settings=0x7F03; // style and intensity, not camera drops
constexpr uint32_t max_vertices=12000;       // per-segment visual limit (puddle_mesh)

void invalidate() {
    // Rebuilt entries get new generations, so task buffers rewrite themselves.
    segments.clear(); // keeps buckets
}
void encode(const Vertex& v,uint32_t* out) {
    auto h=[](float f) { return uint32_t(uint16_t(coordinate(f))); };
    out[0]=(h(v.p.x)<<16)|h(v.p.y); out[1]=h(v.p.z)<<16; out[2]=0; out[3]=v.rgba;
}
bool decode(uint8_t* ram,uint32_t list,std::vector<Triangle>& triangles) {
    ++stats.decodes;
    return road_mesh(ram,list,triangles);
}
void build(uint8_t* ram,Segment& segment,const std::array<uint32_t,3>& lists,unsigned s) {
    ++stats.builds;
    segment.lists=lists; segment.style=s&geometry_settings; segment.generation=++generations;
    segment.vertices.clear(); segment.ripples.clear(); segment.animated=seconds;
    static std::vector<Triangle> road,part;
    static std::vector<RoofPlane> roof;
    static PuddleMesh mesh;
    // A failed decode is cached too. Rain builds only in state 8, and the track
    // loader hook clears the cache after the lists are complete.
    bool decoded=decode(ram,lists[0],road);
    roof.clear();
    for (int i:{1,2}) if (decode(ram,lists[i],part)) roof_planes(part,roof);
    // Segment coverage is conservative: hide screen rain for a covered road
    // segment, including its portal. World puddles use per-face coverage.
    segment.sheltered=!decoded || std::any_of(road.begin(),road.end(),[&](const Triangle& t) {
        return floor(t) && covered((t.p[0]+t.p[1]+t.p[2])*(1.0f/3),roof);
    });
    if (!decoded) return;
    puddle_mesh(road,roof,(s&3)==2,seconds,float((s>>8)&127)/100,mesh);
    if (mesh.vertices.size()>max_vertices) return;
    segment.vertices.resize(mesh.vertices.size()*4);
    for (size_t i=0;i<mesh.vertices.size();++i) encode(mesh.vertices[i],&segment.vertices[i*4]);
    segment.ripples=mesh.ripples;
}
// Current geometry for a record, rebuilt when its identity key changed.
Segment& current(uint8_t* ram,uint32_t record,unsigned s,decltype(segments)::iterator found) {
    std::array<uint32_t,3> lists{uint32_t(word(ram,record+4)),uint32_t(word(ram,record+8)),
        uint32_t(word(ram,record+12))};
    if (found==segments.end()) {
        if (segments.size()>=segment_limit) {
            LAMBO_LOG("rain", "segment cache full (%zu); rebuilding\n",segments.size());
            invalidate();
        }
        found=segments.try_emplace(record).first;
    }
    Segment& segment=found->second;
    if (!segment.generation || segment.lists!=lists || segment.style!=(s&geometry_settings))
        build(ram,segment,lists,s);
    return segment;
}

// Road sublist layout: vertex block at the buffer, commands at `list_offset`.
constexpr uint32_t list_offset=max_vertices*16;
const List& header() {
    static const List list=[] {
        List l; l.save(true);
        l.cmd(0xBA000020,0x00100000); // Two cycles, no texture LUT.
        l.cmd(0xB9000020,0xC8104A50); // Native fog, alpha blend, Z compare, decal.
        l.cmd(0xFCFFFFFF,0xFFFE773B); // SHADE RGB, PRIMITIVE alpha in both cycles.
        l.cmd(0xB6000000,0xFFFFFFFF); l.cmd(0xB7000000,0x10205);
        return l;
    }();
    return list;
}
uint32_t header_commands() { return uint32_t(header().words.size()); }
const List& footer() {
    static const List list=[] { List l; l.restore(true); l.cmd(0xB8000000); return l; }();
    return list;
}
uint32_t face_color(const Segment& segment,size_t face) {
    const uint32_t* v=&segment.vertices[face*12];
    return 0xFFFFFF00u|((v[3]&255)+(v[7]&255)+(v[11]&255))/3;
}
uint32_t face_command(uint32_t buffer,size_t face) {
    return buffer+list_offset+(header_commands()+uint32_t(face)*4)*8;
}
// Each face: pipe sync, face color, its three vertices, one triangle.
void face_commands(uint8_t* ram,uint32_t buffer,const Segment& segment,size_t count) {
    for (size_t k=0;k<count;++k) {
        // RT64's expanded vertex cache slots 32..34 preserve the native
        // F3DEX 0..31 bank, including vertices inherited by the next list.
        const uint32_t words[8]{0xE7000000,0,0xFA000000,face_color(segment,k),
            0x04400C2F,buffer+uint32_t(k)*48,0xBF000000,(32u<<17)|(33u<<9)|(34u<<1)};
        store(ram,face_command(buffer,k),words,8);
    }
}
void animate(Segment& segment) {
    if (segment.ripples.empty() || segment.animated==seconds) return;
    Vertex quad[6];
    for (const auto& p:segment.ripples) {
        float phase=ripple_phase(p,seconds);
        for (int side=0;side<ripple_sides;++side) {
            ripple(p,side,phase,quad);
            // The quad repeats two corners: a0 b0 b1 | a0 b1 a1.
            uint32_t* out=&segment.vertices[ripple_vertex(p,side)*4];
            for (int i:{0,1,2,5}) encode(quad[i],out+i*4);
            std::copy(out,out+4,out+12); std::copy(out+8,out+12,out+16);
        }
    }
    segment.animated=seconds;
}
// Brings a task buffer up to date: static geometry once per build, then
// Modern ripple vertices and their face colors when the clock moved.
void emit(uint8_t* ram,Buffer& buffer,const Segment& segment) {
    uint32_t address=buffer.address;
    size_t faces=segment.vertices.size()/12;
    if (buffer.generation!=segment.generation) {
        ++stats.full_writes;
        store(ram,address,segment.vertices.data(),segment.vertices.size());
        header().write(ram,address+list_offset);
        face_commands(ram,address,segment,faces);
        footer().write(ram,face_command(address,faces));
    } else if (!segment.ripples.empty() && buffer.seconds!=segment.animated) {
        // Ripple vertices and the color word of their two faces; every other
        // command word is time independent.
        ++stats.ripple_writes;
        for (const auto& p:segment.ripples) for (int side=0;side<ripple_sides;++side) {
            uint32_t first=ripple_vertex(p,side);
            store(ram,address+first*16,&segment.vertices[first*4],24);
            for (size_t face:{first/3,first/3+1})
                word(ram,face_command(address,face)+12)=int32_t(face_color(segment,face));
        }
    }
    buffer.generation=segment.generation;
    buffer.seconds=segment.animated;
}
uint32_t allocate(uint8_t* ram,Buffer& buffer,uint32_t size) {
    if (buffer.capacity<size) {
        // Mesh output has a fixed maximum, so a slot allocates at most once.
        if (buffer.address) return 0;
        auto* memory=static_cast<uint8_t*>(recomp::alloc(ram,size));
        if (!memory) return 0;
        buffer.address=0x80000000u+uint32_t(memory-ram); buffer.capacity=size;
    }
    return buffer.address;
}
bool append(uint8_t* ram,uint32_t list) {
    int index=slot(ram);
    uint32_t cursor=word(ram,0x800A39CC),task=word(ram,0x800A2BFC);
    // Record insertion points without consuming a byte of the native arena.
    // Publication splices these calls into a separate, fenced root copy.
    if (index<0 || (cursor&7) || cursor<task+0x1C0 || cursor>task+0x79C0 ||
        tasks[index].insertions.size()>=1604) return false;
    tasks[index].insertions.emplace_back(cursor,list);
    return true;
}
bool shelter(uint8_t* ram,unsigned s) {
    int segment=half(ram,0x800BF1CC);
    uint32_t records=word(ram,0x800BF1D0);
    if (segment<0 || segment>=200 || !native(records,200*64)) return true;
    uint32_t record=records+segment*64;
    return current(ram,record,s,segments.find(record)).sheltered;
}
// Leaving the race or changing circuit starts a new epoch. Viewport count
// does not change track geometry, so it is not part of the epoch.
void epoch(uint8_t* ram) {
    static int16_t circuit=-1;
    int16_t now=half(ram,0x800CE794);
    if (half(ram,0x800CE6AC)!=8 || now!=circuit) {
        if (!segments.empty()) invalidate();
        circuit=now;
    }
}
}

namespace lambo::rain {
const Counters& counters() { return stats; }
float animation_seconds() { return seconds; }
}

extern "C" void lambo_rain_invalidate(void) { invalidate(); }

extern "C" void lambo_rain_frame(uint8_t* ram) {
    int index=slot(ram);
    if (index<0) return;
    if (segments.bucket_count()<256) segments.reserve(256);
    epoch(ram);
    auto& task=tasks[index];
    ++task.frame; task.records=0;
    task.insertions.clear();
    using Clock=std::chrono::steady_clock;
    static auto last=Clock::now();
    auto now=Clock::now();
    float dt=std::chrono::duration<float>(now-last).count(); last=now;
    if (settings(ram) && half(ram,0x800CE808)==0) seconds+=std::min(dt,0.1f);
    if (half(ram,0x800CE6AC)!=8) seconds=0;
}

extern "C" unsigned lambo_rain_active(uint8_t* ram) { return settings(ram)!=0; }
extern "C" unsigned lambo_rain_wet(uint8_t* ram) {
    return ram && half(ram,0x800CE6AC)==8 && half(ram,0x800CE6B4)!=4 && lambo_rain_settings(ram)!=0;
}

extern "C" void lambo_rain_publish(uint8_t* ram,uint32_t root,uint32_t bytes) {
    int index=slot(ram);
    if (index<0) return;
    auto& task=tasks[index];
    static unsigned sampled=0;
    if (half(ram,0x800CE6AC)==8 && (++sampled<=4 || sampled%1800==0))
        LAMBO_LOG("rain", "publish slot=%d settings=%x insertions=%zu records=%u bytes=%u "
            "segments=%zu decodes=%llu builds=%llu full_writes=%llu ripple_writes=%llu\n",
            index,settings(ram),task.insertions.size(),task.records,bytes,segments.size(),
            (unsigned long long)stats.decodes,(unsigned long long)stats.builds,
            (unsigned long long)stats.full_writes,(unsigned long long)stats.ripple_writes);
    root|=0x80000000u;
    uint32_t owner=word(ram,0x800A2BFC);
    if (task.insertions.empty() || root!=owner+0x1C0 || bytes<40 ||
        (bytes&7) || bytes>0x79C0-0x1C0 || uint32_t(word(ram,root+bytes-8))!=0xB8000000u) return;
    // Native root emission is linear. Reject a stale/out-of-order insertion
    // rather than interpreting arbitrary offsets or copying a partial frame.
    uint32_t previous=root;
    for (auto [at,list]:task.insertions) {
        if (at<previous || at>=root+bytes || (at&7)) return;
        previous=at;
    }
    constexpr uint32_t capacity=0x8000+1604*24+16;
    uint32_t buffer=allocate(ram,task.root,capacity);
    if (!buffer) return;
    List& copy=task.copy; GfxCommand c{};
    copy.words.clear(); copy.words.reserve(capacity/8+1);
    // The root wrapper enabled extended addresses solely for this call.
    gEXSetRDRAMExtended(&c,0); copy.ex(c);
    size_t insertion=0;
    for (uint32_t at=root;at<root+bytes;at+=8) {
        while (insertion<task.insertions.size() && task.insertions[insertion].first==at) {
            gEXSetRDRAMExtended(&c,1); copy.ex(c);
            copy.cmd(0x06000000,task.insertions[insertion++].second);
            gEXSetRDRAMExtended(&c,0); copy.ex(c);
        }
        copy.cmd(word(ram,at),word(ram,at+4));
    }
    if (copy.words.size()*8>capacity) return;
    copy.write(ram,buffer);
    List& wrapper=task.wrapper;
    wrapper.words.clear();
    gEXEnable(&c); wrapper.ex(c);
    gEXSetRDRAMExtended(&c,1); wrapper.ex(c);
    wrapper.cmd(0x06000000,buffer);
    gEXSetRDRAMExtended(&c,0); wrapper.ex(c);
    wrapper.cmd(0xB8000000);
    wrapper.write(ram,root);
    task.insertions.clear();
}

extern "C" void lambo_rain_road(uint8_t* ram,uint32_t record) {
    unsigned s=settings(ram); int index=slot(ram);
    if (!s || index<0 || !native(record,64)) return;
    auto& task=tasks[index];
    auto found=segments.find(record);
    if (found!=segments.end() && found->second.frame[index]==task.frame) {
        if (found->second.target[index]) append(ram,found->second.target[index]);
        return;
    }
    if (task.records>=200) return;
    unsigned ordinal=task.records++;
    Segment& segment=current(ram,record,s,found);
    segment.frame[index]=task.frame; segment.target[index]=0;
    if (segment.vertices.empty() || !std::isfinite(seconds)) return;
    if (task.roads.size()<=ordinal) task.roads.resize(ordinal+1);
    constexpr uint32_t size=max_vertices*16+max_vertices/3*32+256;
    Buffer& buffer=task.roads[ordinal];
    if (!allocate(ram,buffer,size)) return;
    animate(segment);
    emit(ram,buffer,segment);
    segment.target[index]=buffer.address+list_offset;
    append(ram,segment.target[index]);
}

extern "C" uint32_t lambo_rain_sky(uint8_t* ram,int tile,int row,uint32_t original) {
    unsigned s=settings(ram);
    int track=half(ram,0x800CE794);
    if (!s || track<0 || track>=6 || tile<0 || tile>=8 || row<0 || row>1) return original;
    // Immutable once published: texture DMA may outlive several CPU frames.
    static std::array<std::array<Buffer,6>,2> skies;
    auto& sky=skies[(s&3)-1][track];
    if (!sky.address) {
        if (!allocate(ram,sky,16*4096)) return original;
        for (int r=0;r<2;++r) for (int t=0;t<8;++t) {
            bool mirror=(uint16_t(half(ram,0x80089054+track*4+r*2))&(1u<<t))!=0;
            for (int y=0;y<32;++y) for (int x=0;x<64;++x)
                half(ram,sky.address+(r*8+t)*4096+(y*64+x)*2)=int16_t(cloud_pixel(t*64+(mirror?63-x:x),r*32+31-y,(s&3)==2));
        }
    }
    return sky.address+(row*8+tile)*4096;
}

extern "C" void lambo_rain_camera(uint8_t* ram) {
    unsigned s=settings(ram); int index=slot(ram);
    int players=half(ram,0x800CE6A4),player=half(ram,0x800CE6A6)-1;
    if (!s || index<0 || players<1 || players>4 || player<0 || player>=players || shelter(ram,s)) return;
    const bool modern=(s&3)==2;
    float full_width=std::clamp(lambo_sky_target_aspect()*240,240.0f,1920.0f);
    float x0=160-full_width/2,y0=0,w=full_width,h=240;
    if (players==2) { h=120; y0=player*h; }
    if (players>=3) { w=full_width/2; h=120; x0+=(player%2)*w; y0=(player/2)*h; }
    constexpr uint32_t capacity=40000;
    List& list=tasks[index].camera;
    list.words.clear(); list.words.reserve(capacity/8+1);
    list.save(false); list.extension(G_EX_PUSHSCISSOR_V1);
    GfxCommand c[2]{};
    gEXSetScissor(c,0,G_EX_ORIGIN_LEFT,G_EX_ORIGIN_RIGHT,0,y0,0,y0+h);
    list.ex(c[0]); list.ex(c[1]);
    gEXSetRectAspect(c,G_EX_ASPECT_STRETCH); list.ex(c[0]);
    list.cmd(0xBA000020,0); list.cmd(0xB9000020,0x00504240);
    list.cmd(0xFCFFFFFF,0xFFFDF6FB); // PRIMITIVE RGB/alpha.
    auto rect=[&](float x,float y,float width,float height,uint32_t rgba) {
        float l=std::max(x,x0),t=std::max(y,y0),r=std::min(x+width,x0+w),b=std::min(y+height,y0+h);
        if (l>=r || t>=b) return;
        auto pack=[](float x,float y) { return (uint32_t(uint16_t(std::lround(x*4)))<<16)|uint16_t(std::lround(y*4)); };
        list.cmd(0xE7000000); list.cmd(0xFA000000,rgba);
        list.cmd((RT64_EXTENDED_OPCODE<<24)|G_EX_FILLRECT_V1,G_EX_ORIGIN_NONE|(G_EX_ORIGIN_NONE<<12));
        list.cmd(pack(l,t),pack(r,b));
    };
    float intensity=float((s>>8)&127)/100;
    int count=int((modern?100:48)*intensity);
    for (int i=0;i<count;++i) {
        uint32_t seed=hash(uint32_t(i+player*619));
        float speed=modern?110+float(seed%90):140;
        float y=std::fmod(float(seed%1000)/1000*h+seconds*speed,h+20)-20;
        float x=std::fmod(float((seed>>10)%1000)/1000*w+seconds*9,w);
        if (!modern) { x=std::floor(x); y=std::floor(y); }
        int steps=modern?4:2;
        for (int j=0;j<steps;++j) rect(x0+x+j*(modern?0.4f:1),y0+y+j*2,
            modern?0.6f:1,modern?3:4,0xB6CBDD00u|uint32_t(modern?24+j*9:76));
    }
    if (s&0x10000) for (int i=0;i<(modern?18:7);++i) {
        uint32_t seed=hash(uint32_t(i+player*239+900));
        float age=std::fmod(seconds+float(seed%1000)/100,10.0f);
        float x=x0+float((seed>>12)%1000)/1000*w;
        float y=y0+float((seed>>2)%1000)/1000*h+age*age*(modern?0.6f:0.2f);
        float radius=modern?1.5f+float(seed%3):1;
        float fade=std::min(age,1.0f)*std::min(10-age,1.0f)*intensity;
        if (modern) {
            for (int row=-4;row<=4;++row) {
                float ry=float(row)/4,rx=radius*std::sqrt(std::max(0.0f,1-ry*ry));
                rect(x-rx,y+ry*radius,rx*2,0.5f,0x253C5000u|uint32_t(32*fade));
            }
            rect(x-radius*0.6f,y-radius*0.6f,1.3f,0.6f,0xD8EAF400u|uint32_t(110*fade));
            rect(x+radius*0.5f,y,0.5f,radius,0xA3C1D200u|uint32_t(65*fade));
        } else rect(std::floor(x),std::floor(y),2,3,0xA6BBCD00u|uint32_t(70*fade));
    }
    gEXSetRectAspect(c,G_EX_ASPECT_AUTO); list.ex(c[0]);
    list.extension(G_EX_POPSCISSOR_V1); list.restore(false); list.cmd(0xB8000000);
    if (list.words.size()*8>capacity) return;
    uint32_t buffer=allocate(ram,tasks[index].cameras[player],capacity);
    if (!buffer) return;
    list.write(ram,buffer); append(ram,buffer);
}
