// Xbox state/assembly stays on the guest thread; only owned commands cross to GL.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <map>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <span>
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

namespace gpu {
namespace {
using namespace pgr::gldirect;
void DumpRejected(const char* kind,std::span<const uint32_t> words) {
    const auto hash=Hash(words.data(),words.size_bytes());
    std::ostringstream path; path<<"shadercache-gl-v1/rejected-"<<kind<<'-'<<std::hex<<hash<<".nv2a";
    std::error_code error; std::filesystem::create_directories("shadercache-gl-v1",error);
    std::ofstream file(path.str(),std::ios::binary);
    file.write(reinterpret_cast<const char*>(words.data()),words.size_bytes());
    LOGR("gldirect: rejected %s shader %llx; diagnostic %s (%s)",kind,(unsigned long long)hash,path.str().c_str(),file?"saved":"write failed");
}
Rect Rectangle(const uint32_t* r) { return {int(r[0]),int(r[1]),int(r[2])-int(r[0]),int(r[3])-int(r[1])}; }
class GLBackend final : public Backend {
    Submission frame_;
    std::set<const char*> warned_;
    std::set<uint32_t> gpu_written_, unread_;
    std::map<uint32_t,std::pair<TextureDesc,Target>> targets_;
    std::map<uint32_t,uint64_t> gpu_epoch_;
    struct LateRead { TextureDesc desc; std::future<std::vector<uint32_t>> future; };
    std::map<uint32_t,LateRead> late_;
    static void WritePixels(uint32_t address,const TextureDesc& desc,const std::vector<uint32_t>& pixels) {
            for(unsigned y=0;y<desc.height;y++) for(unsigned x=0;x<desc.width;x++) {
                const auto pixel=pixels[size_t(desc.height-1-y)*desc.width+x];
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
    std::map<std::pair<uint64_t,uint64_t>,std::shared_ptr<const ShaderPair>> shaders_;
    struct CachedPixels {
        TextureDesc desc{};
        std::shared_ptr<const TexturePixels> pixels;
        // Exact byte snapshots let libc's vectorized comparison check every bind,
        // including direct CPU writes made without Lock/MarkDirty in this frame.
        std::vector<uint8_t> guest;
        uint64_t used=0;
    };
    std::map<uint32_t,CachedPixels> textures_;
    uint64_t texture_clock_=0;

    void Unsupported(const char* reason) {
        ++unsupported_;
        if(warned_.insert(reason).second) LOGR("gldirect: unsupported %s (draws remain counted in window)",reason);
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
        if(const char* compare=std::getenv("PGR1_COMPARE_FRAME")) compare_frame_=std::strtoull(compare,nullptr,10);
        LOGR("gldirect: GPU FVF XYZRHW renderer %ux%u; SDK HLSL/SPIR-V/GLSL shaders enabled",w,h);
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
        gpu_written_.insert(c.target.color); unread_.insert(c.target.color); gpu_epoch_[c.target.color]=DirtyEpoch(c.target.color);
        if(c.target.depth) { gpu_written_.insert(c.target.depth); unread_.insert(c.target.depth); gpu_epoch_[c.target.depth]=DirtyEpoch(c.target.depth); }
        frame_.commands.push_back(std::move(c));
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
        if(program) for(size_t i=0;i+3<s.vs->program.size();i+=4) {
            unsigned op=(s.vs->program[i+1]>>21)&15;
            if(op>13) { Unsupported("unknown NV2A MAC opcode"); DumpRejected("vs",s.vs->program); LOGR("gldirect: VS %llx instruction %zu opcode %u",(unsigned long long)s.vs->hash,i/4,op); return; }
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
            if(d.cube) for(unsigned face=0;face<6;face++) {
                const auto address=d.data+face*FaceSize(d);
                if(gpu_written_.contains(address)) t.face_targets[face]=address;
            }
            if(!d.cube && gpu_written_.contains(d.data)) {
                if(d.data==c.target.color) { Unsupported("render-target feedback loop"); return; }
                t.render_target=d.data; tex_info[stage].flags|=1;
                if(NoAlphaFormat(d.format)) tex_info[stage].flags|=kTexOpaque;
                continue;
            }
            if(textures_.size()>=512 && !textures_.contains(d.data)) {
                // Evict one least recently bound snapshot, not the whole scene.
                // Commands retain shared ownership until the GPU consumes them.
                auto oldest=std::min_element(textures_.begin(),textures_.end(),[](const auto& a,const auto& b) { return a.second.used<b.second.used; });
                textures_.erase(oldest);
            }
            auto& cached=textures_[d.data];
            cached.used=++texture_clock_;
            const auto check_start=std::chrono::steady_clock::now();
            const auto bytes=TotalSize(d);
            const auto* guest=static_cast<const uint8_t*>(kernel::HostPtr(d.data));
            const bool changed=!cached.pixels || cached.desc.format!=d.format || cached.desc.width!=d.width ||
               cached.desc.height!=d.height || cached.desc.levels!=d.levels || cached.desc.cube!=d.cube || cached.desc.pitch!=d.pitch ||
               cached.guest.size()!=bytes || std::memcmp(guest,cached.guest.data(),bytes)!=0;
            frame_.texture_check_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-check_start).count();
            if(changed) {
                const auto decode_start=std::chrono::steady_clock::now();
                auto pixels=std::make_shared<TexturePixels>();
                for(unsigned face=0;face<(d.cube?6u:1u);face++) for(unsigned level=0;level<d.levels;level++) {
                    std::vector<uint32_t> decoded;
                    if(!DecodeBgra(d,level,static_cast<const uint8_t*>(kernel::HostPtr(d.data))+face*FaceSize(d),decoded)) {
                        Unsupported("texture decoder format"); return;
                    }
                    pixels->levels.push_back(std::move(decoded));
                }
                cached.desc=d; cached.pixels=pixels; cached.guest.assign(guest,guest+bytes);
                frame_.decode_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-decode_start).count();
            }
            t.pixels=cached.pixels;
        }
        Assembled a;
        const auto assembly_start=std::chrono::steady_clock::now();
        if(!Assemble(s,prim,start,count,indices,a)) return;
        frame_.assemble_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-assembly_start).count();
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
        uint64_t vh=program ? Hash(s.vs->program.data(),s.vs->program.size()*4) : Hash(&fk,sizeof(fk));
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
            const std::string marker="    float4 tc0 = i.tc0, tc1 = i.tc1, tc2 = i.tc2, tc3 = i.tc3;\n";
            std::string flip;
            for(unsigned i=0;i<4;i++) if(tex_info[i].flags&1) {
                const auto n=std::to_string(i);
                flip+="    tc"+n+".y = "+(tex_info[i].rect ? "texsize["+n+"].y" : "1.0")+" * tc"+n+".w - tc"+n+".y;\n";
            }
            if(!flip.empty()) ps.insert(ps.find(marker)+marker.size(),flip);
            try { shaders->vertex=CompileHlsl(vs,true); shaders->fragment=CompileHlsl(ps,false); }
            catch(const std::exception& e) { Fail(e.what()); return; }
            pair=shaders;
            LOGR("gldirect: translated shader pair %llx/%llx",(unsigned long long)vh,(unsigned long long)ph);
        }
        c.shaders=pair; c.vertex_constants=s.vsc;
        // The SDK loads declaration constants on SetVertexShader. State::vsc also
        // includes later title writes; reapplying the declaration here would erase them.
        c.draw_constants.resize(sizeof(constants)); std::memcpy(c.draw_constants.data(),&constants,sizeof(constants));
        c.depth_slope_offset=offsetof(DrawConstants,wdepth)+sizeof(float);
        const unsigned stride=std::popcount(a.regs)*4;
        c.vertices.resize(a.vertex_count);
        for(unsigned i=0;i<a.vertex_count;i++) {
            std::array<std::array<float,4>,16> attrs{};
            for(unsigned r=0;r<16;r++) attrs[r]=r==3 ? std::array<float,4>{1,1,1,1} : std::array<float,4>{0,0,0,1};
            unsigned offset=0;
            for(unsigned r=0;r<16;r++) if(a.regs&(1u<<r)) { std::copy_n(a.vertices.data()+i*stride+offset,4,attrs[r].data()); offset+=4; }
            auto& o=c.vertices[i]; o={attrs[0],attrs[3],attrs[9],attrs};

        }
        timer.supported=true;
        c.indices=a.indices;
        if(a.index_count && c.indices.size()!=a.index_count) { Unsupported("unexpected assembler index path"); return; }
        c.topology=a.topology==Topology::Points ? 0 : a.topology==Topology::Lines ? (a.strip?3:1) : (a.strip?5:4);
        gpu_written_.insert(c.target.color); unread_.insert(c.target.color); gpu_epoch_[c.target.color]=DirtyEpoch(c.target.color);
        if(c.target.depth) { gpu_written_.insert(c.target.depth); unread_.insert(c.target.depth); gpu_epoch_[c.target.depth]=DirtyEpoch(c.target.depth); }
        frame_.commands.push_back(std::move(c));
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
        Submit(std::move(frame_)); frame_={};
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
        if(frames_%60==0) LOGI("gldirect: frames %llu draws %llu unsupported %llu",(unsigned long long)frames_,(unsigned long long)draws_,(unsigned long long)unsupported_);
    }
    void Copy(const Surface& src,const Surface& dst,uint32_t n,uint32_t r,uint32_t p) override {
#ifdef PGR1_ENABLE_REFERENCE_COMPARE
        if(compare_frame_ && frames_+1==compare_frame_) reference::Copy(src,dst,n,r,p);
#endif
        TextureDesc a,b;
        if(!DescribeSurface(src,a) || !DescribeSurface(dst,b)) { Unsupported("CopyRects surface"); return; }
        if(!gpu_written_.contains(a.data)) { CopySurfaceMemory(src,dst,n,r,p); return; }
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
        targets_[b.data]={b,c.target}; gpu_written_.insert(b.data); unread_.insert(b.data); gpu_epoch_[b.data]=DirtyEpoch(b.data); frame_.commands.push_back(std::move(c));
    }
    void Sync(uint32_t data,uint32_t size) override {
        for(const auto& [address,surface]:targets_) {
            const auto& [desc,target]=surface;
            if(!gpu_written_.contains(address) || !unread_.contains(address)) continue;
            // SDK passes size=0 for an exact resource, including every vertex stream.
            // Treating it as an overlapping byte range reads unrelated recycled targets.
            if(size==0 ? data!=address : (uint64_t(data)+size<=address || uint64_t(address)+TotalSize(desc)<=data)) continue;
            if(gpu_epoch_[address]!=DirtyEpoch(address)) { gpu_written_.erase(address); unread_.erase(address); continue; }
            Command c; c.kind=Command::Kind::Readback; c.target=target; c.read_depth=Format(desc.format).depth;
            c.readback=std::make_shared<std::promise<std::vector<uint32_t>>>(); auto future=c.readback->get_future();
            frame_.commands.push_back(std::move(c)); frame_.present=false; frame_.frame=frames_+1; Submit(std::move(frame_)); frame_={};
            std::vector<uint32_t> pixels;
            try { pixels=future.get(); } catch(const std::exception& e) { Fail(std::string("GPU readback cancelled: ")+e.what()); return; }
            WritePixels(address,desc,pixels); unread_.erase(address);
        }
    }
    void SyncLate(uint32_t data) override {
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
    void Flush() override { if(!frame_.commands.empty()) { frame_.present=false; frame_.frame=frames_+1; Submit(std::move(frame_)); frame_={}; } }
    void SetGamma(const uint8_t*) override { Unsupported("gamma ramp"); }
    void GetReport(uint32_t r) override {
        kernel::W32(r,uint32_t(frames_)); kernel::W32(r+4,0); kernel::W32(r+8,0); kernel::W32(r+12,0);
    }
};
}
std::unique_ptr<Backend> CreateBackend() { return std::make_unique<GLBackend>(); }
std::unique_ptr<Backend> CreateFallbackBackend() { return nullptr; }
}
