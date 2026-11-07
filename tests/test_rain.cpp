#include "lambo_rain.h"
#include "lambo_f3dex.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace lambo::rain;
void require(bool condition,const char* message) {
    if (!condition) { std::cerr<<message<<'\n'; std::exit(1); }
}
int main(int argc,char** argv) {
    Triangle road{{Point{-1000,0,-1000},Point{2000,0,-1000},Point{-1000,0,2000}}};
    Triangle wall{{Point{0,0,0},Point{0,2000,0},Point{2000,0,0}}};
    Triangle roof=road;
    for (auto& p:roof.p) p.y=1000;
    require(floor(road) && !floor(wall),"only near-horizontal road faces receive water");
    require(covered({100,0,100},{roof}) && !covered({2500,0,2500},{roof}),"roof coverage respects triangle bounds");
    require(!covered({100,1200,100},{roof}),"a floor below the point is not a roof");
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
