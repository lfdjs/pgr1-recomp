// Xbox state/assembly stays on the guest thread; only owned commands cross to GL.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
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
Rect Rectangle(const uint32_t* r) { return {int(r[0]),int(r[1]),int(r[2])-int(r[0]),int(r[3])-int(r[1])}; }
class GLBackend final : public Backend {
    Submission frame_;
    std::set<const char*> warned_;
    std::set<uint32_t> gpu_written_;
    std::uint64_t frames_=0, draws_=0, unsupported_=0;
    void Unsupported(const char* reason) {
        ++unsupported_;
        if(warned_.insert(reason).second) LOGR("gldirect: unsupported %s (draws remain counted in window)",reason);
    }
    bool TargetOf(const State& s, Target& t) {
        TextureDesc rt, zs;
        if(!DescribeSurface(s.rt,rt) || !rt.width || !rt.height || rt.width>8192 || rt.height>8192 ||
           (rt.format!=0x06 && rt.format!=0x07 && rt.format!=0x12 && rt.format!=0x1e)) {
            Unsupported("render target format/size"); return false;
        }
        t={rt.data,0,rt.width,rt.height,rt.format==0x07 || rt.format==0x1e};
        if(s.zs.data) {
            if(!DescribeSurface(s.zs,zs) || zs.width!=rt.width || zs.height!=rt.height ||
               (zs.format!=0x2a && zs.format!=0x2e)) { Unsupported("depth format/size (requires D24S8)"); return false; }
            t.depth=zs.data;
        }
        return true;
    }
public:
    bool Init(uint32_t w,uint32_t h) override {
        // Assemble's converted path still uses the SDK's exact Xbox attribute decoders.
        // No global flag is toggled around individual draws.
        g_raw_vertices=false;
        LOGR("gldirect: GPU FVF XYZRHW renderer %ux%u; programmable shaders unsupported",w,h);
        return true;
    }
    void Clear(const State& s,uint32_t n,const uint8_t* rects,uint32_t flags,uint32_t color,float z,uint32_t stencil) override {
        Command c; if(!TargetOf(s,c.target)) return;
        c.flags=flags; c.color=color; c.z=z; c.stencil=stencil;
        if(n && rects) for(uint32_t i=0;i<n;i++) { uint32_t r[4]; std::memcpy(r,rects+i*16,16); c.rects.push_back(Rectangle(r)); }
        if(c.rects.empty()) c.rects.push_back({int(s.viewport.x),int(s.viewport.y),int(s.viewport.w),int(s.viewport.h)});
        gpu_written_.insert(c.target.color);
        frame_.commands.push_back(std::move(c));
    }
    void Draw(const State& s,uint32_t prim,uint32_t start,uint32_t count,const uint8_t* indices) override {
        ++draws_;
        if((s.vs_handle&1) || s.ps) { Unsupported("programmable vertex/pixel shader"); return; }
        if((s.vs_handle&0xe)!=4) { Unsupported("non-XYZRHW fixed function transform"); return; }
        if(s.rs[kRsFogEnable] || s.rs[kRsPointSpriteEnable] || s.rs[kRsZEnable]==2 || s.aa_x!=1 || s.aa_y!=1 || s.rs[kRsZBias]) {
            Unsupported("fog/point sprites/W-buffer/MSAA/depth bias"); return;
        }
        if(s.simple[kMShadeMode/4]==0x1d00) { Unsupported("flat shading"); return; }
        if(s.scissor_count>1) { Unsupported("multiple scissor rectangles"); return; }
        Command c; c.kind=Command::Kind::Draw; if(!TargetOf(s,c.target)) return;
        auto& p=c.pipeline; const auto& v=s.viewport;
        if(!v.w || !v.h) return;
        p.viewport={int(v.x),int(v.y),int(v.w),int(v.h)}; p.min_z=v.min_z; p.max_z=v.max_z;
        auto m=[&](unsigned key) { return s.simple[key/4]; };
        p.depth=s.rs[kRsZEnable] && c.target.depth; p.depth_write=m(kMZWriteEnable); p.depth_func=m(kMZFunc);
        p.blend=m(kMBlendEnable); p.src_blend=m(kMSrcBlend); p.dst_blend=m(kMDestBlend); p.blend_op=m(kMBlendOp); p.blend_color=m(kMBlendColor);
        p.color_mask=m(kMColorWriteEnable); p.cull=s.rs[kRsCullMode]; p.front=s.rs[kRsFrontFace];
        p.stencil=s.rs[kRsStencilEnable] && c.target.depth; p.stencil_func=m(kMStencilFunc); p.stencil_ref=m(kMStencilRef);
        p.stencil_mask=m(kMStencilMask); p.stencil_write=m(kMStencilWriteMask);
        p.stencil_fail=s.rs[kRsStencilFail]; p.stencil_zfail=m(kMStencilZFail); p.stencil_pass=m(kMStencilPass);
        p.alpha_test=m(kMAlphaTestEnable); p.alpha_func=m(kMAlphaFunc); p.alpha_ref=m(kMAlphaRef)&255;
        for(unsigned i=0;i<std::min(s.scissor_count,8u);i++) p.scissors.push_back(Rectangle(s.scissors.data()+i*4));
        for(unsigned i=1;i<4;i++) if(s.tss[i][kTssColorOp]!=1) { Unsupported("multiple texture stages"); return; }
        const auto& t=s.tss[0];
        bool linear_texture=false;
        if(t[kTssColorOp]!=1) {
            // CURRENT is diffuse at the first stage, so both selectors are equivalent.
            auto operation=[](unsigned op,unsigned a,unsigned b) -> unsigned {
                auto diffuse=[](unsigned arg) { return arg==0 || arg==1; };
                if(op==2) return a==2 ? 2 : diffuse(a) ? 3 : 0;
                if(op==3) return b==2 ? 2 : diffuse(b) ? 3 : 0;
                if(op==4 && ((a==2 && diffuse(b)) || (b==2 && diffuse(a)))) return 4;
                return 0;
            };
            p.color_op=operation(t[kTssColorOp],t[kTssColorArg1],t[kTssColorArg2]);
            p.alpha_op=operation(t[kTssAlphaOp],t[kTssAlphaArg1],t[kTssAlphaArg2]);
            if(!p.color_op || !p.alpha_op || t[kTssTextureTransformFlags] || t[kTssTexCoordIndex]!=0 ||
               t[kTssAddressU]<1 || t[kTssAddressU]>3 || t[kTssAddressV]<1 || t[kTssAddressV]>3 ||
               t[kTssMinFilter]<1 || t[kTssMinFilter]>2 || t[kTssMagFilter]<1 || t[kTssMagFilter]>2 ||
               t[kTssMipFilter]!=0 || t[kTssAlphaKill]) {
                Unsupported("texture stage arguments/transform, border addressing or mip filtering"); return;
            }
            TextureDesc d;
            if(s.textures[0] && Describe(s.textures[0],d) && gpu_written_.contains(d.data)) {
                Unsupported("sampling a GPU render target (requires texture barrier/resolve)"); return;
            }
            if(!s.textures[0] || !Describe(s.textures[0],d) || d.cube || d.format==0x0b || d.depth!=1 || d.width>4096 || d.height>4096 ||
               !DecodeBgra(d,0,static_cast<const uint8_t*>(kernel::HostPtr(d.data)),c.texture.bgra)) {
                Unsupported("texture format or cube/volume texture"); return;
            }
            c.texture.width=d.width; c.texture.height=d.height;
            c.texture.wrap_u=t[kTssAddressU]; c.texture.wrap_v=t[kTssAddressV];
            c.texture.min_filter=t[kTssMinFilter]; c.texture.mag_filter=t[kTssMagFilter];
            linear_texture=d.linear;
        }
        Assembled a;
        if(!Assemble(s,prim,start,count,indices,a)) return;
        if(!a.pretransformed || a.vertices.empty() || !(a.regs&1)) { Unsupported("raw or missing position attributes"); return; }
        const unsigned stride=std::popcount(a.regs)*4;
        c.vertices.resize(a.vertex_count);
        for(unsigned i=0;i<a.vertex_count;i++) {
            std::array<std::array<float,4>,16> attrs=s.vattr;
            unsigned offset=0;
            for(unsigned r=0;r<16;r++) if(a.regs&(1u<<r)) { std::copy_n(a.vertices.data()+i*stride+offset,4,attrs[r].data()); offset+=4; }
            auto& o=c.vertices[i]; o={attrs[0],attrs[3],attrs[9]};
            if(!std::isfinite(o.position[3]) || o.position[3]<=0) { Unsupported("invalid XYZRHW"); return; }
            if(v.max_z!=v.min_z) o.position[2]=(o.position[2]-v.min_z)/(v.max_z-v.min_z);
            if(linear_texture) { o.uv[0]/=c.texture.width; o.uv[1]/=c.texture.height; }
        }
        c.indices=a.indices;
        if(a.index_count && c.indices.size()!=a.index_count) { Unsupported("unexpected assembler index path"); return; }
        c.topology=a.topology==Topology::Points ? 0 : a.topology==Topology::Lines ? (a.strip?3:1) : (a.strip?5:4);
        gpu_written_.insert(c.target.color);
        frame_.commands.push_back(std::move(c));
    }
    void Present(const Surface& front) override {
        TextureDesc d; if(!DescribeSurface(front,d)) { Unsupported("present surface"); return; }
        frame_.front=d.data; frame_.frame=++frames_; frame_.draws=draws_; frame_.unsupported=unsupported_;
        harness::g_frames.store(frames_,std::memory_order_relaxed);
        Submit(std::move(frame_)); frame_={};
        if(frames_%60==0) LOGI("gldirect: frames %llu draws %llu unsupported %llu",(unsigned long long)frames_,(unsigned long long)draws_,(unsigned long long)unsupported_);
    }
    void Copy(const Surface& src,const Surface& dst,uint32_t n,uint32_t r,uint32_t p) override {
        Unsupported("GPU CopyRects/readback (guest memory copy only)"); CopySurfaceMemory(src,dst,n,r,p);
    }
    void Sync(uint32_t,uint32_t) override { Unsupported("GPU readback"); }
    void SyncLate(uint32_t) override { Unsupported("late GPU readback"); }
    void Flush() override {}
    void SetGamma(const uint8_t*) override { Unsupported("gamma ramp"); }
    void GetReport(uint32_t r) override {
        kernel::W32(r,uint32_t(frames_)); kernel::W32(r+4,0); kernel::W32(r+8,0); kernel::W32(r+12,0);
    }
};
}
std::unique_ptr<Backend> CreateBackend() { return std::make_unique<GLBackend>(); }
std::unique_ptr<Backend> CreateFallbackBackend() { return nullptr; }
}
