#include "lambo_rain.h"
#include "lambo_f3dex.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace lambo::rain {
uint32_t hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}
namespace {
float dot(Point a, Point b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Point cross(Point a, Point b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
float length(Point a) { return std::sqrt(dot(a,a)); }
bool address(uint32_t a, uint32_t bytes, uint32_t& offset) {
    if (a >= 0x80000000u && a < 0x80800000u) a -= 0x80000000u;
    if (a > 0x800000u || bytes > 0x800000u-a) return false;
    offset = a;
    return true;
}
struct Decoder {
    const uint8_t* ram;
    std::vector<Triangle>& triangles;
    Decoder(const uint8_t* memory,std::vector<Triangle>& out):ram(memory),triangles(out) {}
    std::array<Point,32> vertices{};
    uint32_t valid = 0;
    unsigned budget = 8192;
    bool tri(uint32_t w) {
        unsigned a=(w>>17)&127, b=(w>>9)&127, c=(w>>1)&127;
        if (a>=32 || b>=32 || c>=32 || !(valid&(1u<<a)) || !(valid&(1u<<b)) || !(valid&(1u<<c))) return false;
        if (triangles.size() >= 2048) return false;
        triangles.push_back({{vertices[a],vertices[b],vertices[c]}});
        return true;
    }
    bool run(uint32_t list, unsigned depth=0) {
        uint32_t offset;
        if (depth>8 || !address(list,8,offset) || (offset&7)) return false;
        while (offset <= 0x800000-8 && budget) {
            --budget;
            uint32_t w0=f3dex::read(ram,offset), w1=f3dex::read(ram,offset+4);
            switch (w0>>24) {
            case 0x04: {
                unsigned n=(w0>>10)&63, first=(w0>>17)&127;
                uint32_t source;
                if (!n || first+n>32 || !address(w1,n*16,source) || (source&1)) return false;
                for (unsigned i=0;i<n;++i) {
                    int16_t xyz[3];
                    for (int axis=0;axis<3;++axis) std::memcpy(&xyz[axis],ram+((source+i*16+axis*2)^2),2);
                    vertices[first+i]={float(xyz[0]),float(xyz[1]),float(xyz[2])};
                    valid |= 1u<<(first+i);
                }
                break;
            }
            case 0xBF: if (!tri(w1)) return false; break;
            case 0xB1: if (!tri(w0) || !tri(w1)) return false; break;
            case 0x06:
                if (!run(w1,depth+1)) return false;
                if (w0&0x10000) return true;
                break;
            case 0xB8: return true;
            // No inferred transform, branch, segment state or modified vertices.
            case 0x01: case 0x03: case 0xB0: case 0xB2: case 0xBC: case 0xBD: case 0xAF: return false;
            default: break; // Material commands do not alter positions.
            }
            offset += 8;
        }
        return false;
    }
};
}
bool road_mesh(const uint8_t* ram, uint32_t list, std::vector<Triangle>& triangles) {
    triangles.clear();
    if (!ram || list==0) return false;
    Decoder decoder{ram,triangles};
    if (decoder.run(list)) return true;
    triangles.clear();
    return false;
}
bool floor(const Triangle& t) {
    Point n=cross(t.p[1]-t.p[0],t.p[2]-t.p[0]);
    return length(n)>1 && std::abs(n.y)>length(n)*0.8f;
}
void roof_planes(const std::vector<Triangle>& triangles, std::vector<RoofPlane>& planes) {
    for (const auto& t:triangles) {
        if (!floor(t)) continue;
        Point a=t.p[1]-t.p[0], b=t.p[2]-t.p[0];
        float det=a.x*b.z-a.z*b.x;
        if (std::abs(det)>=1) planes.push_back({t.p[0],a,b,det});
    }
}
bool covered(Point p, const std::vector<RoofPlane>& roof) {
    for (const auto& t:roof) {
        Point a=t.a, b=t.b, c=p-t.origin;
        float u=(c.x*b.z-c.z*b.x)/t.det, v=(a.x*c.z-a.z*c.x)/t.det;
        if (u>=0 && v>=0 && u+v<=1 && t.origin.y+a.y*u+b.y*v>p.y+80) return true;
    }
    return false;
}
namespace {
constexpr int modern_rings=5;
constexpr uint32_t ring_vertices=3+(modern_rings-1)*6;
uint32_t color(uint32_t rgb,int a,float intensity) { return (rgb<<8)|uint32_t(a*intensity); }
}
Point Puddle::point(int j,float r) const {
    j%=sides;
    return center+direction[j]*(radius*r*shape[j]);
}
uint32_t ripple_vertex(const Puddle& p,int side) {
    return p.first+uint32_t(side)*(ring_vertices+6)+ring_vertices;
}
float ripple_phase(const Puddle& p,float seconds) {
    return std::fmod(seconds*0.7f+(p.seed%101)*0.01f,1.0f);
}
void ripple(const Puddle& p,int i,float phase,Vertex* out) {
    Point a0=p.point(i,phase*0.8f), b0=p.point(i+1,phase*0.8f);
    Point a1=p.point(i,phase*0.8f+0.025f), b1=p.point(i+1,phase*0.8f+0.025f);
    uint32_t c=color(0xB5CAD4,int((1-phase)*40),p.intensity);
    const Vertex quad[6]{{a0,c},{b0,c},{b1,c},{a0,c},{b1,c},{a1,c}};
    std::copy(quad,quad+6,out);
}
void puddle_mesh(const std::vector<Triangle>& road, const std::vector<RoofPlane>& roof,
        bool modern, float seconds, float intensity, PuddleMesh& mesh) {
    auto& out=mesh.vertices;
    out.clear(); mesh.ripples.clear();
    if (!std::isfinite(seconds) || !std::isfinite(intensity) || intensity<=0) return;
    intensity=std::min(intensity,1.0f);
    for (const auto& t:road) {
        if (out.size()+3>12000) break;
        if (!floor(t)) continue;
        Point center=(t.p[0]+t.p[1]+t.p[2])*(1.0f/3);
        if (covered(center,roof)) continue;
        // Stable face-space placement, independent of camera and frame ordering.
        uint32_t seed=hash(uint32_t(int(center.x)) ^ hash(uint32_t(int(center.z))));
        for (Point p:t.p) out.push_back({p,color(0x152531,modern?20:24,intensity)});
        if ((seed&3)!=0 || out.size()+400>12000) continue;
        Point u=t.p[1]-t.p[0], n=cross(u,t.p[2]-t.p[0]);
        u=u*(1/length(u)); n=n*(1/length(n));
        Point v=cross(n,u);
        float radius=280;
        for (int edge=0;edge<3;++edge) {
            Point a=t.p[edge], b=t.p[(edge+1)%3];
            radius=std::min(radius,length(cross(center-a,b-a))/length(b-a)*0.72f);
        }
        if (radius<20) continue;
        Puddle puddle;
        puddle.center=center; puddle.radius=radius; puddle.intensity=intensity;
        puddle.seed=seed; puddle.first=uint32_t(out.size()); puddle.sides=modern?ripple_sides:6;
        for (int j=0;j<puddle.sides;++j) {
            float angle=j*6.28318530718f/puddle.sides;
            puddle.direction[j]=u*std::cos(angle)+v*(std::sin(angle)*0.65f);
            puddle.shape[j]=0.93f+0.04f*std::sin(j*3.0f+float(seed%17));
        }
        for (int i=0;i<puddle.sides;++i) {
            // Constant alpha per ring leaves SHADE alpha available to the
            // native fog blender. Rings provide soft edges without replacing
            // that fog with a screen-space dark overlay.
            const int rings=modern?modern_rings:1;
            for (int ring=0;ring<rings;++ring) {
                float inner=float(ring)/rings, outer=float(ring+1)/rings;
                Point a0=puddle.point(i,inner),b0=puddle.point(i+1,inner);
                Point a1=puddle.point(i,outer),b1=puddle.point(i+1,outer);
                uint32_t c=color(0x8297A4,modern?int(52*(1-outer*0.9f)):55,intensity);
                out.insert(out.end(),{{a0,c},{a1,c},{b1,c}});
                if (ring) out.insert(out.end(),{{a0,c},{b1,c},{b0,c}});
            }
            if (modern) {
                out.resize(out.size()+6);
                ripple(puddle,i,ripple_phase(puddle,seconds),&out[out.size()-6]);
            }
        }
        if (modern) mesh.ripples.push_back(puddle);
    }
}
std::vector<Vertex> puddles(const std::vector<Triangle>& road,
        const std::vector<Triangle>& roof, bool modern, float seconds, float intensity) {
    std::vector<RoofPlane> planes;
    roof_planes(roof,planes);
    PuddleMesh mesh;
    puddle_mesh(road,planes,modern,seconds,intensity,mesh);
    return std::move(mesh.vertices);
}
namespace {
float unit(uint32_t seed) { return float(seed%10007)/10007; }
uint32_t screen_color(uint32_t rgb,float alpha) { return (rgb<<8)|uint32_t(std::clamp(alpha,0.0f,255.0f)); }
// Appends one shape; its triangles index its own vertices.
class Shape {
public:
    explicit Shape(ScreenMesh& mesh):mesh_(mesh) {
        shape_.first=uint32_t(mesh.vertices.size());
        shape_.first_index=uint32_t(mesh.indices.size());
    }
    ~Shape() {
        shape_.count=uint32_t(mesh_.vertices.size())-shape_.first;
        shape_.index_count=uint32_t(mesh_.indices.size())-shape_.first_index;
        mesh_.shapes.push_back(shape_);
    }
    uint8_t vertex(float x,float y,uint32_t rgba) {
        mesh_.vertices.push_back({x,y,rgba});
        return uint8_t(mesh_.vertices.size()-1-shape_.first);
    }
    void triangle(uint8_t a,uint8_t b,uint8_t c) { mesh_.indices.insert(mesh_.indices.end(),{a,b,c}); }
private:
    ScreenMesh& mesh_;
    ScreenShape shape_;
};
// Motion-blurred streak: an opaque head at (x,y) tapering to a clear tail at (x+dx,y+dy).
void streak(ScreenMesh& mesh,float x,float y,float dx,float dy,float width,uint32_t rgb,float alpha) {
    float length=std::sqrt(dx*dx+dy*dy);
    if (!(length>0)) return;
    float nx=-dy/length*width/2,ny=dx/length*width/2;
    uint32_t head=screen_color(rgb,alpha),tail=screen_color(rgb,0);
    Shape s(mesh);
    uint8_t a=s.vertex(x+dx-nx*0.3f,y+dy-ny*0.3f,tail),b=s.vertex(x+dx+nx*0.3f,y+dy+ny*0.3f,tail);
    uint8_t c=s.vertex(x+nx,y+ny,head),d=s.vertex(x-nx,y-ny,head);
    s.triangle(a,b,c); s.triangle(a,c,d);
}
// Elliptical fan shaded from centre to rim. feather>1 adds a clear outer ring
// at that scale, so the edge fades instead of ending in a hard line.
void blob(ScreenMesh& mesh,float x,float y,float rx,float ry,int sides,uint32_t center,uint32_t rim,float feather) {
    Shape s(mesh);
    uint8_t c=s.vertex(x,y,center);
    const int rings=feather>1 ? 2 : 1;
    for (int ring=0;ring<rings;++ring) for (int i=0;i<sides;++i) {
        float angle=float(i)*6.28318530718f/float(sides),scale=ring ? feather : 1;
        s.vertex(x+std::cos(angle)*rx*scale,y+std::sin(angle)*ry*scale,ring ? rim&0xFFFFFF00u : rim);
    }
    for (int i=0;i<sides;++i) {
        uint8_t a=uint8_t(1+i),b=uint8_t(1+(i+1)%sides);
        s.triangle(c,a,b);
        if (rings==2) { s.triangle(a,uint8_t(a+sides),uint8_t(b+sides)); s.triangle(a,uint8_t(b+sides),b); }
    }
}
// The drop volume, in metres in front of the camera. Far, middle and near
// slabs: nearer streaks are longer, faster, wider and brighter. At rest the
// slabs reproduce the old flat layers (about 150, 250 and 400 native px/s
// falls, 8-27 px streaks at 240 lines). Stylized, not the game's camera FOV.
constexpr float tan_half_fov=0.6f;  // vertical
constexpr float fall_speed=9;       // terminal velocity of rain
constexpr float wind=0.17f*fall_speed; // streaks fall to the right, tails trail up-left
constexpr float exposure=0.06f;     // motion-blur time that sets streak length
constexpr float visible_streaks=250; // at full intensity in a 4:3 one-player view
struct Layer {
    float share,near,far,width,alpha;
    // The box tiles sideways, so any view width is covered; vertically it
    // covers the frustum at the far edge, with room for streak tails.
    constexpr float height() const { return 2*far*tan_half_fov*1.25f; }
    constexpr float width_box() const { return height()*4/3; }
    constexpr float depth() const { return far-near; }
    // Drops in the box, so a 4:3 view sees share * visible_streaks of them.
    constexpr int drops() const {
        float seen=(far*far*far-near*near*near)/(3*far*far*depth()*1.25f*1.25f);
        return int(visible_streaks*share/seen);
    }
};
constexpr Layer layers[3]{{0.45f,10,16,0.30f,70},{0.35f,5.5f,9,0.42f,95},{0.20f,3.2f,5.2f,0.62f,125}};
float wrap(float value,float period) {
    float r=value-period*std::floor(value/period);
    return r<period ? r : 0; // floor rounding can land exactly on the period
}
// A forward-moving drop that passes the near edge reappears at the far edge.
// Fading both edges hides that jump; at rest nothing jumps, so nothing fades.
float depth_fade(const Layer& l,float z,float forward) {
    float edge=std::clamp(std::min(z-l.near,l.far-z)/(0.25f*l.depth()),0.0f,1.0f);
    return 1-std::clamp(std::abs(forward)/4,0.0f,1.0f)*(1-edge);
}
}
void advance(Flow& flow,Motion target,float dt) {
    if (!(dt>0) || !std::isfinite(dt) || !std::isfinite(target.forward) || !std::isfinite(target.yaw)) return;
    // Ease toward the car's motion so a warp or a crash does not snap the rain.
    float k=std::min(dt/0.25f,1.0f);
    flow.motion.forward+=(target.forward-flow.motion.forward)*k;
    flow.motion.yaw+=(target.yaw-flow.motion.yaw)*k;
    for (int i=0;i<3;++i) {
        const Layer& l=layers[i];
        auto& slab=flow.slabs[i];
        // Turning sweeps the world sideways; at the slab's middle depth that is yaw * depth.
        float side=wind-flow.motion.yaw*(l.near+l.far)/2;
        slab.fall=wrap(slab.fall+fall_speed*dt,l.height());
        slab.approach=wrap(slab.approach+flow.motion.forward*dt,l.depth());
        slab.drift=wrap(slab.drift+side*dt,l.width_box());
    }
}
void screen_rain(float width,float height,int player,float seconds,const Flow& flow,float intensity,
        bool droplets,ScreenMesh& mesh) {
    mesh.vertices.clear(); mesh.indices.clear(); mesh.shapes.clear();
    if (!(width>0) || !(height>0) || !std::isfinite(seconds) || !(intensity>0)) return;
    intensity=std::min(intensity,1.0f);
    const float s=height/240; // split-screen views are half height
    // Count follows view area up to 16:9; wider outputs keep that count, so the
    // camera buffer bound holds and rain is sparser there. A fixed subset of
    // the drops gives it: a taller view's frustum sees more of the volume.
    constexpr float widest_area=(16.0f/9)/(4.0f/3);
    const float area=std::min(width*height/(320.0f*240),widest_area);
    const float subset=intensity*area/((width/height)/(4.0f/3));
    // Variance above the expected count is bounded so the worst case still fits the camera buffer.
    const int limit=int(visible_streaks*intensity*area*1.15f);
    const float f=height/2/tan_half_fov,cx=width/2,cy=height/2;
    const float forward=flow.motion.forward,yaw=flow.motion.yaw;
    int drawn=0;
    for (int index=0;index<3;++index) {
        const Layer& l=layers[index];
        const auto& slab=flow.slabs[index];
        const float box=l.width_box(),tall=l.height();
        const int drops=int(float(l.drops())*subset);
        for (int i=0;i<drops && drawn<limit;++i) {
            uint32_t seed=hash(uint32_t(i)*0x9E3779B9u+uint32_t(player)*0x85EBCA6Bu+uint32_t(index)*0x632BE5ABu+1);
            float z=l.near+wrap(unit(hash(seed+3))*l.depth()-slab.approach,l.depth());
            float y=tall/2-wrap(unit(hash(seed+2))*tall+slab.fall,tall);
            float x=wrap(unit(hash(seed+1))*box+slab.drift,box)-box/2;
            // Where the drop was one exposure ago, relative to the moving camera.
            float vx=wind-yaw*z,vy=-fall_speed,vz=-forward;
            float tz=std::clamp(z-vz*exposure,z*0.55f,z*1.8f);
            float tx=x-vx*exposure,ty=y-vy*exposure;
            float alpha=l.alpha*(0.7f+0.3f*unit(hash(seed+5)))*(0.75f+0.25f*intensity)*depth_fade(l,z,forward);
            float hy=cy-f*y/z,dy=cy-f*ty/tz-hy;
            // The box repeats sideways; draw every copy that reaches the view.
            // Head and tail screen x both grow with the copy's offset, so start
            // at the first copy whose head or tail can reach the left edge.
            float left=std::min((-2-cx)*z/f,(-2-cx)*tz/f-(tx-x));
            for (float copy=x-box*std::ceil((x-left)/box);drawn<limit;copy+=box) {
                float hx=cx+f*copy/z,dx=cx+f*(copy+tx-x)/tz-hx;
                if (std::min(hx,hx+dx)>width+2) break;
                if (std::max(hx,hx+dx)<-2 || std::max(hy,hy+dy)<-2 || std::min(hy,hy+dy)>height+2) continue;
                // Very near drops at speed would streak across the view; cap the smear.
                float length=std::sqrt(dx*dx+dy*dy),most=height*0.35f;
                float scale=length>most ? most/length : 1;
                streak(mesh,hx,hy,dx*scale,dy*scale,l.width*std::max(s,0.75f),0xD2DEEA,alpha);
                ++drawn;
            }
        }
    }
    if (!droplets) return;
    const int drops=int(10+8*intensity);
    for (int i=0;i<drops;++i) {
        uint32_t seed=hash(uint32_t(i)*0x27D4EB2Fu+uint32_t(player)*0x165667B1u+900);
        float age=std::fmod(seconds+unit(seed)*10,10.0f);
        float fade=std::min(age/0.6f,1.0f)*std::min((10-age)/1.5f,1.0f)*(0.55f+0.45f*intensity);
        float rx=s*(2.0f+3.6f*unit(hash(seed+1))*unit(hash(seed+2))),ry=rx*1.08f;
        // A quarter of the drops grow heavy and run down the lens, leaving a trail.
        float slide=(seed&3)==0 ? std::pow(std::max(0.0f,age-4),2.0f)*1.6f*s : 0;
        float x=unit(hash(seed+3))*width,y=unit(hash(seed+4))*height+slide;
        if (slide>ry) streak(mesh,x,y-ry*0.6f,0,-(slide-ry*0.6f),rx*0.55f,0xA8C0D0,40*fade);
        // Dark refracting rim, lighter centre, a caustic below and a specular glint above.
        blob(mesh,x,y,rx,ry,12,screen_color(0xC6DCEA,26*fade),screen_color(0x1E2E3C,88*fade),1.2f);
        blob(mesh,x+rx*0.12f,y+ry*0.5f,rx*0.5f,ry*0.25f,8,screen_color(0xE6F4FF,90*fade),screen_color(0xE6F4FF,0),0);
        blob(mesh,x-rx*0.38f,y-ry*0.42f,rx*0.3f,ry*0.3f,8,screen_color(0xFFFFFF,210*fade),screen_color(0xFFFFFF,0),0);
    }
}
float cloud_level(int x,int y,bool modern) {
    // Periodic panoramic noise: eight 64-pixel tiles, two 32-pixel rows.
    if (!modern) { x=(x/4)*4; y=(y/3)*3; }
    float noise=0, amplitude=1, total=0;
    for (int scale:{128,64,32,16}) {
        float fx=float(x)/scale, fy=float(y)/scale;
        int ix=int(std::floor(fx)), iy=int(std::floor(fy));
        float u=fx-ix,v=fy-iy;
        u=u*u*(3-2*u); v=v*v*(3-2*v);
        auto sample=[&](int dx,int dy) {
            return float(hash(uint32_t((ix+dx)%(512/scale))+uint32_t(iy+dy)*719)&255)/255;
        };
        noise+=amplitude*((sample(0,0)*(1-u)+sample(1,0)*u)*(1-v)+(sample(0,1)*(1-u)+sample(1,1)*u)*v);
        total+=amplitude; amplitude*=0.5f;
    }
    float horizon=std::clamp(float(y)/63,0.0f,1.0f);
    return float(int(25+noise/total*48+horizon*20))/8;
}
uint16_t cloud_pixel(int x,int y,bool modern) {
    // RGBA16 holds about nine steps of this grey range. Every channel takes the
    // same step, so the clouds keep one blue-grey tint: rounding each channel at
    // its own grey painted green and blue bands. Soft grey steps remain; in game
    // a dither read as a hatch and showed each sky tile's clamped edge.
    int level=std::clamp(int(cloud_level(x,y,modern)+0.5f),0,30);
    return uint16_t((level<<11)|(level<<6)|((level+1)<<1)|1);
}
}
