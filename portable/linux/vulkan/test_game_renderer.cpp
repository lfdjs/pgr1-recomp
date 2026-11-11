// Synthetic SDK shader/command integration tests. This is not game evidence.
#include "game_renderer.h"
#include "../gldirect/commands.h"
#include "../gldirect/draw_constants.h"
#include "../gldirect/shader_compiler.h"
#include "gpu/shaders.h"
#include "app/app.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <vector>

namespace app {
const GameInfo kGame{};
Settings& Get() { static Settings s; return s; }
}
namespace {
using namespace pgr::gldirect;
constexpr unsigned size=32;
const Target target{1,2,size,size};

gpu::State State() {
    gpu::State state;
    state.viewport={0,0,size,size,0,1};
    for(auto& t:state.tss) t[gpu::kTssColorOp]=1;
    return state;
}
std::shared_ptr<ShaderPair> Shaders(const gpu::State& state,bool texture=false) {
    auto pair=std::make_shared<ShaderPair>();
    gpu::TexInfo info[4]{};
    if(texture) info[0].kind=gpu::kTex2D;
    pair->spirv=CompileHlslPairSpirvAsync(gpu::FixedVertexHlsl(texture?0x209:9,true),
                                      gpu::PixelHlsl(gpu::MakePixelKey(state,info)));
    return pair;
}
Command Clear(uint32_t color=0xff000000) {
    Command c;
    c.target=target;
    c.flags=0xf3;
    c.color=color;
    c.rects={{0,0,size,size}};
    return c;
}
Command Quad(const gpu::State& state,const std::shared_ptr<ShaderPair>& shaders,
             std::array<float,4> color,float depth=.5f) {
    Command c;
    c.kind=Command::Kind::Draw;
    c.target=target;
    c.pipeline.viewport={0,0,size,size};
    const std::array<std::array<float,4>,4> positions={{{-.5f,-.5f,depth,1},
        {31.5f,-.5f,depth,1},{31.5f,31.5f,depth,1},{-.5f,31.5f,depth,1}}};
    for(const auto& position:positions) {
        Vertex v{};
        v.position=position; v.color=color; v.uv={.5f,.5f,0,1};
        v.attributes[0]=position; v.attributes[3]=color; v.attributes[9]=v.uv;
        c.vertices.push_back(v);
    }
    c.indices={0,1,2,0,2,3};
    c.shaders=shaders;
    const auto constants=Constants(state,size,size,false);
    c.draw_constants.resize(sizeof(constants));
    std::memcpy(c.draw_constants.data(),&constants,sizeof(constants));
    return c;
}
std::vector<uint32_t> Execute(pgr::vulkan::Renderer& renderer,std::vector<Command> commands,
                              Target read_target=target,bool depth=false) {
    Command read;
    read.kind=Command::Kind::Readback;
    read.target=read_target;
    read.read_depth=depth;
    read.readback=std::make_shared<std::promise<std::vector<uint32_t>>>();
    auto future=read.readback->get_future();
    commands.push_back(std::move(read));
    Submission submission;
    submission.present=false;
    submission.front=read_target.color;
    submission.commands=std::move(commands);
    assert(renderer.Render(submission));
    assert(future.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    auto pixels=future.get();
    assert(pixels.size()==size_t(read_target.width)*read_target.height);
    return pixels;
}
void Pixel(const std::vector<uint32_t>& pixels,unsigned x,unsigned y,uint32_t expected,int tolerance=1) {
    const auto actual=pixels[y*size+x];
    for(unsigned shift:{0u,8u,16u,24u}) {
        if(std::abs(int((actual>>shift)&255)-int((expected>>shift)&255))>tolerance) {
            std::fprintf(stderr,"Vulkan pixel %u,%u: BGRA=%08x expected=%08x\n",x,y,actual,expected);
            std::abort();
        }
    }
}
}

int main() {
    // No window/swapchain: this path validates Vulkan draw/resource work on an
    // available ICD, including lavapipe. It does not validate physical AMD/NVIDIA.
    pgr::vulkan::Renderer renderer(nullptr);
    const auto state=State();
    const auto shaders=Shaders(state);
    auto red=Quad(state,shaders,{1,0,0,1});
    auto pixels=Execute(renderer,{Clear(),red});
    Pixel(pixels,16,16,0xffff0000);
    assert(renderer.LastStats().accepted==1 && renderer.LastStats().indexed==1);
    assert(renderer.LastStats().rejected==0);

    // The nonindexed path consumes owned vertex data, preserving the same shader.
    auto green=Quad(state,shaders,{0,1,0,1});
    const auto four=green.vertices;
    green.vertices={four[0],four[1],four[2],four[0],four[2],four[3]};
    green.indices.clear();
    pixels=Execute(renderer,{green});
    Pixel(pixels,16,16,0xff00ff00);
    assert(renderer.LastStats().nonindexed==1);

    // Match Xbox/OpenGL winding after the Vulkan shader's Y inversion.
    auto culled=red;culled.pipeline.cull=0x900;
    pixels=Execute(renderer,{Clear(),culled});Pixel(pixels,16,16,0xff000000);
    culled.pipeline.cull=0x901;
    pixels=Execute(renderer,{culled});Pixel(pixels,16,16,0xffff0000);

    // Retained color/depth attachments and top-left scissor coordinates.
    red.pipeline.depth=true;red.pipeline.depth_write=true;red.pipeline.depth_func=0x201;
    auto blue=Quad(state,shaders,{0,0,1,1},.8f);blue.pipeline=red.pipeline;
    green=Quad(state,shaders,{0,1,0,1},.2f);green.pipeline=red.pipeline;
    green.pipeline.scissors={{0,0,8,8}};
    pixels=Execute(renderer,{Clear(),red,blue,green});
    Pixel(pixels,16,16,0xffff0000);
    Pixel(pixels,2,2,0xff00ff00);
    Pixel(pixels,2,29,0xffff0000);
    pixels=Execute(renderer,{blue});
    Pixel(pixels,16,16,0xffff0000);

    // Xbox visibility reports require exact passing-sample counts. These are
    // device query results, including depth and scissor, never draw/frame counts.
    auto visible=red;
    visible.visibility=std::make_shared<std::promise<uint64_t>>();
    auto visible_count=visible.visibility->get_future();
    pixels=Execute(renderer,{Clear(),visible});Pixel(pixels,16,16,0xffff0000);
    assert(visible_count.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    assert(visible_count.get()==1024);
    auto hidden=blue;
    hidden.visibility=std::make_shared<std::promise<uint64_t>>();
    auto hidden_count=hidden.visibility->get_future();
    pixels=Execute(renderer,{hidden});Pixel(pixels,16,16,0xffff0000);
    assert(hidden_count.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    assert(hidden_count.get()==0);
    auto clipped=green;
    clipped.visibility=std::make_shared<std::promise<uint64_t>>();
    auto clipped_count=clipped.visibility->get_future();
    pixels=Execute(renderer,{clipped});Pixel(pixels,2,2,0xff00ff00);
    assert(clipped_count.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    assert(clipped_count.get()==64);

    // Basic stencil write then compare, without changing the color attachment.
    auto mask=Quad(state,shaders,{1,1,1,1});
    mask.pipeline.color_mask=0;mask.pipeline.stencil=true;mask.pipeline.stencil_ref=3;
    mask.pipeline.stencil_pass=0x1e01;mask.pipeline.scissors={{0,0,8,8}};
    blue=Quad(state,shaders,{0,0,1,1});blue.pipeline.stencil=true;
    blue.pipeline.stencil_ref=3;blue.pipeline.stencil_func=0x202;
    pixels=Execute(renderer,{Clear(),mask,blue});
    Pixel(pixels,2,2,0xff0000ff);Pixel(pixels,16,16,0xff000000);

    // Standard source-alpha blend applies the guest blend equation to RGBA.
    blue=Quad(state,shaders,{0,0,1,.5f});blue.pipeline.blend=true;
    blue.pipeline.src_blend=0x302;blue.pipeline.dst_blend=0x303;
    pixels=Execute(renderer,{Clear(0xffff0000),blue});
    Pixel(pixels,16,16,0xbf800080);

    // Texture bytes remain owned by a command even after producer-side release.
    auto textured_state=State();
    textured_state.tss[0][gpu::kTssColorOp]=textured_state.tss[0][gpu::kTssAlphaOp]=2;
    textured_state.tss[0][gpu::kTssColorArg1]=textured_state.tss[0][gpu::kTssAlphaArg1]=2;
    auto tex=Quad(textured_state,Shaders(textured_state,true),{1,1,1,1});
    auto texture=std::make_shared<TexturePixels>();texture->levels={{0xff00ff00}};
    tex.textures[0].width=tex.textures[0].height=1;tex.textures[0].pixels=texture;
    texture.reset();
    pixels=Execute(renderer,{Clear(),tex});Pixel(pixels,16,16,0xff00ff00);
    auto changed=std::make_shared<TexturePixels>();changed->levels={{0xff0000ff}};
    tex.textures[0].pixels=changed;
    pixels=Execute(renderer,{tex});Pixel(pixels,16,16,0xff0000ff);

    // These sampler values are NV2A values interpreted by the SDK's Address(),
    // not desktop D3D enums. In particular value5 clamps; mirror-once would
    // sample green at this negative coordinate and silently alter game textures.
    auto clamped=tex;
    auto pair_pixels=std::make_shared<TexturePixels>();pair_pixels->levels={{0xffff0000,0xff00ff00}};
    clamped.textures[0].width=2;clamped.textures[0].height=1;
    clamped.textures[0].pixels=pair_pixels;clamped.textures[0].wrap_u=5;
    for(auto& v:clamped.vertices)v.attributes[9]={-.75f,.5f,0,1};
    pixels=Execute(renderer,{clamped});Pixel(pixels,16,16,0xffff0000);

    // Alpha test is emitted by PixelHlsl and consumes the shared DC layout.
    auto alpha_state=textured_state;
    alpha_state.simple[gpu::kMAlphaTestEnable/4]=1;
    alpha_state.simple[gpu::kMAlphaFunc/4]=0x204;
    alpha_state.simple[gpu::kMAlphaRef/4]=128;
    auto alpha=Quad(alpha_state,Shaders(alpha_state,true),{1,1,1,1});
    auto transparent=std::make_shared<TexturePixels>();transparent->levels={{0x0000ff00}};
    alpha.textures=tex.textures;alpha.textures[0].pixels=transparent;
    pixels=Execute(renderer,{Clear(0xffff0000),alpha});Pixel(pixels,16,16,0xffff0000);

    // Restore the known source image before exercising an ordered GPU copy.
    pixels=Execute(renderer,{tex});Pixel(pixels,16,16,0xff0000ff);

    // Transfer commands share ordering with attachment writes and CPU readback.
    Command copy;copy.kind=Command::Kind::Copy;copy.source=target.color;copy.source_height=size;
    copy.target={3,0,size,size};copy.rects={{0,0,size,size}};copy.destination_rects=copy.rects;
    pixels=Execute(renderer,{copy},copy.target);Pixel(pixels,16,16,0xff0000ff);

    // All guest mip levels are uploaded, and the sampler's min LOD reaches the
    // final level even when constant coordinates would otherwise select LOD0.
    auto mip=tex;
    auto chain=std::make_shared<TexturePixels>();
    chain->levels={std::vector<uint32_t>(16,0xffff0000),
                   std::vector<uint32_t>(4,0xff00ff00),{0xff0000ff}};
    mip.textures[0].width=mip.textures[0].height=4;mip.textures[0].pixels=chain;
    mip.textures[0].mip_filter=1;mip.textures[0].max_mip=2;
    pixels=Execute(renderer,{Clear(),mip});Pixel(pixels,16,16,0xff0000ff);
    mip.textures[0].mip_filter=0; // SDK MinLOD still selects level 2 without mip interpolation.
    pixels=Execute(renderer,{Clear(),mip});Pixel(pixels,16,16,0xff0000ff);

    // Each cube side must map to its Xbox face rather than a flat 2D fallback.
    auto cube_pair=std::make_shared<ShaderPair>();
    gpu::TexInfo cube_info[4]{};cube_info[0].kind=gpu::kTexCube;
    cube_pair->spirv=CompileHlslPairSpirvAsync(gpu::FixedVertexHlsl(0x209,true),
        gpu::PixelHlsl(gpu::MakePixelKey(textured_state,cube_info)));
    auto cube=Quad(textured_state,cube_pair,{1,1,1,1});
    auto faces=std::make_shared<TexturePixels>();
    const std::array<uint32_t,6> face_colors={0xffff0000,0xff00ff00,0xff0000ff,
                                             0xffffff00,0xffff00ff,0xff00ffff};
    for(auto color:face_colors)faces->levels.push_back({color});
    cube.textures[0].width=cube.textures[0].height=1;cube.textures[0].cube=true;
    cube.textures[0].pixels=faces;
    const std::array<std::array<float,4>,6> directions={{{1,0,0,1},{-1,0,0,1},
        {0,1,0,1},{0,-1,0,1},{0,0,1,1},{0,0,-1,1}}};
    for(unsigned face=0;face<6;face++) {
        for(auto& v:cube.vertices)v.attributes[9]=directions[face];
        pixels=Execute(renderer,{Clear(),cube});Pixel(pixels,16,16,face_colors[face]);
    }
    // Distinct quadrant colors catch an upside-down or mirrored cube face,
    // which uniform face colors cannot detect in vehicle-reflection tests.
    auto oriented=cube;
    auto quadrants=std::make_shared<TexturePixels>();
    const std::vector<uint32_t> corners={0xffffffff,0xffff0000,0xff00ff00,0xff0000ff};
    quadrants->levels.resize(6,corners);
    oriented.textures[0].width=oriented.textures[0].height=2;
    oriented.textures[0].pixels=quadrants;
    const std::array<std::array<float,3>,6> right={{{0,0,-1},{0,0,1},{1,0,0},
        {1,0,0},{1,0,0},{-1,0,0}}};
    const std::array<std::array<float,3>,6> down={{{0,-1,0},{0,-1,0},{0,0,1},
        {0,0,-1},{0,-1,0},{0,-1,0}}};
    for(unsigned face=0;face<6;face++)for(unsigned corner=0;corner<4;corner++) {
        auto direction=directions[face];
        for(unsigned axis=0;axis<3;axis++)
            direction[axis]+=(corner&1?.5f:-.5f)*right[face][axis]+(corner&2?.5f:-.5f)*down[face][axis];
        for(auto& v:oriented.vertices)v.attributes[9]=direction;
        pixels=Execute(renderer,{oriented});Pixel(pixels,16,16,corners[corner]);
    }
    // A color target can replace a cube face; subsequent writes must invalidate
    // the sampled face without modifying the other five snapshots.
    auto source_clear=Clear(0xff00ff00);source_clear.target=copy.target;
    cube.textures[0].face_targets[0]=copy.target.color;
    for(auto& v:cube.vertices)v.attributes[9]=directions[0];
    pixels=Execute(renderer,{source_clear,cube});Pixel(pixels,16,16,0xff00ff00);
    source_clear.color=0xffff0000;
    pixels=Execute(renderer,{source_clear,cube});Pixel(pixels,16,16,0xffff0000);
    // The same reflection cube is sampled twice with no intervening GPU
    // source write. It should create no image or re-blit any unchanged face.
    pixels=Execute(renderer,{cube,cube});Pixel(pixels,16,16,0xffff0000);
    assert(renderer.LastStats().cube_cache_hits>=2);
    assert(renderer.LastStats().cube_cache_creates==0);
    assert(renderer.LastStats().cube_face_copies==0);
    // If a car reflection face changes *within a submission*, the second
    // draw must see it; only that one face should be re-copied.
    source_clear.color=0xff00ff00;
    pixels=Execute(renderer,{cube,source_clear,cube});Pixel(pixels,16,16,0xff00ff00);
    assert(renderer.LastStats().cube_cache_hits>=2);
    assert(renderer.LastStats().cube_face_copies==1);

    // Render-target texture sampling has guest top-left orientation. It must
    // not inherit the extra vertical flip used by the OpenGL backend.
    source_clear.color=0xff0000ff;
    auto source_corner=source_clear;source_corner.color=0xff00ff00;
    source_corner.rects={{0,0,size,8}};
    auto sampled=tex;sampled.textures[0].pixels.reset();
    sampled.textures[0].render_target=copy.target.color;
    sampled.textures[0].width=sampled.textures[0].height=size;
    for(auto& v:sampled.vertices)v.attributes[9]={.25f,.125f,0,1};
    pixels=Execute(renderer,{source_clear,source_corner,sampled});Pixel(pixels,16,16,0xff00ff00);
    for(auto& v:sampled.vertices)v.attributes[9][1]=.875f;
    pixels=Execute(renderer,{sampled});Pixel(pixels,16,16,0xff0000ff);

    // Vulkan depth/stencil copy packing differs from Xbox D24S8. The readback
    // contract returns the Xbox word (depth high24, stencil low8).
    auto depth_clear=Clear();depth_clear.z=.25f;depth_clear.stencil=7;
    pixels=Execute(renderer,{depth_clear},target,true);
    for(auto word:pixels)assert((word&255)==7 && std::abs(int(word>>8)-4194304)<=1);
    auto short_clear=Clear();short_clear.target={10,11,3,3,false,0x12,0x30};
    short_clear.rects={{0,0,3,3}};short_clear.z=.25f;
    pixels=Execute(renderer,{short_clear},short_clear.target,true);
    assert(pixels.size()==9);for(auto word:pixels)assert(std::abs(int(word)-16384)<=1);

    // Actual NV2A MOV instructions use SDK translation and the same VC/DC ABI.
    // This remains synthetic microcode, separate from commands emitted by PGR1.
    const std::vector<uint32_t> program={0,(1u<<21)|0x1b,2u<<26,(15u<<12)|(1u<<11),
        0,(1u<<21)|(3u<<9)|0x1b,2u<<26,(15u<<12)|(1u<<11)|(3u<<3)|1};
    auto nv_pair=std::make_shared<ShaderPair>();
    gpu::TexInfo info[4]{};
    nv_pair->spirv=CompileHlslPairSpirvAsync(gpu::VertexProgramHlsl(program,9),
                                           gpu::PixelHlsl(gpu::MakePixelKey(state,info)));
    auto nv_draw=Quad(state,nv_pair,{0,1,0,1});
    for(auto& v:nv_draw.vertices) v.attributes[0][2]=.5f*state.zscale;
    pixels=Execute(renderer,{Clear(),nv_draw});Pixel(pixels,16,16,0xff00ff00);

    // Invalid geometry must be rejected and counted, never submitted to Vulkan.
    auto invalid=red;invalid.indices={0,1,500};
    pixels=Execute(renderer,{Clear(),invalid});Pixel(pixels,16,16,0xff000000);
    assert(renderer.LastStats().accepted==0 && renderer.LastStats().rejected==1);
    auto point=red;point.topology=0;
    pixels=Execute(renderer,{Clear(),point});Pixel(pixels,16,16,0xff000000);
    assert(renderer.LastStats().accepted==0 && renderer.LastStats().rejected==1);

    // Guest fence/busy waits also complete when no readback or later frame is
    // available to drive the renderer. Completion is signalled after a GPU fence.
    Submission fenced;fenced.present=false;fenced.commands={Clear(),red};
    fenced.completion=std::make_shared<std::promise<void>>();
    auto completed=fenced.completion->get_future();
    assert(renderer.Render(fenced));
    assert(completed.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    completed.get();
    pixels=Execute(renderer,{});Pixel(pixels,16,16,0xffff0000);

    // An unsupported essential clear must resolve every waiting CPU promise
    // with an exception instead of corrupting channels or leaving Sync blocked.
    auto partial=Clear(0xff00ff00);partial.flags=0x20;
    Command failed_read;failed_read.kind=Command::Kind::Readback;failed_read.target=target;
    failed_read.readback=std::make_shared<std::promise<std::vector<uint32_t>>>();
    auto failed=failed_read.readback->get_future();
    Submission rejected;rejected.present=false;rejected.commands={partial,failed_read};
    assert(!renderer.Render(rejected));
    assert(failed.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    bool exception=false;
    try { (void)failed.get(); } catch(const std::exception&) { exception=true; }
    assert(exception);

    std::puts("Vulkan offscreen SDK integration passed: indexed/nonindexed draws, retained targets, "
              "depth/stencil, top-left scissor, culling, blend/alpha test, owned 2D/mip/cube textures, "
              "GPU reflection faces, render-target sampling, copy, D16/D24S8 readback and NV2A MOV. "
              "Synthetic commands, not PGR1 runtime evidence.");
}
