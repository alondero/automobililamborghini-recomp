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
uint16_t cloud_pixel(int x,int y,bool modern) {
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
    int grey=int(25+noise/total*48+horizon*20);
    return uint16_t(((grey/8)<<11)|(((grey+4)/8)<<6)|(((grey+10)/8)<<1)|1);
}
}
