// Synthetic guest memory, real SDK Assemble/texture decoders and production Backend.
#include "../commands.h"
#include "gpu/backend.h"
#include "gpu/vertex.h"
#include "gpu/texture.h"
#include "kernel/kernel.h"
#include "core/log.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
namespace { std::vector<uint8_t> memory(1<<20); }
namespace kernel { uint8_t* g_base=memory.data(); }
namespace mem {
bool IsCommitted(uint32_t va) { return va<memory.size(); }
uint32_t VirtualFromPhysical(uint32_t pa) { return pa; }
}
namespace logx { void Write(Level,const char*,...) {} }
using namespace pgr::gldirect;
int main() {
    auto header=[](uint32_t h,uint32_t data,uint32_t fmt) {
        kernel::W32(h,0x50000); kernel::W32(h+4,data);
        kernel::W32(h+12,fmt<<8); kernel::W32(h+16,31|(31<<12)|(1u<<24));
    };
    header(0x1000,0x10000,0x12); header(0x1100,0x20000,0x2e);
    gpu::State s; s.rt={0x1000,0x10000}; s.zs={0x1100,0x20000}; s.viewport={0,0,32,32,0,1};
    s.vs_handle=0x44; // XYZRHW + DIFFUSE
    s.streams[0]={0x1200,20}; kernel::W32(0x1204,0x30000);
    struct GuestVertex { float x,y,z,rhw; uint32_t color; };
    GuestVertex vertices[]={{0,0,.5,1,0xffff0000},{31,0,.5,1,0xff00ff00},{31,31,.5,1,0xff0000ff},{0,31,.5,1,0xffffffff}};
    std::memcpy(kernel::HostPtr(0x30000),vertices,sizeof(vertices));
    for(auto& stage:s.tss) stage[gpu::kTssColorOp]=1;
    s.simple[gpu::kMColorWriteEnable/4]=0x01010101;
    s.simple[gpu::kMZFunc/4]=0x201;
    auto backend=gpu::CreateBackend(); assert(backend->Init(32,32));
    assert(!gpu::g_raw_vertices);
    backend->Clear(s,0,nullptr,0xf3,0xff102030,1,0);
    // Guest QUADLIST must turn into an owned triangle index list.
    backend->Draw(s,8,0,4,nullptr); backend->Present(s.rt);
    Submission f; assert(Take(f)); assert(f.commands.size()==2 && f.unsupported==0);
    const auto& c=f.commands[1]; assert(c.vertices.size()==4 && c.indices.size()==6 && c.topology==4);
    assert(c.indices==std::vector<uint32_t>({0,1,2,0,2,3}));
    assert(c.vertices[0].color[0]==1 && c.vertices[0].color[2]==0);
    assert(f.commands[0].rects[0].w==32 && f.front==0x10000);
    // Producer memory can change immediately after Draw, including a 16-bit index buffer.
    uint16_t ix[]={3,2,1}; backend->Draw(s,5,0,3,reinterpret_cast<uint8_t*>(ix));
    ix[0]=0; std::memset(kernel::HostPtr(0x30000),0,sizeof(vertices)); backend->Present(s.rt);
    assert(Take(f)); assert(f.commands[0].vertices[0].position[0]==31); // dense span starts at original vertex 1
    assert(f.commands[0].indices==std::vector<uint32_t>({2,1,0}));
    assert(f.commands[0].vertices[2].position[1]==31);
    // Exercise the actual SDK's Xbox linear texture decoder and texel-coordinate normalization.
    std::memcpy(kernel::HostPtr(0x30000),vertices,sizeof(vertices));
    header(0x1300,0x40000,0x12); kernel::W32(0x1300,0x40000);
    for(unsigned i=0;i<32*32;i++) kernel::W32(0x40000+i*4,0xff123456);
    s.textures[0]=0x1300; auto& t=s.tss[0];
    t[gpu::kTssColorOp]=t[gpu::kTssAlphaOp]=4; t[gpu::kTssColorArg1]=t[gpu::kTssAlphaArg1]=2;
    t[gpu::kTssAddressU]=t[gpu::kTssAddressV]=3; t[gpu::kTssMinFilter]=t[gpu::kTssMagFilter]=1;
    backend->Draw(s,8,0,4,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==1 && f.commands[0].texture.bgra.size()==1024);
    assert(f.commands[0].texture.bgra[0]==0xff123456);
    // Unsupported programmable work is explicitly counted and never replaced with a fake draw.
    s.vs_handle=1; backend->Draw(s,5,0,3,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.empty() && f.unsupported==1);
    std::puts("SDK capture tests passed: FVF colors, quad topology, 16-bit indices, owned guest data, texture decode, unsupported shaders.");
    Stop();
}
