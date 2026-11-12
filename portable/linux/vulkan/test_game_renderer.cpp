// Synthetic SDK shader/command integration tests. This is not game evidence.
#include "game_renderer.h"
#include "quality_state.h"
#include "../gldirect/commands.h"
#include "../gldirect/draw_constants.h"
#include "../gldirect/shader_compiler.h"
#include "gpu/shaders.h"
#include "app/app.h"
#include <array>
#include <algorithm>
#include <vulkan/vulkan.h>
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

    // 04D.2: after one submission is retired, mapped stage memory, descriptors
    // and live-target framebuffer must be reused without changing pixels.
    auto stable_draw=red;
    pixels=Execute(renderer,{Clear(),stable_draw});Pixel(pixels,16,16,0xffff0000);
    pixels=Execute(renderer,{Clear(),stable_draw});Pixel(pixels,16,16,0xffff0000);
    assert(renderer.LastStats().staging_reuses>=1);
    assert(renderer.LastStats().staging_allocations==0);
    assert(renderer.LastStats().descriptor_resets>=1);
    assert(renderer.LastStats().descriptor_creates==0);
    assert(renderer.LastStats().framebuffer_hits>=1);
    assert(renderer.LastStats().framebuffer_creates==0);

    // MSAA must alter actual diagonal coverage in the primary display target;
    // small reflection/auxiliary targets above remain single-sampled.
    const Target display{0x10000,0x20000,640,480};
    auto display_state=State();display_state.viewport={0,0,640,480,0,1};
    auto display_shaders=Shaders(display_state);
    auto display_draw=Quad(display_state,display_shaders,{1,1,1,1});
    display_draw.target=display;display_draw.pipeline.viewport={0,0,640,480};
    display_draw.pipeline.depth=true;display_draw.pipeline.depth_write=true;
    display_draw.pipeline.depth_func=0x201;
    for(auto& v:display_draw.vertices) {
        v.attributes[0][0]=v.position[0]=(v.position[0]+.5f)*20-.5f;
        v.attributes[0][1]=v.position[1]=(v.position[1]+.5f)*15-.5f;
    }
    const auto display_constants=Constants(display_state,640,480,false);
    std::memcpy(display_draw.draw_constants.data(),&display_constants,sizeof(display_constants));
    display_draw.indices={0,1,3};
    auto display_clear=Clear();display_clear.target=display;
    display_clear.rects={{0,0,640,480}};display_clear.stencil=7;
    const auto fractional=[](const auto& image) {
        return std::count_if(image.begin(),image.end(),[](uint32_t p){return (p&255)>0 && (p&255)<255;});
    };
    std::vector<uint32_t> display_off;
    for(unsigned option=0;option<=2;option++) {
        if(!(pgr::vulkan::quality::msaa_supported_mask.load()&(1u<<option)))continue;
        pgr::vulkan::quality::msaa.store(option);
        pixels=Execute(renderer,{display_clear,display_draw},display);
        assert(pgr::vulkan::quality::msaa_applied.load()==option);
        const auto count=fractional(pixels);
        std::printf("Real MSAA %ux coverage: %zu fractional pixels\n",1u<<option,size_t(count));
        if(!option) { assert(count==0);display_off=pixels; }
        else { assert(count>100);assert(pixels!=display_off); }
        auto visible_display=display_draw;visible_display.indices={0,1,2,0,2,3};
        visible_display.visibility=std::make_shared<std::promise<uint64_t>>();
        auto display_count=visible_display.visibility->get_future();
        (void)Execute(renderer,{display_clear,visible_display},display);
        assert(display_count.get()==640u*480u); // MSAA must not multiply guest visibility
        pixels=Execute(renderer,{display_clear,display_draw},display);
        // Resolved depth/stencil remains available in the Xbox readback ABI.
        const auto z=Execute(renderer,{},display,true);
        assert((z[20*640+20]&255)==7);
        assert(std::abs(int(z[20*640+20]>>8)-8388608)<=1);
        // Retained multipass geometry must preserve depth, even without a clear.
        auto covered=display_draw;
        for(auto& v:covered.vertices) { v.attributes[0][2]=v.position[2]=.8f;v.attributes[3]={1,0,0,1}; }
        const auto retained=Execute(renderer,{covered},display);
        assert(retained[20*640+20]==pixels[20*640+20]);
    }
    // The actual PGR1 scene clear also uses flags0x03: depth+stencil only.
    // MSAA can change here by seeding retained color before the new depth clear.
    if(pgr::vulkan::quality::msaa_supported_mask.load()&2u) {
        pgr::vulkan::quality::msaa.store(0);
        auto preserved=Execute(renderer,{display_clear,display_draw},display);
        pgr::vulkan::quality::msaa.store(1);
        auto depth_only=display_clear;depth_only.flags=3;
        pixels=Execute(renderer,{depth_only},display);
        assert(pixels==preserved && pgr::vulkan::quality::msaa_applied.load()==1);
        const auto z=Execute(renderer,{},display,true);
        assert((z[20*640+20]&255)==7 && (z[20*640+20]>>8)==0xffffff);
    }
    // A new surface can already be multisampled when the title clears only
    // color+depth (retained stencil need not be cleared). Report its actual
    // presented samples rather than falsely displaying Off until an f3 clear.
    if(pgr::vulkan::quality::msaa_supported_mask.load()&2u) {
        auto initial_clear=display_clear;initial_clear.target.color+=0x100;initial_clear.target.depth+=0x100;
        initial_clear.flags=0xf1;
        auto initial_draw=display_draw;initial_draw.target=initial_clear.target;
        pgr::vulkan::quality::msaa.store(1);pgr::vulkan::quality::msaa_applied.store(0);
        (void)Execute(renderer,{initial_clear,initial_draw},initial_clear.target);
        Submission first_display;first_display.present=true;first_display.front=initial_clear.target.color;
        assert(renderer.Render(first_display));
        assert(pgr::vulkan::quality::msaa_applied.load()==1);
    }
    // A full title clear safely changes samples back to off at run time.
    pgr::vulkan::quality::msaa.store(0);
    pixels=Execute(renderer,{display_clear,display_draw},display);
    assert(pixels==display_off && pgr::vulkan::quality::msaa_applied.load()==0);

    // FXAA and sharpening use the same GPU pass as WSI presentation. ReadDisplay
    // runs it without a swapchain so the exact source frame is comparable.
    Submission display_submission;display_submission.present=true;display_submission.front=display.color;
    assert(renderer.Render(display_submission));
    assert(!renderer.LastStats().presented); // offscreen work is never FPS
    pgr::vulkan::quality::fxaa.store(false);pgr::vulkan::quality::show_fps.store(false);
    auto unfiltered=renderer.ReadDisplay();
    assert(unfiltered.width==640 && unfiltered.height==480);
    pgr::vulkan::quality::fxaa.store(true);
    auto filtered=renderer.ReadDisplay();
    assert(filtered.pixels!=unfiltered.pixels && fractional(filtered.pixels)>fractional(unfiltered.pixels));
    pgr::vulkan::quality::sharpen_percent.store(100);
    auto sharpened=renderer.ReadDisplay();assert(sharpened.pixels!=filtered.pixels);
    pgr::vulkan::quality::sharpen_percent.store(0);
    pgr::vulkan::quality::fxaa.store(false);
    assert(renderer.ReadDisplay().pixels==unfiltered.pixels); // live toggle reverses
    pgr::vulkan::quality::show_fps.store(true);
    assert(renderer.ReadDisplay().pixels!=unfiltered.pixels); // actual overlay pixels
    pgr::vulkan::quality::show_fps.store(false);
    assert(renderer.ReadDisplay().pixels==unfiltered.pixels);

    // CopyRects updates resolved color. Before a later multisampled draw it is
    // seeded on the GPU, preserving all untouched pixels, not stale MSAA texels.
    const auto support=pgr::vulkan::quality::msaa_supported_mask.load();
    if(support&(VK_SAMPLE_COUNT_2_BIT|VK_SAMPLE_COUNT_4_BIT)) {
        const unsigned option=support&VK_SAMPLE_COUNT_2_BIT?1u:2u;
        pgr::vulkan::quality::msaa.store(option);
        pixels=Execute(renderer,{display_clear,display_draw},display);
        auto src=Clear(0xff0000ff);
        Command transfer;transfer.kind=Command::Kind::Copy;transfer.source=target.color;transfer.target=display;
        transfer.rects={{0,0,32,32}};transfer.destination_rects={{0,0,640,480}};
        auto corner=display_draw;corner.pipeline.scissors={{0,0,8,8}};
        corner.pipeline.depth=false;corner.pipeline.depth_write=false;
        pixels=Execute(renderer,{src,transfer,corner},display);
        assert(pixels[100*640+100]==0xff0000ff && pixels[2*640+2]==0xffffffff);
        pgr::vulkan::quality::alpha_coverage.store(true);
        auto alpha=display_draw;alpha.pipeline.depth=false;alpha.pipeline.depth_write=false;
        for(auto& v:alpha.vertices)v.attributes[3][3]=.5f;
        pixels=Execute(renderer,{display_clear,alpha},display);
        assert((pixels[20*640+20]&255)>0 && (pixels[20*640+20]&255)<255);
        pgr::vulkan::quality::alpha_coverage.store(false);
        pgr::vulkan::quality::msaa.store(0);
        pixels=Execute(renderer,{display_clear},display);
    }

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
