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
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <cstdlib>
#if defined(PGR1_NATIVE_VULKAN) && defined(__linux__) && UINTPTR_MAX > UINT32_MAX
#include "kernel/memory.h"
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace { std::vector<uint8_t> memory(1<<20); }
namespace kernel { uint8_t* g_base=memory.data(); }
namespace mem {
bool IsCommitted(uint32_t va) { return va<memory.size(); }
uint32_t VirtualFromPhysical(uint32_t pa) { return pa; }
}
namespace app { const GameInfo kGame{}; float FrameAspect() { return 4.0f/3; } Settings& Get() { static Settings s; return s; } }
namespace logx { void Write(Level,const char*,...) {} }
using namespace pgr::gldirect;
#ifdef PGR1_NATIVE_VULKAN
namespace {
class ScopedEnvironment {
    const char* name_;
    bool present_;
    std::string previous_;
public:
    ScopedEnvironment(const char* name,const char* value):name_(name),present_(std::getenv(name)!=nullptr),
        previous_(present_?std::getenv(name):"") { setenv(name_,value,1); }
    ~ScopedEnvironment() { if(present_)setenv(name_,previous_.c_str(),1); else unsetenv(name_); }
};
#if defined(__linux__) && UINTPTR_MAX > UINT32_MAX
class AliasedGuestMemory {
    static constexpr std::size_t kGuestBytes=std::size_t{1}<<32;
    uint8_t* previous_=kernel::g_base;
    uint8_t* mapped_=nullptr;
public:
    AliasedGuestMemory() {
        // EN: Match the SDK's shared physical backing, including host RAM.
        //     The low synthetic headers/vertices retain their original layout.
        // PT-BR: Reproduza o backing físico compartilhado do SDK, incluindo RAM
        //        host. Headers/vértices sintéticos baixos mantêm seu layout.
        void* reserved=mmap(nullptr,kGuestBytes,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
        assert(reserved!=MAP_FAILED);
        mapped_=static_cast<uint8_t*>(reserved);
        assert(mprotect(mapped_,memory.size(),PROT_READ|PROT_WRITE)==0);
        std::memcpy(mapped_,previous_,memory.size());
        const int fd=static_cast<int>(syscall(SYS_memfd_create,"pgr1-backend-aliases",0));
        assert(fd>=0 && ftruncate(fd,mem::kMappedPhysical)==0);
        for(uint32_t alias:{mem::kContiguousBase,mem::kWriteCombineBase}) {
            void* view=mmap(mapped_+alias,mem::kMappedPhysical,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fd,0);
            assert(view==mapped_+alias);
        }
        close(fd);
        kernel::g_base=mapped_;
    }
    ~AliasedGuestMemory() { kernel::g_base=previous_; assert(munmap(mapped_,kGuestBytes)==0); }
    AliasedGuestMemory(const AliasedGuestMemory&)=delete;
    AliasedGuestMemory& operator=(const AliasedGuestMemory&)=delete;
};

void TestAliasedTextureCoherency() {
    AliasedGuestMemory guest;
    constexpr uint32_t data=mem::kContiguousBase+0x40000;
    constexpr uint32_t alias=mem::kWriteCombineBase+0x40000;
    auto header=[](uint32_t h,uint32_t address) {
        kernel::W32(h,0x40000); kernel::W32(h+4,address);
        kernel::W32(h+12,0x12u<<8); kernel::W32(h+16,31u|(31u<<12)|(1u<<24));
    };
    gpu::State state;
    state.rt={0x1000,0x10000}; state.zs={0x1100,0x20000};
    state.viewport={0,0,32,32,0,1}; state.vs_handle=0x44;
    state.streams[0]={0x1200,20};
    for(auto& stage:state.tss)stage[gpu::kTssColorOp]=1;
    state.simple[gpu::kMColorWriteEnable/4]=0x01010101;
    state.simple[gpu::kMZFunc/4]=0x201;
    auto& texture=state.tss[0];
    texture[gpu::kTssColorOp]=texture[gpu::kTssAlphaOp]=4;
    texture[gpu::kTssColorArg1]=texture[gpu::kTssAlphaArg1]=2;
    texture[gpu::kTssAddressU]=texture[gpu::kTssAddressV]=3;
    texture[gpu::kTssMinFilter]=texture[gpu::kTssMagFilter]=1;
    for(const char* mode:{"audit","tracked"}) {
        ScopedEnvironment coherency("PGR1_VK_TEXTURE_COHERENCY",mode);
        ScopedEnvironment diagnostic("PGR1_VK_TEXTURE_DIAGNOSTIC","skip-exact");
        auto backend=gpu::CreateBackend(); assert(backend->Init(32,32));
        state.textures[0]=0x1300; header(0x1300,data);
        for(unsigned i=0;i<1024;++i)kernel::W32(data+i*4,0xff123456);
        assert(kernel::R32(alias)==0xff123456);
        // EN: No MarkDirty/notification; both aliases must still trigger exact
        //     decoding within one frame. Unknown coverage never permits reuse.
        // PT-BR: Sem MarkDirty/notificação; ambos aliases ainda exigem decode
        //        exato no mesmo frame. Cobertura desconhecida não permite reuse.
        backend->Draw(state,8,0,4,nullptr);
        kernel::W32(alias+17*4,0xfffedcba);
        backend->Draw(state,8,0,4,nullptr);
        kernel::W32(data+1023*4,0xff654321);
        backend->Draw(state,8,0,4,nullptr);
        backend->Present(state.rt);
        Submission captured; assert(Take(captured));
        assert(captured.commands.size()==3 && captured.unsupported==0);
        assert(captured.texture_memcmp_checks==2 && captured.texture_diagnostic_skips==0);
        assert(captured.texture_tracker_queries==3 && captured.texture_tracker_uncovered_ranges==3);
        assert(captured.texture_tracker_fast_hits==0 && captured.texture_tracker_covered_queries==0);
        assert(captured.texture_tracker_exact_fallbacks==2 && captured.texture_tracker_false_negatives==2);
        const auto first=captured.commands[0].textures[0].pixels;
        const auto second=captured.commands[1].textures[0].pixels;
        const auto third=captured.commands[2].textures[0].pixels;
        assert(first->levels[0][17]==0xff123456 && first->levels[0][1023]==0xff123456);
        assert(second->levels[0][17]==0xfffedcba && second->levels[0][1023]==0xff123456);
        assert(third->levels[0][17]==0xfffedcba && third->levels[0][1023]==0xff654321);
        assert(kernel::R32(alias+1023*4)==0xff654321);
        backend->Draw(state,8,0,4,nullptr); backend->Present(state.rt); assert(Take(captured));
        assert(captured.commands[0].textures[0].pixels==third);
        assert(captured.texture_memcmp_checks==1 && captured.texture_tracker_exact_fallbacks==1);
        assert(captured.texture_tracker_false_negatives==0 && captured.texture_tracker_fast_hits==0);

        // EN: A CPU texture overlaps a GPU target without sharing its start.
        //     Readback must invalidate it and publish through both aliases.
        // PT-BR: Textura CPU sobrepõe target GPU com início diferente.
        //        Readback invalida-a e publica os bytes pelos dois aliases.
        header(0x1400,data+128); state.textures[0]=0x1400;
        backend->Draw(state,8,0,4,nullptr); backend->Present(state.rt); assert(Take(captured));
        const auto before_readback=captured.commands[0].textures[0].pixels;
        header(0x1500,data);
        auto writer=state; writer.rt={0x1500,data}; writer.zs={};
        backend->Clear(writer,0,nullptr,gpu::kClearTarget,0xff89abcd,1,0);
        std::thread readback([&] { backend->Sync(data,0); });
        while(!Take(captured))std::this_thread::yield();
        assert(captured.commands.size()==2 && captured.commands.back().kind==Command::Kind::Readback);
        captured.commands.back().readback->set_value(std::vector<uint32_t>(1024,0xff89abcd));
        readback.join();
        assert(kernel::R32(data)==0xff89abcd && kernel::R32(alias)==0xff89abcd);
        backend->Draw(state,8,0,4,nullptr); backend->Present(state.rt); assert(Take(captured));
        assert(captured.texture_memcmp_checks==1 && captured.texture_tracker_invalidations==1);
        assert(captured.texture_tracker_monitored_writes>0 && captured.texture_tracker_dirty_pages>0);
        assert(captured.texture_tracker_false_negatives==0 && captured.texture_tracker_fast_hits==0);
        assert(captured.commands[0].textures[0].pixels->levels[0][0]==0xff89abcd);
        assert(before_readback->levels[0][0]==0xff123456 && first->levels[0][17]==0xff123456);
        // EN/PT-BR: Preserve GPU authority / Preserve a autoridade GPU.
        state.textures[0]=0x1300;
        backend->Draw(state,8,0,4,nullptr); backend->Present(state.rt); assert(Take(captured));
        assert(captured.commands[0].textures[0].render_target==data);
        assert(!captured.commands[0].textures[0].pixels && captured.texture_tracker_queries==0);
    }
}
#endif
}
#endif
int main() {
#ifdef PGR1_NATIVE_VULKAN
    ScopedEnvironment sync_profile("PGR1_VK_SYNC_PROFILE","1");
    const char* coherency=std::getenv("PGR1_VK_TEXTURE_COHERENCY");
    if(!coherency || (std::strcmp(coherency,"audit")!=0 && std::strcmp(coherency,"tracked")!=0))
        unsetenv("PGR1_VK_TEXTURE_DIAGNOSTIC");
#endif
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
#ifdef PGR1_NATIVE_VULKAN
    assert(c.shaders->spirv.valid());
    for(const auto& binary:c.shaders->spirv.get()) {
        uint32_t magic=0; assert(binary.size()>=20 && binary.size()%4==0);
        std::memcpy(&magic,binary.data(),sizeof(magic)); assert(magic==0x07230203);
    }
#endif
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
#ifdef PGR1_NATIVE_VULKAN
    // Vulkan readback rows use guest top-down orientation (GL uses bottom-up).
    backend->Clear(s,0,nullptr,0xf0,0xff102030,1,0);
    std::thread rows([&] { backend->Sync(s.rt.data,0); });
    while(!Take(f)) std::this_thread::yield();
    auto pixels=std::vector<uint32_t>(1024,0xffabcdef);
    pixels[0]=0xff123456; pixels[31*32]=0xff654321;
    f.commands.back().readback->set_value(std::move(pixels)); rows.join();
    assert(kernel::R32(s.rt.data)==0xff123456);
    assert(kernel::R32(s.rt.data+31*128)==0xff654321);
#endif
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
    // EN: First static bind is a cold decode, not an eviction.
    // PT-BR: Primeiro vínculo estático é uma decodificação inicial.
    assert(f.texture_cache_misses==1 && f.texture_cache_evictions==0);
    assert(f.commands[0].textures[0].pixels->levels[0][0]==0xff123456);
    const auto& sampler=f.commands[0].textures[0];
    assert(sampler.wrap_w==2 && sampler.border_color==0x80402010);
    assert(sampler.lod_bias==-.75f && sampler.max_mip==1 && sampler.max_anisotropy==8);
    // Identical texture stages within a Draw share immutable decoded pixels.
    // Separate Draw calls still validate direct guest writes (tested below).
    s.textures[1]=s.textures[0];
    auto& second=s.tss[1];
    second[gpu::kTssColorOp]=second[gpu::kTssAlphaOp]=4;
    second[gpu::kTssColorArg1]=second[gpu::kTssAlphaArg1]=2;
    backend->Draw(s,8,0,4,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==1 && f.texture_static_binds==2);
    assert(f.texture_same_draw_reuses==1 && f.texture_memcmp_checks==1);
    assert(f.texture_memcmp_bytes>=32*32*4);
    assert(f.commands[0].textures[0].pixels==f.commands[0].textures[1].pixels);
    s.textures[1]=0;
    second[gpu::kTssColorOp]=second[gpu::kTssAlphaOp]=1;
    // Exact cache validation must detect unmarked CPU writes even within a
    // single frame. Previously submitted draws retain the old immutable pixels.
    backend->Draw(s,8,0,4,nullptr);
    kernel::W32(0x40000+17*4,0xfffedcba);
    backend->Draw(s,8,0,4,nullptr);
    backend->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==2);
    assert(f.texture_guest_redecodes==1);
    assert(f.texture_cache_misses==0);
    assert(f.commands[0].textures[0].pixels->levels[0][17]==0xff123456);
    assert(f.commands[1].textures[0].pixels->levels[0][17]==0xfffedcba);
    // Unchanged bytes share the decoded allocation, without rescanning a hash.
    const auto retained=f.commands[1].textures[0].pixels;
    backend->Draw(s,8,0,4,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands[0].textures[0].pixels==retained);
    // EN: Use a real two-level swizzled texture. Only the changed level
    // may be re-decoded; previously submitted snapshots must remain immutable.
    // PT-BR: Teste uma textura swizzled com dois mipmaps. Apenas o nível
    // alterado pode ser decodificado novamente; snapshots antigos são imutáveis.
    kernel::W32(0x130c,(0x06u<<8)|(2u<<16)|(5u<<20)|(5u<<24));
    kernel::W32(0x1310,0);
    for(unsigned i=0;i<16*16;i++)kernel::W32(0x40000+4096+i*4,0xff112233);
    backend->Draw(s,8,0,4,nullptr);backend->Present(s.rt);assert(Take(f));
    assert(f.commands.size()==1 && f.commands[0].textures[0].pixels->levels.size()==2);
    const auto mips_initial=f.commands[0].textures[0].pixels;
    kernel::W32(0x40000,0xffcafeba);
    backend->Draw(s,8,0,4,nullptr);backend->Present(s.rt);assert(Take(f));
    assert(f.texture_mip_levels_reused==1 && f.texture_mip_levels_decoded==1);
    const auto mips_first=f.commands[0].textures[0].pixels;
    assert(mips_first->levels[1]==mips_initial->levels[1]);
    assert(mips_first->levels[0]!=mips_initial->levels[0]);
    kernel::W32(0x40000+4096,0xff556677);
    backend->Draw(s,8,0,4,nullptr);backend->Present(s.rt);assert(Take(f));
    assert(f.texture_mip_levels_reused==1 && f.texture_mip_levels_decoded==1);
    assert(f.commands[0].textures[0].pixels->levels[0]==mips_first->levels[0]);
    assert(f.commands[0].textures[0].pixels->levels[1]!=mips_first->levels[1]);
    kernel::W32(0x130c,(0x12u<<8));
    kernel::W32(0x1310,31u|(31u<<12)|(1u<<24));
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
#ifdef PGR1_NATIVE_VULKAN
    // Flat interpolation attributes must precede HLSL qualifiers; the Vulkan
    // interface annotations must also preserve SDK cubemap shader declarations.
    s.simple[gpu::kMShadeMode/4]=0x1d00;
    backend->Draw(s,8,0,4,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==1);
    assert(f.commands[0].indices==std::vector<uint32_t>({2,0,1,3,0,2}));
    for(const auto& binary:f.commands[0].shaders->spirv.get()) assert(binary.size()>=20 && binary.size()%4==0);
    s.simple[gpu::kMShadeMode/4]=0;
#endif
    // Unsupported programmable work is explicitly counted and never replaced with a fake draw.
    s.vs_handle=1; backend->Draw(s,5,0,3,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.empty() && f.unsupported==1);
    s.vs=std::make_shared<gpu::VertexShader>(); gpu::FvfInputs(0x44,*s.vs);
    s.vs->program={0,(1u<<21)|0x1b,2u<<26,(15u<<12)|(1u<<11)|1};
    s.vs->constants.push_back({100,{1,1,1,1}}); s.vsc[100]={2,3,4,5};
    backend->Draw(s,5,0,3,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==1 && f.commands[0].vertex_constants[100][0]==2);
    // EN: A cached program must not freeze changing VC or owned vertices.
    // PT-BR: Um programa em cache não congela VC ou vértices próprios.
    const auto previous_shader=f.commands[0].shaders;
    s.vsc[100][0]=9;
    backend->Draw(s,5,0,3,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==1 && f.commands[0].shaders==previous_shader);
    assert(f.commands[0].vertex_constants[100][0]==9);
    // EN: Mutate the same object; opcode validation must not use pointer identity.
    // PT-BR: Mude o mesmo objeto; a validação não pode depender do endereço.
    s.vs->program={0,14u<<21,0,1};
    backend->Draw(s,5,0,3,nullptr); backend->Present(s.rt); assert(Take(f));
    assert(f.commands.empty() && f.unsupported==2); // unknown opcode is recorded, never rendered as zero
#ifdef PGR1_NATIVE_VULKAN
    // Fence/busy reflect actual consumer GPU completion, including a prefix
    // without Present; producer snapshots alone are not device completion.
    s.vs_handle=0x44; s.vs.reset();
    backend->Draw(s,5,0,3,nullptr);
    const auto fence=backend->Fence();
    assert(fence && backend->FencePending(fence));
    assert(backend->Busy(s.streams[0].vb));
    assert(!backend->Busy(0xdeadbeef));
    assert(Take(f) && !f.present && f.commands.size()==1 && f.completion);
    assert(f.reason==SubmissionReason::Fence && f.sync.flush_calls>0);
    f.completion->set_value();
    assert(!backend->FencePending(fence));
    assert(!backend->Busy(s.streams[0].vb));
    backend->WaitFence(fence);
    // An empty completion marker is ordered behind all previously queued work.
    const auto marker=backend->Fence();
    assert(Take(f) && f.commands.empty() && f.completion);
    assert(backend->FencePending(marker));
    f.completion->set_value(); backend->WaitFence(marker);
    assert(!backend->FencePending(marker));
    backend->Draw(s,5,0,3,nullptr);
    std::thread resource_wait([&] { backend->WaitResource(s.streams[0].vb); });
    while(!Take(f)) std::this_thread::yield();
    assert(!f.present && f.completion);
    f.completion->set_value(); resource_wait.join();
    assert(!backend->Busy(s.streams[0].vb));

    // Visibility reports sum exact device results from accepted counted draws,
    // preserving time/count/status publication and waiting for GPU completion.
    constexpr uint32_t report=0x8000;
    s.simple[gpu::kMCountEnable/4]=1;
    backend->ClearReport();
    backend->Draw(s,5,0,3,nullptr); backend->Draw(s,5,0,3,nullptr);
    kernel::W32(report+12,0xffffffff);
    std::thread report_wait([&] { backend->GetReport(report); });
    while(!Take(f)) std::this_thread::yield();
    assert(!f.present && f.commands.size()==2 && f.completion);
    assert(f.reason==SubmissionReason::VisibilityReport && f.sync.visibility_reports==1);
    assert(kernel::R32(report+12)==0xffffffff);
    assert(f.commands[0].visibility && f.commands[1].visibility);
    f.commands[0].visibility->set_value(21); f.commands[1].visibility->set_value(34);
    f.completion->set_value(); report_wait.join();
    assert(kernel::R32(report+4)==0 && kernel::R32(report+8)==55 && kernel::R32(report+12)==0);
    // Reset excludes old results, and a wide GPU count saturates the Xbox word.
    backend->ClearReport(); backend->Draw(s,5,0,3,nullptr);
    std::thread saturated([&] { backend->GetReport(report); });
    while(!Take(f)) std::this_thread::yield();
    assert(f.commands.size()==1 && f.commands[0].visibility);
    f.commands[0].visibility->set_value(UINT64_MAX);
    f.completion->set_value(); saturated.join();
    assert(kernel::R32(report+8)==UINT32_MAX);
    // An empty interval still publishes a genuine zero after an ordered marker.
    backend->ClearReport();
    std::thread empty_report([&] { backend->GetReport(report); });
    while(!Take(f)) std::this_thread::yield();
    assert(f.commands.empty() && f.completion);
    f.completion->set_value(); empty_report.join();
    assert(kernel::R32(report+8)==0 && kernel::R32(report+12)==0);
    s.simple[gpu::kMCountEnable/4]=0;
    backend->Present(s.rt); assert(Take(f));
    assert(f.sync.visibility_report_wait_ms>=0 && f.reason==SubmissionReason::Present);
    // EN: Demonstrate why this diagnostic must never become the default.
    // PT-BR: Demonstra por que o diagnóstico nunca pode ser o padrão.
    auto diagnostic=gpu::CreateBackend();
    {
        ScopedEnvironment coherency("PGR1_VK_TEXTURE_COHERENCY","exact");
        ScopedEnvironment unsafe("PGR1_VK_TEXTURE_DIAGNOSTIC","skip-exact");
        assert(diagnostic->Init(32,32));
    }
    header(0x1300,0x40000,0x12); kernel::W32(0x1300,0x40000);
    s.textures[0]=0x1300;
    kernel::W32(0x40000,0xff123456);
    diagnostic->Draw(s,5,0,3,nullptr);
    kernel::W32(0x40000,0xfffedcba);
    diagnostic->Draw(s,5,0,3,nullptr);
    diagnostic->Present(s.rt); assert(Take(f));
    assert(f.commands.size()==2 && f.texture_cache_misses==1);
    assert(f.texture_diagnostic_skips==1 && f.texture_memcmp_checks==0);
    assert(f.commands[1].textures[0].pixels->levels[0][0]==0xff123456);
#if defined(__linux__) && UINTPTR_MAX > UINT32_MAX
    TestAliasedTextureCoherency();
#endif
#endif
    std::puts("SDK capture tests passed: FVF colors, quad topology, 16-bit indices, owned guest data, texture decode, unsupported shaders.");
    Stop();
}
