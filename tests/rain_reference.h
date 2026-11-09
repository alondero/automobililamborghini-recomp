#pragma once
// Frozen copy of the uncached rain producer at e053389 (issue #397). Tests and
// the benchmark compare the cached implementation against these bytes; do not
// "fix" this file when the production code changes.
#include "lambo_f3dex.h"
#include "lambo_rain.h"
#include "rt64_extended_gbi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace reference {
using lambo::rain::Point;
using lambo::rain::Triangle;
using lambo::rain::Vertex;
inline uint32_t hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}
inline float dot(Point a, Point b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Point cross(Point a, Point b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
inline float length(Point a) { return std::sqrt(dot(a,a)); }
inline bool address(uint32_t a, uint32_t bytes, uint32_t& offset) {
    if (a >= 0x80000000u && a < 0x80800000u) a -= 0x80000000u;
    if (a > 0x800000u || bytes > 0x800000u-a) return false;
    offset = a;
    return true;
}
struct Decoder {
    const uint8_t* ram;
    explicit Decoder(const uint8_t* memory):ram(memory) {}
    std::array<Point,32> vertices{};
    uint32_t valid = 0;
    unsigned budget = 8192;
    std::vector<Triangle> triangles;
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
            uint32_t w0=lambo::f3dex::read(ram,offset), w1=lambo::f3dex::read(ram,offset+4);
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
            case 0x01: case 0x03: case 0xB0: case 0xB2: case 0xBC: case 0xBD: case 0xAF: return false;
            default: break;
            }
            offset += 8;
        }
        return false;
    }
};
inline bool road_mesh(const uint8_t* ram, uint32_t list, std::vector<Triangle>& triangles) {
    triangles.clear();
    if (!ram || list==0) return false;
    Decoder decoder{ram};
    if (!decoder.run(list)) return false;
    triangles=std::move(decoder.triangles);
    return true;
}
inline bool floor(const Triangle& t) {
    Point n=cross(t.p[1]-t.p[0],t.p[2]-t.p[0]);
    return length(n)>1 && std::abs(n.y)>length(n)*0.8f;
}
inline bool covered(Point p, const std::vector<Triangle>& roof) {
    for (const auto& t:roof) {
        if (!reference::floor(t)) continue;
        Point a=t.p[1]-t.p[0], b=t.p[2]-t.p[0], c=p-t.p[0];
        float det=a.x*b.z-a.z*b.x;
        if (std::abs(det)<1) continue;
        float u=(c.x*b.z-c.z*b.x)/det, v=(a.x*c.z-a.z*c.x)/det;
        if (u>=0 && v>=0 && u+v<=1 && t.p[0].y+a.y*u+b.y*v>p.y+80) return true;
    }
    return false;
}
inline std::vector<Vertex> puddles(const std::vector<Triangle>& road,
        const std::vector<Triangle>& roof, bool modern, float seconds, float intensity) {
    std::vector<Vertex> out;
    if (!std::isfinite(seconds) || !std::isfinite(intensity) || intensity<=0) return out;
    intensity=std::min(intensity,1.0f);
    auto color=[&](uint32_t rgb,int a) { return (rgb<<8)|uint32_t(a*intensity); };
    for (const auto& t:road) {
        if (out.size()+3>12000) break;
        if (!reference::floor(t)) continue;
        Point center=(t.p[0]+t.p[1]+t.p[2])*(1.0f/3);
        if (reference::covered(center,roof)) continue;
        uint32_t seed=hash(uint32_t(int(center.x)) ^ hash(uint32_t(int(center.z))));
        for (Point p:t.p) out.push_back({p,color(0x152531,modern?20:24)});
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
        const int sides=modern?12:6;
        for (int i=0;i<sides;++i) {
            auto point=[&](int j,float r) {
                j%=sides;
                float angle=j*6.28318530718f/sides;
                float shape=0.93f+0.04f*std::sin(j*3.0f+float(seed%17));
                return center+(u*std::cos(angle)+v*(std::sin(angle)*0.65f))*(radius*r*shape);
            };
            const int rings=modern?5:1;
            for (int ring=0;ring<rings;++ring) {
                float inner=float(ring)/rings, outer=float(ring+1)/rings;
                Point a0=point(i,inner),b0=point(i+1,inner);
                Point a1=point(i,outer),b1=point(i+1,outer);
                uint32_t c=color(0x8297A4,modern?int(52*(1-outer*0.9f)):55);
                out.insert(out.end(),{{a0,c},{a1,c},{b1,c}});
                if (ring) out.insert(out.end(),{{a0,c},{b1,c},{b0,c}});
            }
            if (modern) {
                float phase=std::fmod(seconds*0.7f+(seed%101)*0.01f,1.0f);
                Point a0=point(i,phase*0.8f), b0=point(i+1,phase*0.8f);
                Point a1=point(i,phase*0.8f+0.025f), b1=point(i+1,phase*0.8f+0.025f);
                uint32_t c=color(0xB5CAD4,int((1-phase)*40));
                out.insert(out.end(),{{a0,c},{b0,c},{b1,c},{a0,c},{b1,c},{a1,c}});
            }
        }
    }
    return out;
}
// Roof triangles exactly as lambo_rain_road gathered them from record +8/+12.
inline std::vector<Triangle> roofs(const uint8_t* ram, uint32_t wall, uint32_t scenery) {
    std::vector<Triangle> result,part;
    for (uint32_t list:{wall,scenery})
        if (reference::road_mesh(ram,list,part))
            for (auto t:part) if (reference::floor(t)) result.push_back(t);
    return result;
}

// Guest-visible road sublist the e053389 producer wrote for `vertices` in a
// buffer at `buffer`: halfword vertex fields, then the command words from
// buffer+12000*16 through the terminating 0xB8.
struct Encoded {
    std::vector<int16_t> halves;    // x, y, z, 0 per vertex
    std::vector<uint32_t> colors;   // rgba per vertex (word +12; word +8 is zero)
    std::vector<uint32_t> commands; // w0, w1 pairs
};
inline Encoded encode(const std::vector<Vertex>& vertices, uint32_t buffer) {
    Encoded e;
    auto cmd=[&](uint32_t a,uint32_t b=0){ e.commands.push_back(a); e.commands.push_back(b); };
    auto ext=[&](uint32_t op){ cmd((RT64_EXTENDED_OPCODE<<24)|op); };
    cmd(0xE7000000);
    ext(G_EX_PUSHOTHERMODE_V1); ext(G_EX_PUSHCOMBINE_V1); ext(G_EX_PUSHPRIMCOLOR_V1);
    ext(G_EX_PUSHGEOMETRYMODE_V1);
    cmd(0xBA000020,0x00100000); cmd(0xB9000020,0xC8104A50); cmd(0xFCFFFFFF,0xFFFE773B);
    cmd(0xB6000000,0xFFFFFFFF); cmd(0xB7000000,0x10205);
    uint32_t at=buffer;
    for (size_t i=0;i<vertices.size();++i,at+=16) {
        auto v=vertices[i];
        e.halves.insert(e.halves.end(),{int16_t(std::lround(v.p.x)),int16_t(std::lround(v.p.y)),
            int16_t(std::lround(v.p.z)),0});
        e.colors.push_back(v.rgba);
        if (i%3==2) {
            cmd(0xE7000000);
            cmd(0xFA000000,0xFFFFFF00u|((vertices[i-2].rgba&255)+(vertices[i-1].rgba&255)+(v.rgba&255))/3);
            cmd(0x04400C2F,at-32);
            cmd(0xBF000000,(32u<<17)|(33u<<9)|(34u<<1));
        }
    }
    cmd(0xE7000000);
    ext(G_EX_POPGEOMETRYMODE_V1); ext(G_EX_POPPRIMCOLOR_V1); ext(G_EX_POPCOMBINE_V1);
    ext(G_EX_POPOTHERMODE_V1);
    cmd(0xB8000000);
    return e;
}
}
