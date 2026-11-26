// Xbox state/assembly stays on the guest thread; only owned commands cross to
// the selected host API. The native Vulkan build shares this capture boundary.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <span>
#include <stdexcept>
#include "gpu/shaders.h"
#include "app/app.h"
#include "shader_compiler.h"
#include "draw_constants.h"
#ifdef PGR1_ENABLE_REFERENCE_COMPARE
#include "reference_renderer.h"
#endif
#include "gpu/backend.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "kernel/kernel.h"
#include "core/log.h"
#include "harness.h"
#include "commands.h"
#include "texture_snapshot_key.h"
#include "texture_level_reuse.h"
#include "texture_cache_policy.h"
#ifdef PGR1_NATIVE_VULKAN
#include "texture_compare_worker.h"
#include "texture_coherency.h"
#include "../../include/pgr/memory/journal_stamp.h"
#include "../../include/pgr/runtime/simulation_clock.h"
#include "kernel/memory.h"
#endif
#include "vertex_register_expand.h"
#include "vertex_program_cache.h"
#include "flat_primitive_indices.h"
#include <cstdlib>
#include <array>

namespace gpu {
namespace {
using namespace pgr::gldirect;
#ifdef PGR1_NATIVE_VULKAN
constexpr const char* kBackendName="vulkan-capture";
constexpr const char* kRejectedShaderCache="shadercache-vk-v1";
#else
constexpr const char* kBackendName="gldirect";
constexpr const char* kRejectedShaderCache="shadercache-gl-v1";
#endif
void DumpRejected(const char* kind,std::span<const uint32_t> words) {
    const auto hash=Hash(words.data(),words.size_bytes());
    std::ostringstream path; path<<kRejectedShaderCache<<"/rejected-"<<kind<<'-'<<std::hex<<hash<<".nv2a";
    std::error_code error; std::filesystem::create_directories(kRejectedShaderCache,error);
    std::ofstream file(path.str(),std::ios::binary);
    file.write(reinterpret_cast<const char*>(words.data()),words.size_bytes());
    LOGR("%s: rejected %s shader %llx; diagnostic %s (%s)",kBackendName,kind,(unsigned long long)hash,path.str().c_str(),file?"saved":"write failed");
}
Rect Rectangle(const uint32_t* r) { return {int(r[0]),int(r[1]),int(r[2])-int(r[0]),int(r[3])-int(r[1])}; }
class GLBackend final : public Backend {
    Submission frame_;
    bool sync_profile_=false;
    struct WaitTimer {
        bool enabled;
        double& milliseconds;
        std::chrono::steady_clock::time_point begin;
        WaitTimer(bool on,double& ms):enabled(on),milliseconds(ms) {
            if(enabled) begin=std::chrono::steady_clock::now();
        }
        ~WaitTimer() {
            if(enabled) milliseconds+=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-begin).count();
        }
    };
#ifdef PGR1_NATIVE_VULKAN
    std::shared_future<void> current_completion_;
    std::map<uint32_t,std::shared_future<void>> fences_;
    struct ResourceUse {
        uint64_t generation=0;
        std::shared_future<void> completion;
    };
    // EN: A resource only needs one reference to the current prefix token.
    // PT-BR: Cada recurso precisa de apenas uma referência ao token do prefixo.
    std::unordered_map<uint32_t,ResourceUse> resource_use_;
    uint64_t resource_generation_=1;
    std::vector<std::shared_future<uint64_t>> counted_;
    uint32_t next_fence_=0;
    std::shared_future<void> Completion() {
        if(!frame_.completion) {
            frame_.completion=std::make_shared<std::promise<void>>();
            current_completion_=frame_.completion->get_future().share();
        }
        return current_completion_;
    }
    void Use(uint32_t header) {
        if(!header)return;
        auto& use=resource_use_[header];
        if(use.generation!=resource_generation_) {
            use.completion=Completion();
            use.generation=resource_generation_;
        }
    }
    void UseDraw(const State& s) {
        Use(s.rt.header); Use(s.zs.header); Use(s.index_buffer);
        for(auto texture:s.textures) Use(texture);
        for(const auto& stream:s.streams) Use(stream.vb);
        if(s.input_active) for(const auto& stream:s.input_streams) Use(stream.vb);
    }
    static bool Pending(const std::shared_future<void>& future) {
        if(!future.valid() || future.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            if(future.valid()) try { future.get(); }
            catch(const std::exception& e) { Fail(std::string("GPU completion cancelled: ")+e.what()); }
            return false;
        }
        return true;
    }
    static void WaitCompletion(const std::shared_future<void>& future) {
        if(future.valid()) try { future.get(); }
        catch(const std::exception& e) { Fail(std::string("GPU completion cancelled: ")+e.what()); }
    }
#endif
    void FlushFor(SubmissionReason reason) {
        if(sync_profile_) ++frame_.sync.flush_calls;
        if(!frame_.commands.empty() || frame_.completion) {
            frame_.present=false; frame_.frame=frames_+1; SubmitFrame(reason);
        }
    }
    void SubmitFrame(SubmissionReason reason=SubmissionReason::Present) {
        frame_.reason=reason;
        frame_.guest_vblank=harness::g_vblanks.load(std::memory_order_relaxed);
#ifdef PGR1_NATIVE_VULKAN
        frame_.simulation_tick=pgr::runtime::simulation_ticks.load(std::memory_order_relaxed);
        CollectTrackerStats();
        Completion();
#endif
        Submit(std::move(frame_)); frame_={};
#ifdef PGR1_NATIVE_VULKAN
        current_completion_={};
        ++resource_generation_;
#endif
    }
    std::set<const char*> warned_;
    std::unordered_set<uint32_t> gpu_written_, unread_;
    std::map<uint32_t,std::pair<TextureDesc,Target>> targets_;
    std::unordered_map<uint32_t,uint64_t> gpu_epoch_;
    struct LateRead { TextureDesc desc; std::future<std::vector<uint32_t>> future; };
    std::map<uint32_t,LateRead> late_;
#ifdef PGR1_NATIVE_VULKAN
    using MemoryTracker=pgr::memory::GuestMemoryTracker;
    MemoryTracker texture_tracker_{{{mem::kContiguousBase,mem::kWriteCombineBase},mem::kMappedPhysical,mem::kPage}};
    TextureCoherency texture_coherency_=TextureCoherency::Exact;
    MemoryTracker::Stats tracker_reported_{};
    bool Tracking() const { return texture_coherency_!=TextureCoherency::Exact; }
    void CollectTrackerStats() {
        if(!Tracking()) return;
        const auto begin=std::chrono::steady_clock::now();
        const auto stats=texture_tracker_.GetStats();
        frame_.texture_tracker_monitored_writes+=stats.monitored_writes-tracker_reported_.monitored_writes;
        frame_.texture_tracker_dirty_pages+=stats.dirty_pages-tracker_reported_.dirty_pages;
        frame_.texture_tracker_write_ns+=stats.write_ns-tracker_reported_.write_ns;
        frame_.texture_tracker_tracked_bytes=std::max(frame_.texture_tracker_tracked_bytes,stats.tracked_bytes);
        tracker_reported_=stats;
        frame_.texture_tracker_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-begin).count();
    }
    void InvalidateTextureMemory(uint32_t address,std::size_t bytes) {
        if(!Tracking())return;
        const auto begin=std::chrono::steady_clock::now();
        texture_tracker_.InvalidateRange(address,bytes);
        frame_.texture_tracker_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-begin).count();
        CollectTrackerStats();
    }
#endif
    void MarkGpuWritten(uint32_t address) {
        const bool new_authority=gpu_written_.insert(address).second;
        const bool new_unread=unread_.insert(address).second;
        gpu_epoch_[address]=DirtyEpoch(address);
#ifdef PGR1_NATIVE_VULKAN
        // EN: GPU draws do not mutate CPU bytes. Revoke CPU proof when GPU
        //     authority begins, then again after a readback makes it clean.
        //     Every new guest registration is Unknown, including overlaps.
        // PT-BR: Draws GPU não alteram bytes CPU. Revogue a prova ao iniciar
        //        autoridade GPU e novamente após readback tornar a faixa limpa.
        //        Todo novo registro guest é Unknown, inclusive sobreposições.
        if(Tracking() && (new_authority || new_unread))
            InvalidateTextureMemory(address,TotalSize(targets_.at(address).first));
#else
        (void)new_authority; (void)new_unread;
#endif
    }
    void WritePixels(uint32_t address,const TextureDesc& desc,const std::vector<uint32_t>& pixels) {
            if(pixels.size()!=size_t(desc.width)*desc.height) throw std::runtime_error("GPU readback size mismatch");
#ifdef PGR1_NATIVE_VULKAN
            // EN: Only this known writer joins the domain. Other SDK writers
            //     remain uncovered; this guard does not authorize guest hits.
            // PT-BR: Só este escritor conhecido participa do domínio. Outros
            //        escritores SDK seguem descobertos; isto não autoriza hits guest.
            std::optional<MemoryTracker::WriteGuard> write;
            if(Tracking()) write.emplace(texture_tracker_,address,TotalSize(desc));
#endif
            for(unsigned y=0;y<desc.height;y++) for(unsigned x=0;x<desc.width;x++) {
#ifdef PGR1_NATIVE_VULKAN
                const auto pixel=pixels.at(size_t(y)*desc.width+x);
#else
                const auto pixel=pixels.at(size_t(desc.height-1-y)*desc.width+x);
#endif
                uint32_t offset=Format(desc.format).swizzled ? SwizzledOffset(x,y,desc.width,desc.height)*4 : y*desc.pitch+x*4;
                if(desc.format==0x2c || desc.format==0x30) {
                    const uint16_t value=uint16_t(pixel);
                    offset=Format(desc.format).swizzled ? SwizzledOffset(x,y,desc.width,desc.height)*2 : y*desc.pitch+x*2;
                    std::memcpy(kernel::HostPtr(address+offset),&value,2);
                } else if(desc.format==0x05 || desc.format==0x11) {
                    const uint16_t packed=uint16_t(((pixel>>19)&31)<<11 | ((pixel>>10)&63)<<5 | ((pixel>>3)&31));
                    offset=Format(desc.format).swizzled ? SwizzledOffset(x,y,desc.width,desc.height)*2 : y*desc.pitch+x*2;
                    std::memcpy(kernel::HostPtr(address+offset),&packed,2);
                } else kernel::W32(address+offset,pixel);
            }
    }

    std::uint64_t frames_=0, draws_=0, unsupported_=0;
    std::uint64_t compare_frame_=0;
#ifdef PGR1_NATIVE_VULKAN
    // EN: Opt-in until measured on the reference Sandy Bridge system.
    // PT-BR: Opcional até a medição no Sandy Bridge de referência.
    std::unique_ptr<TextureCompareWorker> compare_worker_;
    bool skip_exact_diagnostic_=false;
    bool miss_trace_=false;
#ifdef XBE_PHYSICAL_WRITE_AUDIT
    bool journal_audit_=false;
    static std::optional<uint64_t> ReadJournal(uint32_t address,std::size_t bytes) {
        auto physical=pgr::memory::Canonicalize(
            {{mem::kContiguousBase,mem::kWriteCombineBase},mem::kMappedPhysical,mem::kPage},address,bytes);
        if(!physical)return {};
        return pgr::memory::JournalStamp(physical->offset,physical->bytes,mem::kPage,mem::kMappedPhysical,
            [](uint32_t offset){return mem::PhysicalWriteAudit::Read(mem::kContiguousBase+offset).sequence;});
    }
#endif
    unsigned miss_trace_events_=0;
#endif
    std::map<std::pair<uint64_t,uint64_t>,std::shared_ptr<const ShaderPair>> shaders_;
    VertexProgramCache vertex_program_cache_;
    struct CachedPixels {
        TextureDesc desc{};
        std::shared_ptr<const TexturePixels> pixels;
        // Exact byte snapshots let libc's vectorized comparison check every bind,
        // including direct CPU writes made without Lock/MarkDirty in this frame.
        std::vector<uint8_t> guest;
        uint64_t used=0;
#ifdef PGR1_NATIVE_VULKAN
        MemoryTracker::Handle tracked_range=0;
        MemoryTracker::Stamp tracked_stamp{};
#ifdef XBE_PHYSICAL_WRITE_AUDIT
        std::optional<uint64_t> journal_stamp;
#endif
#endif
    };
    std::unordered_map<uint32_t,CachedPixels> textures_;
    uint64_t texture_clock_=0;
    // EN: Native Vulkan defaults to 1024 entries after the RX 580 cache-thrash
    // test; legacy OpenGL still defaults to 512. The env override remains valid.
    // PT-BR: Vulkan usa 1024 entradas após os testes de expulsão na RX 580;
    // OpenGL legado continua em 512. A variável de ambiente tem prioridade.
#ifdef PGR1_NATIVE_VULKAN
    static constexpr bool kVulkanTextureCache=true;
#else
    static constexpr bool kVulkanTextureCache=false;
#endif
    const unsigned texture_cache_entries_=TextureCacheEntries(
        std::getenv("PGR1_VK_TEXTURE_CACHE_ENTRIES"),TextureCacheDefault(kVulkanTextureCache));

    void Unsupported(const char* reason) {
        ++unsupported_;
        if(warned_.insert(reason).second) LOGR("%s: unsupported %s (draws remain counted in telemetry)",kBackendName,reason);
    }
    bool TargetOf(const State& s, Target& t) {
        TextureDesc rt, zs;
        if(!DescribeSurface(s.rt,rt) || !rt.width || !rt.height || rt.width>8192 || rt.height>8192 ||
           (rt.format!=0x06 && rt.format!=0x07 && rt.format!=0x12 && rt.format!=0x1e && rt.format!=0x05 && rt.format!=0x11)) {
            Unsupported("render target format/size"); return false;
        }
        t={rt.data,0,rt.width,rt.height,rt.format==0x07 || rt.format==0x1e || rt.format==0x05 || rt.format==0x11};
        t.format=rt.format;
        if(s.zs.data) {
            if(!DescribeSurface(s.zs,zs) || zs.width!=rt.width || zs.height!=rt.height ||
               (zs.format!=0x2a && zs.format!=0x2e && zs.format!=0x2c && zs.format!=0x30)) { Unsupported("depth format/size"); return false; }
            t.depth=zs.data; t.depth_format=zs.format;
            targets_[zs.data]={zs,t};
        }
        targets_[rt.data]={rt,t};
        return true;
    }
public:
    bool Init(uint32_t w,uint32_t h) override {
        // Assemble's converted path still uses the SDK's exact Xbox attribute decoders.
        // No global flag is toggled around individual draws.
        g_raw_vertices=false;
        const char* sync_profile=std::getenv("PGR1_VK_SYNC_PROFILE");
        sync_profile_=sync_profile && std::strcmp(sync_profile,"1")==0;
#ifdef PGR1_NATIVE_VULKAN
        const char* overlap=std::getenv("PGR1_VK_TEXTURE_OVERLAP");
        // EN: Deliberately incorrect ceiling experiment, never a coherency mode.
        // PT-BR: Experimento de teto deliberadamente incorreto, nunca um modo de coerência.
        const char* diagnostic=std::getenv("PGR1_VK_TEXTURE_DIAGNOSTIC");
        const char* coherency=std::getenv("PGR1_VK_TEXTURE_COHERENCY");
        texture_coherency_=ParseTextureCoherency(coherency);
        const char* miss_trace=std::getenv("PGR1_VK_TEXTURE_MISS_TRACE");
        miss_trace_=texture_coherency_==TextureCoherency::Audit && miss_trace && std::strcmp(miss_trace,"1")==0;
#ifdef XBE_PHYSICAL_WRITE_AUDIT
        const char* journal=std::getenv("PGR1_VK_TEXTURE_JOURNAL_AUDIT");
        journal_audit_=texture_coherency_==TextureCoherency::Audit && journal && std::strcmp(journal,"1")==0;
        mem::PhysicalWriteAudit::Enable(miss_trace_ || journal_audit_);
        LOGR("vulkan-capture: SDK physical write journal / diário de escritas físicas = %s",(miss_trace_ || journal_audit_)?"on":"off");
        if(journal_audit_) LOGR("vulkan-capture: journal prediction audit (exact bytes retained) / auditoria de previsões do diário (bytes exatos preservados)");
#else
        if(miss_trace_) LOGR("vulkan-capture: miss trace enabled; SDK journal not compiled / rastreio ativo; diário SDK não compilado");
        if(std::getenv("PGR1_VK_TEXTURE_JOURNAL_AUDIT"))
            LOGR("vulkan-capture: journal audit unavailable without SDK write audit build / auditoria do diário indisponível sem build instrumentado");
#endif
        skip_exact_diagnostic_=!Tracking() && diagnostic && std::strcmp(diagnostic,"skip-exact")==0;
        if(Tracking() && diagnostic && std::strcmp(diagnostic,"skip-exact")==0)
            LOGR("vulkan-capture: audit/tracked require exact fallback; unsafe option ignored / audit/tracked exigem fallback exato; opção insegura ignorada");
        LOGR("vulkan-capture: texture coherency / coerência de texturas = %s; pinned SDK coverage unknown / cobertura do SDK fixado desconhecida",
            texture_coherency_==TextureCoherency::Audit?"audit":texture_coherency_==TextureCoherency::Tracked?"tracked (experimental)":"exact");
        if(skip_exact_diagnostic_)
            LOGR("WARNING / AVISO: UNSAFE texture diagnostic skip-exact; stale textures possible; NOT correctness validation");
        if(!skip_exact_diagnostic_ && overlap && std::strcmp(overlap,"1")==0)
            compare_worker_=std::make_unique<TextureCompareWorker>();
        LOGR("%s: exact texture/vertex overlap / sobreposição exata textura/vértices = %s",
            kBackendName,compare_worker_?"on":"off");
#endif
        if(const char* compare=std::getenv("PGR1_COMPARE_FRAME")) compare_frame_=std::strtoull(compare,nullptr,10);
#ifdef PGR1_NATIVE_VULKAN
        LOGR("vulkan-capture: PGR1 SDK command capture %ux%u; SDK HLSL -> Vulkan SPIR-V, owned FIFO",w,h);
#else
        LOGR("gldirect: GPU FVF XYZRHW renderer %ux%u; SDK HLSL/SPIR-V/GLSL shaders enabled",w,h);
#endif
        LOGR("%s: CPU texture cache capacity / capacidade = %u entries",
             kBackendName,texture_cache_entries_);
        return true;
    }
    void Clear(const State& s,uint32_t n,const uint8_t* rects,uint32_t flags,uint32_t color,float z,uint32_t stencil) override {
#ifdef PGR1_ENABLE_REFERENCE_COMPARE
        if(compare_frame_ && frames_+1==compare_frame_) reference::Clear(s,n,rects,flags,color,z,stencil);
#endif
        Command c; if(!TargetOf(s,c.target)) return;
        c.flags=flags; c.color=color; c.z=z; c.stencil=stencil;
        if(n && rects) for(uint32_t i=0;i<n;i++) { uint32_t r[4]; std::memcpy(r,rects+i*16,16); c.rects.push_back(Rectangle(r)); }
        if(c.rects.empty()) c.rects.push_back({int(s.viewport.x),int(s.viewport.y),int(s.viewport.w),int(s.viewport.h)});
        frame_.geometry_vertices+=c.vertices.size();
        frame_.geometry_indices+=c.indices.size();
        frame_.geometry_bytes+=c.vertices.size()*sizeof(Vertex)+c.indices.size()*sizeof(uint32_t);
        MarkGpuWritten(c.target.color);
        if(c.target.depth)MarkGpuWritten(c.target.depth);
        frame_.commands.push_back(std::move(c));
#ifdef PGR1_NATIVE_VULKAN
        Use(s.rt.header); Use(s.zs.header);
#endif
    }
    void Draw(const State& s,uint32_t prim,uint32_t start,uint32_t count,const uint8_t* indices) override {
        ++draws_;
#ifdef PGR1_ENABLE_REFERENCE_COMPARE
        if(compare_frame_ && frames_+1==compare_frame_) reference::Draw(s,prim,start,count,indices);
#endif
        struct Timer {
            Submission& frame;
            std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
            bool supported=false;
            ~Timer() {
                auto ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
                frame.draw_ns+=ns; frame.max_draw_ns=std::max(frame.max_draw_ns,ns);
                if(supported) ++frame.supported; else ++frame.rejected_draws;
            }
        } timer{frame_};
        const bool program=(s.vs_handle&1) && s.vs && !s.vs->program.empty();
        if((s.vs_handle&1) && !s.vs) { Unsupported("missing vertex program"); return; }
        if(program && s.vs->program.size()%4) { Unsupported("truncated NV2A vertex instruction"); DumpRejected("vs",s.vs->program); return; }
        if(s.ps) for(unsigned stage=0;stage<4;stage++) {
            const auto mode=(s.rs[kRsPsTextureModes]>>(stage*5))&31;
            if(mode==8 || mode>0x11) { Unsupported("unknown NV2A texture shader mode"); DumpRejected("ps",s.ps->def); return; }
        }
        uint64_t program_hash=0;
        if(program) {
            // EN: Exact program words, not the shader object's address, prove a cache hit.
            // PT-BR: Palavras exatas, não o endereço do shader, comprovam o acerto.
            const auto info=vertex_program_cache_.Get(s.vs.get(),s.vs->program,
                [](const void* data,std::size_t bytes){return Hash(data,bytes);});
            if(!info.valid) {
                Unsupported("unknown NV2A MAC opcode"); DumpRejected("vs",s.vs->program);
                LOGR("%s: VS %llx instruction %zu opcode %u",kBackendName,
                    (unsigned long long)s.vs->hash,info.bad_instruction,info.bad_opcode);
                return;
            }
            program_hash=info.hash;
        }
        if(s.rs[kRsPointSpriteEnable] || s.aa_x!=1 || s.aa_y!=1) {
            Unsupported("point sprites/MSAA"); return;
        }
        if(s.scissor_count>1) { Unsupported("multiple scissor rectangles"); return; }
        Command c; c.kind=Command::Kind::Draw; if(!TargetOf(s,c.target)) return;
        auto& p=c.pipeline; const auto& v=s.viewport;
        if(!v.w || !v.h) return;
        p.viewport={int(v.x),int(v.y),int(v.w),int(v.h)}; p.min_z=v.min_z; p.max_z=v.max_z;
        auto m=[&](unsigned key) { return s.simple[key/4]; };
        p.depth=s.rs[kRsZEnable] && c.target.depth; p.depth_write=m(kMZWriteEnable); p.depth_func=m(kMZFunc);
        p.blend=m(kMBlendEnable); p.src_blend=m(kMSrcBlend); p.dst_blend=m(kMDestBlend); p.blend_op=m(kMBlendOp); p.blend_color=m(kMBlendColor);
        p.fill=s.rs[kRsFillMode] ? s.rs[kRsFillMode] : 0x1b02;
        // GL polygon offset units use the host depth format's integer range.
        // Xbox offsets are expressed in zscale units; points/lines do not take
        // the solid-triangle offset (the same restriction as the SDK backend).
        if(prim>=5 && m(kMSolidOffsetEnable) && s.rs[kRsZEnable]!=2) {
            const float max_depth=(c.target.depth_format==0x2c || c.target.depth_format==0x30)?65535.0f:16777215.0f;
            p.depth_bias=std::round(std::bit_cast<float>(m(kMDepthOffset))*max_depth/s.zscale);
            p.depth_slope=std::bit_cast<float>(m(kMSlopeScale));
        }
        p.color_mask=m(kMColorWriteEnable); p.cull=s.rs[kRsCullMode]; p.front=s.rs[kRsFrontFace];
        p.stencil=s.rs[kRsStencilEnable] && c.target.depth; p.stencil_func=m(kMStencilFunc); p.stencil_ref=m(kMStencilRef);
        p.stencil_mask=m(kMStencilMask); p.stencil_write=m(kMStencilWriteMask);
        p.stencil_fail=s.rs[kRsStencilFail]; p.stencil_zfail=m(kMStencilZFail); p.stencil_pass=m(kMStencilPass);
        p.alpha_test=m(kMAlphaTestEnable); p.alpha_func=m(kMAlphaFunc); p.alpha_ref=m(kMAlphaRef)&255;
        for(unsigned i=0;i<std::min(s.scissor_count,8u);i++) p.scissors.push_back(Rectangle(s.scissors.data()+i*4));
        gpu::TexInfo tex_info[4]{};
        auto constants=Constants(s,c.target.width,c.target.height,program);
        // EN: A synchronous Draw() has no guest execution between texture stages.
        // PT-BR: Durante um Draw() síncrono, o jogo não executa entre estágios.
        // EN: Never reuse across draws: guest writes can bypass MarkDirty().
        // PT-BR: Nunca reutilize entre draws: alterações podem ignorar MarkDirty().
        struct VerifiedTexture {
            TextureSnapshotKey key{};
            std::shared_ptr<const TexturePixels> pixels;
        };
        std::array<VerifiedTexture,4> verified_this_draw{};
        unsigned verified_count=0;
        // EN: SDK assembly only reads guest inputs. Keep it on the capture
        // thread and finish any borrowed comparison before leaving this Draw.
        // PT-BR: A montagem SDK lê entradas guest. Mantenha-a na thread de
        // captura e conclua qualquer comparação emprestada antes de sair.
        // EN: The SDK resets its converted buffers on Assemble(); retain
        // capacity on the producer thread, never borrow it in a submission.
        // PT-BR: Assemble() reinicia os buffers convertidos do SDK; retenha
        // a capacidade na thread produtora, sem emprestá-los à submissão.
        static thread_local Assembled a;
        bool assembled=false, assembly_ok=false;
        auto assemble=[&] {
            const auto start_assembly=std::chrono::steady_clock::now();
            assembly_ok=Assemble(s,prim,start,count,indices,a);
            assembled=true;
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-start_assembly).count();
            frame_.assemble_ns+=ns;
            return ns;
        };
        bool fixed_enabled=true;
        for(unsigned stage=0;stage<4;stage++) {
            if(s.ps) {
                unsigned mode=(s.rs[kRsPsTextureModes]>>(stage*5))&31;
                if(mode==0 || mode==4 || mode==5 || mode==0xa || mode==0x11) continue;
            } else {
                fixed_enabled=fixed_enabled && s.tss[stage][kTssColorOp]>1;
                if(!fixed_enabled) continue;
            }
            if(!s.textures[stage]) continue;
            TextureDesc d;
            if(!Describe(s.textures[stage],d) || d.depth!=1 || d.format==0x0b || Format(d.format).depth || d.width>4096 || d.height>4096) {
                Unsupported("depth/palette/volume texture"); return;
            }
            auto& t=c.textures[stage]; const auto& ts=s.tss[stage];
            t.width=d.width; t.height=d.height; t.cube=d.cube;
            t.wrap_u=ts[kTssAddressU]; t.wrap_v=ts[kTssAddressV];
            t.min_filter=ts[kTssMinFilter]; t.mag_filter=ts[kTssMagFilter]; t.mip_filter=ts[kTssMipFilter];
            t.wrap_w=ts[kTssAddressW]; t.border_color=ts[kTssBorderColor];
            t.lod_bias=std::bit_cast<float>(ts[kTssMipLodBias]); t.max_mip=ts[kTssMaxMipLevel];
            t.max_anisotropy=std::max(1u,ts[kTssMaxAnisotropy]);
            tex_info[stage].kind=d.cube ? kTexCube : kTex2D; tex_info[stage].rect=d.linear;
            constants.texsize[stage][0]=float(d.width); constants.texsize[stage][1]=float(d.height);
            if(gpu_written_.contains(d.data) && gpu_epoch_[d.data]!=DirtyEpoch(d.data)) gpu_written_.erase(d.data);
            // EN/PT-BR: Compute face stride once / Calcule o passo entre faces uma vez.
            const auto face_stride=d.cube?FaceSize(d):0u;
            if(d.cube) for(unsigned face=0;face<6;face++) {
                const auto address=d.data+face*face_stride;
                if(gpu_written_.contains(address)) t.face_targets[face]=address;
            }
            if(!d.cube && gpu_written_.contains(d.data)) {
                if(d.data==c.target.color) { Unsupported("render-target feedback loop"); return; }
                t.render_target=d.data; tex_info[stage].flags|=1;
                if(NoAlphaFormat(d.format)) tex_info[stage].flags|=kTexOpaque;
                continue;
            }
            ++frame_.texture_static_binds;
            const TextureSnapshotKey signature{d.data,d.format,d.width,d.height,d.depth,d.levels,d.pitch,d.cube,d.linear};
            bool reused_in_draw=false;
            for(unsigned seen=0;seen<verified_count;++seen) {
                if(verified_this_draw[seen].key==signature) {
                    t.pixels=verified_this_draw[seen].pixels;
                    ++frame_.texture_same_draw_reuses;
                    reused_in_draw=true;
                    break;
                }
            }
            if(reused_in_draw)continue;
            if(textures_.size()>=texture_cache_entries_ && !textures_.contains(d.data)) {
                // EN: Count evictions; distinguish cache churn from real writes.
                // PT-BR: Conte remoções; diferencie a lotação de escritas reais.
                ++frame_.texture_cache_evictions;
                // Evict one least recently bound snapshot, not the whole scene.
                // Commands retain shared ownership until the GPU consumes them.
                auto oldest=std::min_element(textures_.begin(),textures_.end(),[](const auto& a,const auto& b) { return a.second.used<b.second.used; });
#ifdef PGR1_NATIVE_VULKAN
                if(Tracking()) {
                    const auto begin=std::chrono::steady_clock::now();
                    texture_tracker_.UnregisterRange(oldest->second.tracked_range);
                    frame_.texture_tracker_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now()-begin).count();
                }
#endif
                textures_.erase(oldest);
            }
            auto& cached=textures_[d.data];
            cached.used=++texture_clock_;
            frame_.texture_cache_entries_peak=std::max(
                frame_.texture_cache_entries_peak,std::uint64_t(textures_.size()));
            const auto check_start=std::chrono::steady_clock::now();
            const auto* guest=static_cast<const uint8_t*>(kernel::HostPtr(d.data));
            const bool metadata_same=cached.pixels && cached.desc.format==d.format && cached.desc.width==d.width &&
               cached.desc.height==d.height && cached.desc.depth==d.depth && cached.desc.levels==d.levels &&
               cached.desc.cube==d.cube && cached.desc.linear==d.linear && cached.desc.pitch==d.pitch;
            // EN: TotalSize depends only on these descriptor fields; a valid
            // snapshot already owns exactly that span. Still compare every byte.
            // PT-BR: TotalSize depende só desses campos; um snapshot válido
            // contém exatamente essa extensão. Ainda compare todos os bytes.
            const std::size_t bytes=metadata_same?cached.guest.size():
                (d.cube?std::size_t(std::uint32_t(face_stride*6u)):TotalSize(d));
            bool changed=!metadata_same;
            std::int64_t overlapped_assembly_ns=0;
#ifdef PGR1_NATIVE_VULKAN
            std::optional<MemoryTracker::ReadGuard> tracked_read;
            MemoryTracker::Prediction prediction;
            if(Tracking()) {
                const auto begin=std::chrono::steady_clock::now();
                if(!metadata_same) {
                    texture_tracker_.UnregisterRange(cached.tracked_range);
                    // EN: No guest writer coverage is claimed by the pinned SDK.
                    // PT-BR: O SDK fixado não comprova cobertura dos escritores guest.
                    cached.tracked_range=texture_tracker_.RegisterRange(d.data,bytes);
                    cached.tracked_stamp={};
                }
                tracked_read.emplace(texture_tracker_);
                prediction=tracked_read->IsUnchanged(cached.tracked_range,cached.tracked_stamp);
                ++frame_.texture_tracker_queries;
                if(prediction.covered) ++frame_.texture_tracker_covered_queries;
                else ++frame_.texture_tracker_uncovered_ranges;
                if(metadata_same && prediction.registered && !prediction.advisory_unchanged)
                    ++frame_.texture_tracker_invalidations;
                frame_.texture_tracker_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now()-begin).count();
            }
#ifdef XBE_PHYSICAL_WRITE_AUDIT
            std::optional<uint64_t> journal_before;
            auto journal_prediction=pgr::memory::JournalPrediction::Unknown;
            if(journal_audit_) {
                const auto begin=std::chrono::steady_clock::now();
                journal_before=ReadJournal(d.data,bytes);
                if(metadata_same)
                    journal_prediction=pgr::memory::PredictJournal(cached.journal_stamp,journal_before,
                        prediction.registered,prediction.advisory_unchanged);
                frame_.texture_journal_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now()-begin).count();
            }
#endif
            const bool tracked_hit=metadata_same && texture_coherency_==TextureCoherency::Tracked && prediction.CanReuse();
            if(tracked_hit) ++frame_.texture_tracker_fast_hits;
            else if(metadata_same && Tracking()) ++frame_.texture_tracker_exact_fallbacks;
            if(metadata_same && (skip_exact_diagnostic_ || tracked_hit)) {
                // EN: Proven hits require coverage; only the separate unsafe
                //     diagnostic deliberately bypasses proof of byte equality.
                // PT-BR: Hits comprovados exigem cobertura; só o diagnóstico
                //        inseguro separado omite a prova de igualdade dos bytes.
                if(skip_exact_diagnostic_)++frame_.texture_diagnostic_skips;
            } else
#endif
            if(metadata_same) {
                ++frame_.texture_memcmp_checks;
                frame_.texture_memcmp_bytes+=bytes; // EN: upper bound (early exit); PT-BR: limite superior (saída antecipada)
#ifdef PGR1_NATIVE_VULKAN
                // EN: One large cached texture per draw can overlap vertex
                // assembly. Small checks stay inline to avoid wake-up overhead.
                // PT-BR: Uma textura grande por draw pode sobrepor a montagem.
                // Comparações pequenas continuam locais, sem acordar a thread.
                if(compare_worker_ && !assembled && bytes>=256u*1024u && count>=64) {
                    ++frame_.texture_overlap_checks;
                    changed=!compare_worker_->CompareWhile(guest,cached.guest.data(),bytes,[&]{
                        overlapped_assembly_ns=assemble();
                    });
                } else
#endif
                    changed=std::memcmp(guest,cached.guest.data(),bytes)!=0;
            }
#ifdef PGR1_NATIVE_VULKAN
#ifdef XBE_PHYSICAL_WRITE_AUDIT
            // EN: Shadow prediction only. Exact comparison above is never bypassed.
            // PT-BR: Apenas previsão em sombra. A comparação exata acima nunca é omitida.
            if(journal_audit_ && metadata_same) {
                switch(journal_prediction) {
                case pgr::memory::JournalPrediction::Clean:
                    ++frame_.texture_journal_clean;
                    if(changed)++frame_.texture_journal_false_negatives;
                    break;
                case pgr::memory::JournalPrediction::Dirty:
                    ++frame_.texture_journal_dirty;
                    if(!changed)++frame_.texture_journal_false_positives;
                    break;
                default: ++frame_.texture_journal_unknown; break;
                }
            }
#endif
            if(Tracking() && metadata_same && !tracked_hit) {
                const auto audit=AuditTexturePrediction(prediction,changed);
                frame_.texture_tracker_false_positives+=audit.false_positive;
                frame_.texture_tracker_false_negatives+=audit.false_negative;
                if(audit.false_negative && warned_.insert("texture tracker missed write").second)
                    LOGR("vulkan-capture: advisory tracker missed guest bytes; exact fallback preserved / tracker informativo não detectou escrita guest; fallback exato preservado");
                // EN: Extra scanning only on observed misses, never on cache hits.
                //     The last page writer is an advisory hint, not causal proof.
                // PT-BR: Varredura extra só em falhas observadas, nunca em hits.
                //        Último escritor da página é indício, não prova causal.
                if(audit.false_negative && miss_trace_ && miss_trace_events_<256) {
                    ++miss_trace_events_;
                    const auto mismatch=std::mismatch(cached.guest.begin(),cached.guest.end(),guest);
                    const auto offset=std::size_t(mismatch.first-cached.guest.begin());
                    if(offset<bytes && uint64_t(d.data)+offset<=UINT32_MAX) {
                        const uint32_t changed_address=d.data+uint32_t(offset);
                        const auto physical=pgr::memory::Canonicalize(
                            {{mem::kContiguousBase,mem::kWriteCombineBase},mem::kMappedPhysical,mem::kPage},changed_address,1);
                        unsigned source=0; uint64_t sequence=0;
#ifdef XBE_PHYSICAL_WRITE_AUDIT
                        const auto event=mem::PhysicalWriteAudit::Read(changed_address);
                        source=unsigned(event.source); sequence=event.sequence;
#endif
                        LOGR("texture-miss: frame=%llu draw=%llu guest=%08X bytes=%llu offset=%llu changed=%08X physical=%08X format=%u width=%u height=%u levels=%u last_source=%u page_sequence=%llu",
                            (unsigned long long)(frames_+1),(unsigned long long)draws_,
                            unsigned(d.data),(unsigned long long)bytes,(unsigned long long)offset,
                            unsigned(changed_address),physical?unsigned(physical->offset):UINT32_MAX,
                            unsigned(d.format),unsigned(d.width),unsigned(d.height),unsigned(d.levels),source,
                            (unsigned long long)sequence);
                    } else LOGR("texture-miss: no stable differing byte / sem byte divergente estável");
                }
            }
#endif
            // EN: Do not charge assembly twice when work overlaps.
            // PT-BR: Não contabilize a montagem duas vezes na sobreposição.
            frame_.texture_check_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-check_start).count()-overlapped_assembly_ns;
            if(changed) {
                const auto decode_start=std::chrono::steady_clock::now();
                auto pixels=std::make_shared<TexturePixels>();
                const unsigned face_count=d.cube?6u:1u;
                // EN: Reusing unmodified mip levels is useful for swizzled
                // and compressed multi-level textures. Preserve single-level decoding.
                // PT-BR: Reutilizar mipmaps inalterados beneficia formatos swizzled
                // e comprimidos. Mantenha a decodificação habitual de um único nível.
                const bool previous_levels=metadata_same && !d.linear &&
                    face_count*d.levels>1 && cached.pixels &&
                    cached.pixels->levels.size()==std::size_t(face_count)*d.levels;
                for(unsigned face=0;face<face_count;face++) for(unsigned level=0;level<d.levels;level++) {
                    const std::size_t index=std::size_t(face)*d.levels+level;
                    const std::size_t offset=std::size_t(face)*face_stride+LevelOffset(d,level);
                    const std::size_t length=LevelSize(d,level);
                    // EN: Reuse only if every byte in this mip is identical to
                    // the previous immutable snapshot (not just DirtyEpoch).
                    // PT-BR: Reutilize apenas se todos os bytes do mip coincidirem
                    // com o snapshot anterior; DirtyEpoch não basta.
                    if(previous_levels && TextureLevelUnchanged(
                          guest,cached.guest.data(),bytes,offset,length)) {
                        pixels->levels.push_back(cached.pixels->levels[index]);
                        ++frame_.texture_mip_levels_reused;
                        continue;
                    }
                    std::vector<uint32_t> decoded;
                    if(!DecodeBgra(d,level,guest+std::size_t(face)*face_stride,decoded)) {
                        Unsupported("texture decoder format"); return;
                    }
                    pixels->levels.push_back(std::move(decoded));
                    ++frame_.texture_mip_levels_decoded;
                }
                // EN: Classify why the snapshot must be decoded.
                // PT-BR: Identifique a causa de cada snapshot decodificado.
                if(!cached.pixels) ++frame_.texture_cache_misses;
                else if(!metadata_same) ++frame_.texture_descriptor_redecodes;
                else ++frame_.texture_guest_redecodes;
                cached.desc=d; cached.pixels=pixels; cached.guest.assign(guest,guest+bytes);
                ++frame_.texture_decodes;
                frame_.decode_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-decode_start).count();
            }
#ifdef PGR1_NATIVE_VULKAN
#ifdef XBE_PHYSICAL_WRITE_AUDIT
            if(journal_audit_) {
                const auto begin=std::chrono::steady_clock::now();
                // EN: Compare a second sample after exact snapshot processing.
                // PT-BR: Compare uma segunda amostra após processar o snapshot exato.
                const auto journal_after=ReadJournal(d.data,bytes);
                if(journal_before && journal_before!=journal_after)++frame_.texture_journal_unstable;
                cached.journal_stamp=pgr::memory::StableJournalStamp(journal_before,journal_after);
                frame_.texture_journal_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now()-begin).count();
            }
#endif
            if(tracked_read) {
                const auto begin=std::chrono::steady_clock::now();
                // EN: Publish the stamp with the owned snapshot under the same
                //     read guard. Unknown coverage still requires exact next draw.
                // PT-BR: Publique stamp e snapshot próprio sob a mesma guarda.
                //        Sem cobertura, o próximo draw ainda compara todos os bytes.
                cached.tracked_stamp=tracked_read->CaptureGeneration(cached.tracked_range);
                tracked_read.reset();
                frame_.texture_tracker_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now()-begin).count();
            }
#endif
            t.pixels=cached.pixels;
            verified_this_draw[verified_count++]={signature,t.pixels};
        }
        if(!assembled)assemble();
        if(!assembly_ok) { Unsupported("SDK primitive/vertex assembly failed"); return; }
        if(a.vertices.empty() || !(a.regs&1)) { Unsupported("raw or missing position attributes"); return; }
        if(a.pretransformed && (c.target.height==480 || c.target.height==576) && c.target.width>=c.target.height &&
           app::FrameAspect()>app::kGame.hud_aspect+1e-3f) {
            // Match SDK HudSqueeze/DrawSpan for the PGR1 FVF HUD. Full-frame fills
            // keep their edge vertices; individual elements keep their shape when stretched to 16:9.
            float lo=1e30f,hi=-1e30f; const unsigned registers=std::popcount(a.regs);
            for(unsigned i=0;i<a.vertex_count;i++) { float x=a.vertices[size_t(i)*registers*4]; lo=std::min(lo,x); hi=std::max(hi,x); }
            bool spans=!s.squeeze_beyond && ((lo<=.5f && hi>=c.target.width-.5f) ||
                (!s.textures[0] && lo>=-2 && hi<=c.target.width+2 && (lo<=.5f || hi>=c.target.width-.5f)));
            bool viewport_fill=s.viewport.w<c.target.width-1 && lo<=s.viewport.x+.5f && hi>=s.viewport.x+s.viewport.w-.5f;
            if(!viewport_fill || s.squeeze_beyond) {
                float k=app::kGame.hud_aspect/app::FrameAspect(); constants.hud[0]=spans?k:-k;
                if(s.squeeze_beyond && k<1) constants.hud[1]+=s.squeeze_shift*k/(1-k);
            }
        }
        if(constants.hud[0]<0) { p.clip_scale=-constants.hud[0]; p.clip_pivot=constants.hud[1]; }
        auto pk=MakePixelKey(s,tex_info); pk.wbuf=s.rs[kRsZEnable]==2;
        FixedKey fk{}; if(!program) fk=MakeFixedKey(s);
        uint64_t vh=program ? program_hash : Hash(&fk,sizeof(fk));
        vh=Hash(&a.regs,sizeof(a.regs),vh); vh=Hash(&a.pretransformed,sizeof(a.pretransformed),vh);
        const uint64_t ph=Hash(&pk,sizeof(pk));
        auto& pair=shaders_[{vh,ph}];
        if(!pair) {
            auto shaders=std::make_shared<ShaderPair>();
            std::string vs=program ? VertexProgramHlsl(s.vs->program,a.regs) : FixedVertexHlsl(a.regs,a.pretransformed,0,fk);
            std::string ps=PixelHlsl(pk);
            if(pk.flat) for(const char* field : {"d0 : COLOR0", "d1 : COLOR1", "b0 : COLOR2", "b1 : COLOR3"}) {
                const std::string declaration="float4 "+std::string(field);
                for(auto* source:{&vs,&ps}) {
                    auto at=source->find(declaration);
                    if(at!=std::string::npos) source->insert(at,"nointerpolation ");
                }
            }
#ifndef PGR1_NATIVE_VULKAN
            const std::string marker="    float4 tc0 = i.tc0, tc1 = i.tc1, tc2 = i.tc2, tc3 = i.tc3;\n";
            std::string flip;
            for(unsigned i=0;i<4;i++) if(tex_info[i].flags&1) {
                const auto n=std::to_string(i);
                flip+="    tc"+n+".y = "+(tex_info[i].rect ? "texsize["+n+"].y" : "1.0")+" * tc"+n+".w - tc"+n+".y;\n";
            }
            if(!flip.empty()) ps.insert(ps.find(marker)+marker.size(),flip);
#endif
#ifdef PGR1_NATIVE_VULKAN
            shaders->spirv=CompileHlslPairSpirvAsync(std::move(vs),std::move(ps));
#else
            try { shaders->vertex=CompileHlsl(vs,true); shaders->fragment=CompileHlsl(ps,false); }
            catch(const std::exception& e) { Fail(e.what()); return; }
#endif
            pair=shaders;
            LOGR("%s: shader pair %llx/%llx",kBackendName,(unsigned long long)vh,(unsigned long long)ph);
        }
        c.shaders=pair; c.vertex_constants=s.vsc;
        // The SDK loads declaration constants on SetVertexShader. State::vsc also
        // includes later title writes; reapplying the declaration here would erase them.
        c.draw_constants.resize(sizeof(constants)); std::memcpy(c.draw_constants.data(),&constants,sizeof(constants));
        c.depth_slope_offset=offsetof(DrawConstants,wdepth)+sizeof(float);
        const unsigned stride=std::popcount(a.regs)*4;
        c.vertices.resize(a.vertex_count);
        // EN: The active-register layout is constant across this draw; do
        // not scan all 16 Xbox registers separately for every vertex.
        // PT-BR: O layout de registradores e fixo durante o draw; evite
        // percorrer os 16 registradores do Xbox a cada vertice.
        const VertexRegisterExpander expander(a.regs);
        for(unsigned i=0;i<a.vertex_count;i++) {
            const auto attrs=expander.Expand(a.vertices.data()+size_t(i)*stride);
            auto& o=c.vertices[i]; o={attrs[0],attrs[3],attrs[9],attrs};
        }
        if(a.index_count && a.indices.size()!=a.index_count) { Unsupported("unexpected assembler index path"); return; }
        c.topology=a.topology==Topology::Points ? 0 : a.topology==Topology::Lines ? (a.strip?3:1) : (a.strip?5:4);
#ifdef PGR1_NATIVE_VULKAN
        if(pk.flat && c.topology!=0) {
            auto flat=BuildFlatPrimitiveIndices(c.topology,a.vertex_count,a.indices);
            c.topology=flat.topology; c.indices=std::move(flat.indices);
        } else
#endif
            c.indices=a.indices;
#ifdef PGR1_NATIVE_VULKAN
        std::shared_future<uint64_t> visibility;
        if(s.simple[kMCountEnable/4]) {
            c.visibility=std::make_shared<std::promise<uint64_t>>();
            visibility=c.visibility->get_future().share();
        }
#endif
        frame_.geometry_vertices+=c.vertices.size();
        frame_.geometry_indices+=c.indices.size();
        frame_.geometry_bytes+=c.vertices.size()*sizeof(Vertex)+c.indices.size()*sizeof(uint32_t);
        MarkGpuWritten(c.target.color);
        if(c.target.depth)MarkGpuWritten(c.target.depth);
        frame_.commands.push_back(std::move(c));
        timer.supported=true;
#ifdef PGR1_NATIVE_VULKAN
        UseDraw(s);
        if(visibility.valid()) counted_.push_back(std::move(visibility));
#endif
    }
    void Present(const Surface& front) override {
        TextureDesc d; if(!DescribeSurface(front,d)) { Unsupported("present surface"); return; }
        frame_.front=d.data; frame_.frame=++frames_; frame_.draws=draws_; frame_.unsupported=unsupported_;
        harness::g_frames.store(frames_,std::memory_order_relaxed);
#ifdef PGR1_ENABLE_REFERENCE_COMPARE
        if(compare_frame_==frames_) {
            frame_.reference_width=d.width; frame_.reference_height=d.height;
            DecodeBgra(d,0,static_cast<const uint8_t*>(kernel::HostPtr(d.data)),frame_.reference_pixels);
        }
#endif
#ifdef PGR1_NATIVE_VULKAN
        Use(front.header);
#endif
        SubmitFrame();
#ifdef PGR1_ENABLE_REFERENCE_COMPARE
        std::error_code diagnostic_error;
        if(std::filesystem::is_regular_file("compare",diagnostic_error) && std::filesystem::remove("compare",diagnostic_error))
            compare_frame_=frames_+1;
        if(compare_frame_ && frames_+1==compare_frame_) {
            reference::BeginFrame();
            // Initialize the reference's retained targets from the actual GPU prefix.
            for(const auto& [address,surface]:targets_) Sync(address,0);
        }
#endif
        if(frames_%60==0) LOGI("%s: frames %llu draws %llu unsupported %llu",kBackendName,(unsigned long long)frames_,(unsigned long long)draws_,(unsigned long long)unsupported_);
    }
    void Copy(const Surface& src,const Surface& dst,uint32_t n,uint32_t r,uint32_t p) override {
#ifdef PGR1_ENABLE_REFERENCE_COMPARE
        if(compare_frame_ && frames_+1==compare_frame_) reference::Copy(src,dst,n,r,p);
#endif
        TextureDesc a,b;
        if(!DescribeSurface(src,a) || !DescribeSurface(dst,b)) { Unsupported("CopyRects surface"); return; }
        if(!gpu_written_.contains(a.data)) {
#ifdef PGR1_NATIVE_VULKAN
            std::optional<MemoryTracker::WriteGuard> write;
            if(Tracking())write.emplace(texture_tracker_,b.data,TotalSize(b));
#endif
            CopySurfaceMemory(src,dst,n,r,p); return;
        }
        if(a.data==b.data) { Unsupported("overlapping GPU CopyRects"); return; }
        Command c; c.kind=Command::Kind::Copy; c.source=a.data; c.source_height=a.height;
        c.target={b.data,0,b.width,b.height,b.format==0x07 || b.format==0x1e || b.format==0x11 || b.format==0x05};
        for(unsigned i=0;i<(n?n:1);i++) {
            Rect source{0,0,int(a.width),int(a.height)}; int x=0,y=0;
            if(n && r) { uint32_t rect[4]; std::memcpy(rect,kernel::HostPtr(r+i*16),16); source=Rectangle(rect); }
            if(n && p) { x=int(kernel::R32(p+i*8)); y=int(kernel::R32(p+i*8+4)); }
            c.rects.push_back(source); c.destination_rects.push_back({x,y,source.w,source.h});
        }
        c.target.format=b.format;
        targets_[b.data]={b,c.target}; MarkGpuWritten(b.data); frame_.commands.push_back(std::move(c));
#ifdef PGR1_NATIVE_VULKAN
        Use(src.header); Use(dst.header);
#endif
    }
    void Sync(uint32_t data,uint32_t size) override {
        if(sync_profile_) ++frame_.sync.sync_calls;
        for(const auto& [address,surface]:targets_) {
            const auto& [desc,target]=surface;
            if(!gpu_written_.contains(address) || !unread_.contains(address)) continue;
            // SDK passes size=0 for an exact resource, including every vertex stream.
            // Treating it as an overlapping byte range reads unrelated recycled targets.
            if(size==0 ? data!=address : (uint64_t(data)+size<=address || uint64_t(address)+TotalSize(desc)<=data)) continue;
            if(gpu_epoch_[address]!=DirtyEpoch(address)) { gpu_written_.erase(address); unread_.erase(address); continue; }
            Command c; c.kind=Command::Kind::Readback; c.target=target; c.read_depth=Format(desc.format).depth;
            c.readback=std::make_shared<std::promise<std::vector<uint32_t>>>(); auto future=c.readback->get_future();
            frame_.commands.push_back(std::move(c)); frame_.present=false; frame_.frame=frames_+1; SubmitFrame(SubmissionReason::Readback);
            std::vector<uint32_t> pixels;
            if(sync_profile_) ++frame_.sync.readback_waits;
            try { WaitTimer timer(sync_profile_,frame_.sync.readback_wait_ms); pixels=future.get(); } catch(const std::exception& e) { Fail(std::string("GPU readback cancelled: ")+e.what()); return; }
            try { WritePixels(address,desc,pixels); }
            catch(const std::exception& e) { Fail(e.what()); return; }
            unread_.erase(address);
        }
    }
    void SyncLate(uint32_t data) override {
        if(sync_profile_) ++frame_.sync.sync_late_calls;
        auto surface=targets_.find(data);
        if(surface==targets_.end() || !gpu_written_.contains(data)) return;
        if(auto old=late_.find(data);old!=late_.end()) {
            if(old->second.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
            try { WritePixels(data,old->second.desc,old->second.future.get()); }
            catch(const std::exception& e) { Fail(e.what()); }
            late_.erase(old);
        }
        Command c; c.kind=Command::Kind::Readback; c.target=surface->second.second;
        c.read_depth=Format(surface->second.first.format).depth;
        c.readback=std::make_shared<std::promise<std::vector<uint32_t>>>();
        late_.emplace(data,LateRead{surface->second.first,c.readback->get_future()});
        frame_.commands.push_back(std::move(c));
    }
    void Flush() override { FlushFor(SubmissionReason::Flush); }
    void SetGamma(const uint8_t*) override { Unsupported("gamma ramp"); }
#ifdef PGR1_NATIVE_VULKAN
    uint32_t Fence() override {
        // Prune completed tokens. A retired token is permanently nonpending;
        // resource futures retain their own references until those uses retire.
        for(auto it=fences_.begin();it!=fences_.end();) {
            if(!Pending(it->second)) it=fences_.erase(it); else ++it;
        }
        do { ++next_fence_; } while(!next_fence_ || fences_.contains(next_fence_));
        fences_[next_fence_]=Completion();
        FlushFor(SubmissionReason::Fence);
        return next_fence_;
    }
    bool FencePending(uint32_t fence) override {
        const auto found=fences_.find(fence);
        return found!=fences_.end() && Pending(found->second);
    }
    void WaitFence(uint32_t fence) override {
        if(sync_profile_) ++frame_.sync.fence_wait_calls;
        WaitTimer timer(sync_profile_,frame_.sync.fence_wait_ms);
        const auto found=fences_.find(fence);
        if(found!=fences_.end()) { WaitCompletion(found->second); fences_.erase(found); }
    }
    bool Busy(uint32_t resource) override {
        if(sync_profile_) ++frame_.sync.gpu_busy_poll_count;
        const auto found=resource_use_.find(resource);
        if(found==resource_use_.end()) return false;
        // A guest polling IsBusy must let the recorded prefix reach the device.
        FlushFor(SubmissionReason::ResourceBusy);
        if(Pending(found->second.completion)) return true;
        resource_use_.erase(found); return false;
    }
    void WaitResource(uint32_t resource) override {
        if(sync_profile_) ++frame_.sync.resource_wait_calls;
        const auto found=resource_use_.find(resource);
        if(found==resource_use_.end()) return;
        FlushFor(SubmissionReason::ResourceWait);
        WaitTimer timer(sync_profile_,frame_.sync.resource_wait_ms);
        WaitCompletion(found->second.completion); resource_use_.erase(found);
    }
    void ClearReport() override { if(sync_profile_) ++frame_.sync.visibility_clears; counted_.clear(); }
#endif
    void GetReport(uint32_t r) override {
#ifdef PGR1_NATIVE_VULKAN
        // A report is an ordered GPU operation, even with no counted draws.
        // Preserve the SDK's 16-byte time/count/status layout. Unsupported or
        // cancelled work leaves its pending status intact, never a fake result.
        if(sync_profile_) ++frame_.sync.visibility_reports;
        const auto completed=Completion();
        FlushFor(SubmissionReason::VisibilityReport);
        uint64_t count=0;
        constexpr uint64_t limit=UINT32_MAX;
        try {
            WaitTimer timer(sync_profile_,frame_.sync.visibility_report_wait_ms);
            completed.get();
            for(const auto& query:counted_) count=std::min(limit,count+std::min(limit,query.get()));
        } catch(const std::exception& e) {
            Fail(std::string("GPU visibility report cancelled: ")+e.what()); return;
        }
        kernel::W32(r,uint32_t(frames_)); kernel::W32(r+4,0); kernel::W32(r+8,uint32_t(count));
        std::atomic_thread_fence(std::memory_order_release);
        kernel::W32(r+12,0);
#else
        kernel::W32(r,uint32_t(frames_)); kernel::W32(r+4,0); kernel::W32(r+8,0); kernel::W32(r+12,0);
#endif
    }
};
}
std::unique_ptr<Backend> CreateBackend() { return std::make_unique<GLBackend>(); }
std::unique_ptr<Backend> CreateFallbackBackend() { return nullptr; }
}
