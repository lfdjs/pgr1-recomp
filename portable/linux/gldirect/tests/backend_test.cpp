// Synthetic guest memory, real SDK Assemble/texture decoders and production Backend.
#include "../commands.h"
#include "gpu/backend.h"
#include "gpu/vertex.h"
#include "gpu/texture.h"
#include "kernel/kernel.h"
#include "core/log.h"
#include "app/app.h"
#include <cassert>
#include <bit>
#include <bit>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
namespace { std::vector<uint8_t> memory(1<<20); }
namespace kernel { uint8_t* g_base=memory.data(); }
namespace mem {
bool IsCommitted(uint32_t va) { return va<memory.size(); }
uint32_t VirtualFromPhysical(uint32_t pa) { return pa; }
}
namespace app { const GameInfo kGame{}; float FrameAspect() { return 4.0f/3; } Settings& Get() { static Settings s; return s; } }
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
    // size=0 is an exact resource lookup, not a one-byte overlapping range.
    backend->Sync(s.rt.data+4,0); assert(!Take(f));
    std::thread sync([&] { backend->Sync(s.rt.data,0); });
    while(!Take(f)) std::this_thread::yield();
    assert(!f.present && f.commands.back().kind==Command::Kind::Readback);
    f.commands.back().readback->set_value(std::vector<uint32_t>(1024,0xffabcdef)); sync.join();
    assert(kernel::R32(s.rt.data)==0xffabcdef);
    backend->Sync(s.rt.data,0); assert(!Take(f)); // no new GPU writes: no redundant stall
    backend->SyncLate(s.rt.data); backend->Flush(); assert(Take(f));
    f.commands.back().readback->set_value(std::vector<uint32_t>(1024,0xff123456));
    backend->SyncLate(s.rt.data); assert(kernel::R32(s.rt.data)==0xff123456);
    backend->Flush(); assert(Take(f)); f.commands.back().readback->set_value(std::vector<uint32_t>(1024,0xff123456));
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
    t[gpu::kTssAddressW]=2; t[gpu::kTssBorderColor]=0x80402010;
    t[gpu::kTssMipLodBias]=std::bit_cast<uint32_t>(-.75f);
    t[gpu::kTssMaxMipLevel]=1; t[gpu::kTssMaxAnisotropy]=8;
    backend->Draw(s,8,0,4,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==1 && f.commands[0].textures[0].pixels->levels[0].size()==1024);
    assert(f.commands[0].textures[0].pixels->levels[0][0]==0xff123456);
    const auto& sampler=f.commands[0].textures[0];
    assert(sampler.wrap_w==2 && sampler.border_color==0x80402010);
    assert(sampler.lod_bias==-.75f && sampler.max_mip==1 && sampler.max_anisotropy==8);
    // Exact cache validation must detect unmarked CPU writes even within a
    // single frame. Previously submitted draws retain the old immutable pixels.
    backend->Draw(s,8,0,4,nullptr);
    kernel::W32(0x40000+17*4,0xfffedcba);
    backend->Draw(s,8,0,4,nullptr);
    backend->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==2);
    assert(f.commands[0].textures[0].pixels->levels[0][17]==0xff123456);
    assert(f.commands[1].textures[0].pixels->levels[0][17]==0xfffedcba);
    // Unchanged bytes share the decoded allocation, without rescanning a hash.
    const auto retained=f.commands[1].textures[0].pixels;
    backend->Draw(s,8,0,4,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands[0].textures[0].pixels==retained);
    // Console depth-bias units depend on the physical depth format and apply
    // to triangles only. D16 uses65535 steps instead of D24's16777215.
    s.simple[gpu::kMSolidOffsetEnable/4]=1;
    s.simple[gpu::kMDepthOffset/4]=std::bit_cast<uint32_t>(2.0f);
    s.zscale=65535;
    header(0x1100,0x20000,0x30);
    backend->Draw(s,8,0,4,nullptr); backend->Draw(s,4,0,4,nullptr);
    backend->Present(s.rt); assert(Take(f));
    assert(f.commands[0].pipeline.depth_bias==2 && f.commands[1].pipeline.depth_bias==0);
    s.simple[gpu::kMSolidOffsetEnable/4]=0;
    header(0x1100,0x20000,0x2e);
    // A cube sharing face 0 with a GPU surface needs face composition, not a 2D bind.
    kernel::W32(0x1304,s.rt.data); kernel::W32(0x130c,(0x06u<<8)|4|(5u<<20)|(5u<<24)); kernel::W32(0x1310,0);
    backend->Draw(s,8,0,4,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==1 && f.commands[0].textures[0].cube);
    assert(f.commands[0].textures[0].render_target==0 && f.commands[0].textures[0].face_targets[0]==s.rt.data);
    // Unsupported programmable work is explicitly counted and never replaced with a fake draw.
    s.vs_handle=1; backend->Draw(s,5,0,3,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.empty() && f.unsupported==1);
    s.vs=std::make_shared<gpu::VertexShader>(); gpu::FvfInputs(0x44,*s.vs);
    s.vs->program={0,(1u<<21)|0x1b,2u<<26,(15u<<12)|(1u<<11)|1};
    s.vs->constants.push_back({100,{1,1,1,1}}); s.vsc[100]={2,3,4,5};
    backend->Draw(s,5,0,3,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==1 && f.commands[0].vertex_constants[100][0]==2);
    s.vs=std::make_shared<gpu::VertexShader>(); s.vs->program={0,14u<<21,0,1};
    backend->Draw(s,5,0,3,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.empty() && f.unsupported==2); // unknown opcode is recorded, never rendered as zero
    std::puts("SDK capture tests passed: FVF colors, quad topology, 16-bit indices, owned guest data, texture decode, unsupported shaders.");
    Stop();
}
