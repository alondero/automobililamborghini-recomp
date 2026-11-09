#include "lambo_rain.h"
#include "lambo_f3dex.h"
#include "rain_reference.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace lambo::rain;
void require(bool condition,const char* message) {
    if (!condition) { std::cerr<<message<<'\n'; std::exit(1); }
}
bool same(const std::vector<Vertex>& a,const std::vector<Vertex>& b) {
    return a.size()==b.size() && (a.empty() || std::memcmp(a.data(),b.data(),a.size()*sizeof(Vertex))==0);
}
// Decoded road geometry has integral coordinates. Mostly gentle slopes, with
// some walls and degenerate faces, so every early-out in puddles() is reached.
std::vector<Triangle> fixture(unsigned faces,uint32_t salt,float height=0) {
    std::vector<Triangle> out;
    for (unsigned i=0;i<faces;++i) {
        uint32_t r=reference::hash(i*7919u+salt);
        float x=float(int(r%9000)-4500), z=float(int((r>>12)%9000)-4500), size=float(80+(r>>3)%1400);
        float slope=float(int((r>>20)%40)-20);
        Triangle t{{Point{x,height,z},Point{x+size,height+slope,z},Point{x,height-slope,z+size}}};
        if (r%11==0) t.p[2]={x,height+size,z};          // wall
        if (r%23==0) t.p[2]=t.p[1];                     // degenerate
        if (r%2) std::swap(t.p[1],t.p[2]);              // both windings
        out.push_back(t);
    }
    return out;
}
void rounding() {
    auto check=[](float f) {
        require(coordinate(f)==int16_t(std::lround(f)),"guest coordinate rounding matches std::lround");
    };
    for (int k=-70000;k<=70000;++k) {
        float half=float(k)+0.5f;
        for (float f:{float(k),half,std::nextafter(half,-1e9f),std::nextafter(half,1e9f),-half})
            check(f);
    }
    for (uint32_t i=0;i<2000000;++i) {
        uint32_t bits=reference::hash(i);
        float f; std::memcpy(&f,&bits,4);
        if (std::isfinite(f) && std::fabs(f)<2.0e9f) check(f);
    }
    for (float f:{0.0f,-0.0f,0.49999997f,-0.49999997f,8388607.5f,-8388607.5f,16777217.0f,2.0e9f,-2.0e9f}) check(f);
}
void equivalence() {
    rounding();
    for (unsigned faces:{1u,36u,120u,600u}) for (uint32_t salt:{1u,77u}) {
        auto road=fixture(faces,salt), roof=fixture(faces/3+1,salt+5,1000);
        roof.push_back(fixture(1,salt+9,-500)[0]);     // a floor below the road is no roof
        std::vector<RoofPlane> planes; roof_planes(roof,planes);
        for (int i=0;i<400;++i) {
            uint32_t r=reference::hash(uint32_t(i)+salt*31);
            Point p{float(int(r%10000)-5000),float(int((r>>14)%300)-150),float(int((r>>4)%10000)-5000)};
            require(covered(p,planes)==reference::covered(p,roof),"precomputed roof planes match per-pair coverage");
        }
        for (bool modern:{false,true}) for (float intensity:{0.35f,1.0f,1.7f}) {
            PuddleMesh mesh;
            puddle_mesh(road,planes,modern,0.25f,intensity,mesh);
            for (float seconds:{0.0f,0.25f,3.7f,61.03f,1234.5f}) {
                auto expected=reference::puddles(road,roof,modern,seconds,intensity);
                require(same(puddles(road,roof,modern,seconds,intensity),expected),"puddles bytes match e053389");
                // Only ripple vertices are time dependent; rewriting them in place
                // reproduces a full regeneration at the new time.
                for (const auto& p:mesh.ripples) for (int side=0;side<ripple_sides;++side)
                    ripple(p,side,ripple_phase(p,seconds),&mesh.vertices[ripple_vertex(p,side)]);
                require(same(mesh.vertices,expected),"animated static mesh matches regeneration");
            }
            if (!modern) require(mesh.ripples.empty(),"retro puddles have no time-dependent vertices");
            else if (faces>=36) require(!mesh.ripples.empty(),"fixture exercises animated ripples");
        }
    }
    require(puddles(fixture(5,3),{},true,NAN,1).empty(),"non-finite time emits nothing");
    PuddleMesh mesh; puddle_mesh(fixture(5,3),{},true,0,NAN,mesh);
    require(mesh.vertices.empty() && mesh.ripples.empty(),"non-finite intensity emits nothing");
    // Decoding into a caller vector reuses its storage, and failure leaves it empty.
    std::vector<uint8_t> ram(0x800000);
    lambo::f3dex::write(ram.data(),0x100,0xB8000000);
    std::vector<Triangle> reused(64);
    reused.clear(); auto capacity=reused.capacity();
    require(road_mesh(ram.data(),0x100,reused) && reused.empty() && reused.capacity()==capacity,
        "decode reuses caller storage");
}
constexpr float frame_time=1.0f/60;
// A streak shape: its head (two opaque vertices) and its clear tail, in native pixels.
struct Streak { float hx,hy,tx,ty; float length() const { return std::hypot(tx-hx,ty-hy); } };
std::vector<Streak> streaks(const ScreenMesh& mesh) {
    std::vector<Streak> out;
    for (const auto& s:mesh.shapes) {
        if (s.count!=4) continue;
        const auto* v=&mesh.vertices[s.first];
        out.push_back({(v[2].x+v[3].x)/2,(v[2].y+v[3].y)/2,(v[0].x+v[1].x)/2,(v[0].y+v[1].y)/2});
    }
    return out;
}
std::vector<Streak> rain(const Flow& flow,float width=427,float height=240,float intensity=1) {
    ScreenMesh mesh;
    screen_rain(width,height,0,0,flow,intensity,false,mesh);
    for (const auto& s:mesh.shapes)
        require(s.count<=screen_shape_vertices && s.index_count%3==0,"rain shapes fit one vertex load");
    return streaks(mesh);
}
Flow settled(Motion motion,int frames=120) {
    Flow flow;
    for (int i=0;i<frames;++i) advance(flow,motion,frame_time);
    return flow;
}
// Streaks keep their per-frame motion however long the race runs (#410). The
// old float clock lost ~0.1 px of precision in an hour; the drop volume's
// offsets wrap at its own size instead, so one frame always moves it equally.
void long_race() {
    for (Motion motion:{Motion{},Motion{70,0.3f}}) {
        // Twenty seconds in, the eased motion has fully reached the car's.
        Flow start=settled(motion,20*60),hour=settled(motion,60*60*60);
        for (int i=0;i<3;++i) {
            auto step=[&](Flow flow) {
                auto before=flow.slabs[i]; advance(flow,motion,frame_time); auto after=flow.slabs[i];
                return std::array<float,3>{after.fall-before.fall,after.approach-before.approach,after.drift-before.drift};
            };
            auto a=step(start),b=step(hour);
            for (int k=0;k<3;++k) {
                // A slab offset that wrapped during the frame says nothing about precision.
                if (std::abs(a[k])>1 || std::abs(b[k])>1) continue;
                require(std::abs(a[k]-b[k])<2e-5f,"an hour later, one frame moves the rain as far as at the start");
            }
            const auto& slab=hour.slabs[i];
            require(std::abs(slab.fall)<40 && std::abs(slab.approach)<10 && std::abs(slab.drift)<60,
                "the drop volume's offsets stay small");
        }
    }
}
// Modern streaks follow the car (#410): at rest they fall and slant with the
// wind; driving makes them stream out of the view centre and stretch;
// steering sweeps them sideways.
void speed() {
    auto mean=[](const std::vector<Streak>& all,auto value) {
        double sum=0; for (const auto& s:all) sum+=value(s); return sum/double(all.size());
    };
    auto rest=rain(settled({}));
    require(rest.size()>280 && rest.size()<390,"16:9 rain draws about 333 streaks at full intensity");
    for (const auto& s:rest) require(s.ty<s.hy && s.tx<=s.hx,"at rest, streaks fall to the right with tails up-left");

    auto fast=rain(settled({60,0}));
    require(fast.size()>250 && fast.size()<390,"driving keeps the streak count");
    size_t outward=0,far=0;
    for (const auto& s:fast) {
        float head=std::hypot(s.hx-213.5f,s.hy-120),tail=std::hypot(s.tx-213.5f,s.ty-120);
        if (head<40) continue;
        ++far; outward+=tail<head;
    }
    require(far>100 && outward>=far*9/10,"at speed, streaks point out of the view centre");
    auto length=[](const Streak& s) { return s.length(); };
    require(mean(fast,length)>2*mean(rest,length),"streaks stretch as speed rises");
    for (const auto& s:fast) require(s.length()<=240*0.35f+0.01f,"the nearest streaks are capped");

    auto sideways=[](const Streak& s) { return s.tx-s.hx; };
    auto right=rain(settled({0,0.6f})),left=rain(settled({0,-0.6f}));
    require(mean(right,sideways)>0.5 && mean(rest,sideways)<0 && mean(left,sideways)<mean(rest,sideways)-0.5,
        "steering sweeps the rain against the turn");

    Flow flow; advance(flow,{60,0},frame_time);
    require(flow.motion.forward>0 && flow.motion.forward<10,"the rain eases toward a sudden change of speed");
    Flow bad=settled({}); auto before=bad.slabs[0];
    advance(bad,{NAN,0},frame_time);
    require(bad.slabs[0].fall==before.fall && bad.motion.forward==0,"non-finite motion is ignored");

    // Count follows view area up to 16:9 (2P views are half height); wider outputs keep that count.
    auto two=rain(settled({}),427,120);
    require(two.size()>120 && two.size()<200,"a 2P view draws half the streaks");
    auto wide=rain(settled({60,0.3f}),853,240);
    require(wide.size()>280 && wide.size()<=383,"32:9 keeps the 16:9 count within the camera buffer bound");
    require(rain(settled({}),427,240,0.3f).size()<rest.size()/2,"intensity scales the count");
}
// The storm sky showed wide green and blue-grey bands in game (#410): each
// 5-bit channel was truncated from the grey at a different offset. Clouds
// keep one tint, rounded to the nearest step.
void sky_banding() {
    for (bool modern:{false,true}) for (int y=0;y<64;++y) for (int x=0;x<512;++x) {
        uint16_t p=cloud_pixel(x,y,modern);
        int r=(p>>11)&31,g=(p>>6)&31,b=(p>>1)&31;
        require(g==r && b==r+1 && (p&1),"storm clouds keep one blue-grey tint");
        require(std::abs(float(r)-cloud_level(x,y,modern))<=0.5f,"the sky rounds to the nearest step");
    }
}
int main(int argc,char** argv) {
    sky_banding();
    long_race();
    speed();
    equivalence();
    Triangle road{{Point{-1000,0,-1000},Point{2000,0,-1000},Point{-1000,0,2000}}};
    Triangle wall{{Point{0,0,0},Point{0,2000,0},Point{2000,0,0}}};
    Triangle roof=road;
    for (auto& p:roof.p) p.y=1000;
    require(floor(road) && !floor(wall),"only near-horizontal road faces receive water");
    std::vector<RoofPlane> planes; roof_planes({roof,wall},planes);
    require(planes.size()==1,"walls cannot become roof planes");
    require(covered({100,0,100},planes) && !covered({2500,0,2500},planes),"roof coverage respects triangle bounds");
    require(!covered({100,1200,100},planes),"a floor below the point is not a roof");
    require(puddles({road},{roof},true,0,1).empty(),"covered road has no puddles");
    require(puddles({wall},{},true,0,1).empty(),"walls cannot acquire puddles");
    require(puddles({road},{},true,0,0).empty(),"zero intensity emits nothing");
    auto retro=puddles({road},{},false,0,1),modern=puddles({road},{},true,0,1);
    require(!retro.empty() && modern.size()>retro.size(),"modern style adds soft water and ripple geometry");
    for (auto v:modern) require(v.p.y==0 && v.p.x>=-1000.01 && v.p.z>=-1000.01 && v.p.x+v.p.z<=1000.01,
        "puddles stay on and inside their source face");
    for (bool style:{false,true}) for (int y=0;y<64;++y)
        require(cloud_pixel(0,y,style)==cloud_pixel(512,y,style),"cloud panorama wraps continuously");
    std::vector<uint8_t> ram(0x800000);
    auto w=[&](uint32_t a,uint32_t v){ lambo::f3dex::write(ram.data(),a,v); };
    auto h=[&](uint32_t a,int16_t v){ std::memcpy(ram.data()+(a^2),&v,2); };
    w(0x100,0x04000C2F);w(0x104,0x80000200);
    for (int i=0;i<3;++i) { h(0x200+i*16,int16_t(road.p[i].x));h(0x202+i*16,0);h(0x204+i*16,int16_t(road.p[i].z)); }
    w(0x108,0xBF000000);w(0x10C,0x00000204);w(0x110,0xB8000000);
    std::vector<Triangle> mesh;
    require(road_mesh(ram.data(),0x80000100,mesh) && mesh.size()==1,"decodes actual F3DEX v1 vertices and triangle indices");
    require(mesh[0].p[1].x==2000 && mesh[0].p[2].z==2000,"guest halfword byte order is preserved");
    w(0x104,0x807FFFF0);
    require(!road_mesh(ram.data(),0x100,mesh) && mesh.empty(),"out-of-range DMA fails closed");
    w(0x104,0x80000200);w(0x10C,0x00000206);
    require(!road_mesh(ram.data(),0x100,mesh),"unloaded vertex is rejected");
    w(0x100,0x06000000);w(0x104,0x80000100);
    require(!road_mesh(ram.data(),0x100,mesh),"cyclic sublist terminates without partial geometry");
    w(0x100,0x01000000);
    require(!road_mesh(ram.data(),0x100,mesh),"unsupported transform cannot place puddles on guessed coordinates");
    if (argc==2) {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<uint8_t> capture((std::istreambuf_iterator<char>(file)),{});
        require(capture.size()==0x800000,"optional capture must contain exactly native RAM");
        auto read=[&](uint32_t a){return lambo::f3dex::read(capture.data(),a&0x7FFFFF);};
        uint32_t records=read(0x800BF1D0),header=read(0x80098238);
        unsigned n=(read(header+8)-read(header+4))/20,decoded=0,faces=0;
        require(n>0 && n<=200,"capture has valid track records");
        for (unsigned i=0;i<n;++i) if (road_mesh(capture.data(),read(records+i*64+4),mesh)) { ++decoded; faces+=unsigned(mesh.size()); }
        std::cout<<"Decoded "<<decoded<<" / "<<n<<" road lists, "<<faces<<" faces\n";
        require(decoded>0 && faces>0,"captured track has supported road geometry");
    }
}
