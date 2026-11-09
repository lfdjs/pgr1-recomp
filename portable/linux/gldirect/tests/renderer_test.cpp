#include "../renderer.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <array>
#include <cstring>
#include <fstream>
#ifdef PGR1_TEST_SDK_SHADERS
#include "../shader_compiler.h"
#include "../draw_constants.h"
#include "gpu/shaders.h"
#include "app/app.h"
namespace app { const GameInfo kGame{}; Settings& Get() { static Settings s; return s; } }
#endif
using namespace pgr::gldirect;
namespace {
Submission Frame(std::vector<Command> commands,unsigned front=1) {
    Submission f; f.front=front; f.commands=std::move(commands); return f;
}
Command Clear(unsigned color=0xff000000) {
    Command c; c.target={1,2,32,32}; c.flags=0xf3; c.color=color; return c;
}
Command Quad(std::array<float,4> color,float z=.5f) {
    Command c; c.kind=Command::Kind::Draw; c.target={1,2,32,32};
    c.pipeline.viewport={0,0,32,32};
    // D3D pixel centres lie at integer coordinates.
    c.vertices={{{-.5,-.5,z,1},color,{0,0,0,1}},{{31.5,-.5,z,1},color,{1,0,0,1}},
                {{31.5,31.5,z,1},color,{1,1,0,1}},{{-.5,31.5,z,1},color,{0,1,0,1}}};
    c.indices={0,1,2,0,2,3}; return c;
}
}
int main() {
    auto getDisplay=reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if(!getDisplay) return 77;
    EGLDisplay display=getDisplay(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);
    if(!eglInitialize(display,nullptr,nullptr)) return 77;
    assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config; EGLint n; assert(eglChooseConfig(display,attrs,&config,1,&n) && n);
    EGLint surfaceAttrs[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttrs); assert(surface!=EGL_NO_SURFACE);
    EGLint contextAttrs[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,0,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs); assert(context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display,surface,surface,context));
    auto info=reinterpret_cast<PFNGLGETSTRINGPROC>(eglGetProcAddress("glGetString"));
    std::printf("GL vendor=%s renderer=%s version=%s\n",info(GL_VENDOR),info(GL_RENDERER),info(GL_VERSION));
    auto read=reinterpret_cast<PFNGLREADPIXELSPROC>(eglGetProcAddress("glReadPixels"));
    auto bind=reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(eglGetProcAddress("glBindFramebuffer"));
    auto pixel=[&](int x,int y,std::array<int,4> expected) {
        bind(GL_READ_FRAMEBUFFER,0);
        unsigned char rgba[4]{}; read(x,31-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        for(unsigned i=0;i<4;i++) if(std::abs(int(rgba[i])-expected[i])>1) {
            std::fprintf(stderr,"pixel %d,%d channel %u: got %u expected %d\n",x,y,i,rgba[i],expected[i]); std::abort();
        }
    };
    {
        Renderer r([](const char* name) { return reinterpret_cast<Renderer::Proc>(eglGetProcAddress(name)); }); assert(r.Init());
        auto red=Quad({1,0,0,1}); assert(r.Execute(Frame({Clear(),red}))); assert(r.Present(32,32)); pixel(16,16,{255,0,0,255});
        // Incremental frames retain the previous color attachment; a masked clear must preserve red.
        auto clear=Clear(0xff00ff00); clear.flags=0x20; clear.rects={{0,0,8,8}};
        assert(r.Execute(Frame({clear}))); assert(r.Present(32,32)); pixel(2,2,{255,255,0,255}); pixel(16,16,{255,0,0,255});
        // Depth rejects the far blue quad; scissor confines the green overlay.
        red.pipeline.depth=true; red.pipeline.depth_write=true; red.pipeline.depth_func=GL_LESS;
        auto blue=Quad({0,0,1,1},.8); blue.pipeline=red.pipeline;
        auto green=Quad({0,1,0,1},.2); green.pipeline=red.pipeline; green.pipeline.scissors={{0,0,8,8}};
        assert(r.Execute(Frame({Clear(),red,blue,green}))); assert(r.Present(32,32)); pixel(16,16,{255,0,0,255}); pixel(2,2,{0,255,0,255});
        // Stencil is written with color disabled, then consumed by a second draw.
        auto mask=Quad({0,0,0,1}); mask.pipeline.color_mask=0; mask.pipeline.stencil=true;
        mask.pipeline.stencil_ref=3; mask.pipeline.stencil_pass=GL_REPLACE; mask.pipeline.scissors={{0,0,8,8}};
        blue.pipeline=Pipeline{}; blue.pipeline.viewport={0,0,32,32}; blue.pipeline.stencil=true;
        blue.pipeline.stencil_ref=3; blue.pipeline.stencil_func=GL_EQUAL;
        assert(r.Execute(Frame({Clear(),mask,blue}))); assert(r.Present(32,32)); pixel(2,2,{0,0,255,255}); pixel(16,16,{0,0,0,255});
        // Standard source-alpha blend: CPU reference equation .5*blue + .5*red.
        blue=Quad({0,0,1,.5}); blue.pipeline.blend=true; blue.pipeline.src_blend=GL_SRC_ALPHA; blue.pipeline.dst_blend=GL_ONE_MINUS_SRC_ALPHA;
        assert(r.Execute(Frame({Clear(0xffff0000),blue}))); assert(r.Present(32,32)); pixel(16,16,{128,0,128,191});
        auto tex=Quad({1,1,1,1}); tex.texture={1,1,{0xff00ff00}}; tex.pipeline.color_op=2; tex.pipeline.alpha_op=2;
        assert(r.Execute(Frame({Clear(),tex}))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
        // Same dimensions, different bytes must invalidate the texture upload cache.
        tex.texture.bgra[0]=0xff0000ff;
        assert(r.Execute(Frame({tex}))); assert(r.Present(32,32)); pixel(16,16,{0,0,255,255});
        auto sampled=Quad({1,1,1,1}); sampled.texture={2,1,{0xff00ff00,0xff0000ff}};
        sampled.pipeline.color_op=2; sampled.pipeline.alpha_op=2;
        for(auto& v:sampled.vertices) v.uv={.5f,.5f,0,1};
        sampled.texture.mag_filter=2;
        assert(r.Execute(Frame({sampled}))); assert(r.Present(32,32)); pixel(16,16,{0,128,128,255});
        sampled.texture.mag_filter=1; sampled.texture.wrap_u=1;
        for(auto& v:sampled.vertices) v.uv[0]=1.25f;
        assert(r.Execute(Frame({sampled}))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
        sampled.texture.wrap_u=3;
        assert(r.Execute(Frame({sampled}))); assert(r.Present(32,32)); pixel(16,16,{0,0,255,255});
        tex.pipeline.alpha_test=true; tex.pipeline.alpha_func=GL_GREATER; tex.pipeline.alpha_ref=128; tex.texture.bgra[0]=0x0000ff00;
        assert(r.Execute(Frame({Clear(0xffff0000),tex}))); assert(r.Present(32,32)); pixel(16,16,{255,0,0,255});
        // Independent target identity and resize do not overwrite the old target.
        clear=Clear(0xff00ff00); clear.target={3,4,16,16};
        assert(r.Execute(Frame({clear},3))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
        assert(r.Execute(Frame({},1))); assert(r.Present(32,32)); pixel(16,16,{255,0,0,255});
        // Xbox primitive assembly emits points, lines, triangle lists and strips.
        auto strip=Quad({1,1,0,1}); strip.topology=GL_TRIANGLE_STRIP; strip.indices={0,1,3,2};
        assert(r.Execute(Frame({Clear(),strip}))); assert(r.Present(32,32)); pixel(16,16,{255,255,0,255});
        auto culled=Quad({1,0,0,1}); culled.pipeline.cull=GL_CW;
        assert(r.Execute(Frame({Clear(),culled}))); assert(r.Present(32,32)); pixel(16,16,{0,0,0,255});
        culled.pipeline.cull=GL_CCW;
        assert(r.Execute(Frame({culled}))); assert(r.Present(32,32)); pixel(16,16,{255,0,0,255});
        auto opaque=Clear(0x0000ff00); opaque.target={5,0,32,32,true};
        assert(r.Execute(Frame({opaque},5))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
        auto depth_clear=Clear(); depth_clear.z=.25f; depth_clear.stencil=7;
        Command depth_read; depth_read.kind=Command::Kind::Readback; depth_read.target=depth_clear.target; depth_read.read_depth=true;
        depth_read.readback=std::make_shared<std::promise<std::vector<uint32_t>>>(); auto depth_future=depth_read.readback->get_future();
        assert(r.Execute(Frame({depth_clear,depth_read})));
        const auto packed=depth_future.get()[0]; assert((packed&255)==7 && std::abs(int(packed>>8)-4194304)<=1);
        auto odd_clear=Clear(); odd_clear.target={10,11,3,3,false,0x12,0x30}; odd_clear.z=.25f;
        Command odd_read; odd_read.kind=Command::Kind::Readback; odd_read.target=odd_clear.target; odd_read.read_depth=true;
        odd_read.readback=std::make_shared<std::promise<std::vector<uint32_t>>>(); auto odd_future=odd_read.readback->get_future();
        assert(r.Execute(Frame({odd_clear,odd_read},10))); const auto odd_depth=odd_future.get();
        assert(odd_depth.size()==9); for(auto z:odd_depth) assert(std::abs(int(z)-16384)<=1);
#ifdef PGR1_TEST_SDK_SHADERS
        gpu::State state;
        for(auto& stage:state.tss) stage[gpu::kTssColorOp]=1;
        state.viewport={0,0,32,32,0,1};
        auto generated=Quad({0,1,0,1});
        for(auto& v:generated.vertices) { v.attributes[0]=v.position; v.attributes[3]=v.color; }
        auto pair=std::make_shared<ShaderPair>();
        pair->vertex=CompileHlsl(gpu::FixedVertexHlsl(9,true),true);
        gpu::TexInfo info[4]{};
        pair->fragment=CompileHlsl(gpu::PixelHlsl(gpu::MakePixelKey(state,info)),false);
        generated.shaders=pair;
        auto dc=Constants(state,32,32,false);
        generated.draw_constants.resize(sizeof(dc)); std::memcpy(generated.draw_constants.data(),&dc,sizeof(dc));
        assert(r.Execute(Frame({Clear(),generated}))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
        generated.pipeline.viewport={4,4,8,8};
        assert(r.Execute(Frame({Clear(),generated}))); assert(r.Present(32,32));
        pixel(2,2,{0,0,0,255}); pixel(6,6,{0,255,0,255});
        generated.pipeline.viewport={0,0,32,32};
        assert(r.Execute(Frame({Clear(),generated}))); assert(r.Present(32,32));
        // GPU CopyRects and color/depth readback execute in the same ordered stream.
        Command copy; copy.kind=Command::Kind::Copy; copy.source=1; copy.source_height=32;
        copy.target={8,0,32,32}; copy.rects={{0,0,32,32}}; copy.destination_rects={{0,0,32,32}};
        Command readback; readback.kind=Command::Kind::Readback; readback.target=copy.target;
        readback.readback=std::make_shared<std::promise<std::vector<uint32_t>>>(); auto future=readback.readback->get_future();
        assert(r.Execute(Frame({copy,readback},8))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
        assert(future.get()[16*32+16]==0xff00ff00);
        // A partial batch preserves the last present identity.
        auto partial_clear=Clear(0xffff0000); partial_clear.target={8,0,32,32};
        auto partial=Frame({partial_clear},8); partial.present=false;
        assert(r.Execute(partial)); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
        assert(r.Execute(Frame({copy},8)));
        // Cube face 0 updated from a 2D GPU surface must remain a cube GL object.
        auto cube_pixels=std::make_shared<TexturePixels>(); cube_pixels->levels.resize(6,std::vector<uint32_t>{0xff0000ff});
        auto cube_draw=generated; cube_draw.target={9,0,32,32};
        cube_draw.textures[0].cube=true; cube_draw.textures[0].width=cube_draw.textures[0].height=1;
        cube_draw.textures[0].pixels=cube_pixels; cube_draw.textures[0].face_targets[0]=8;
        gpu::TexInfo cube_info[4]{}; cube_info[0].kind=gpu::kTexCube;
        state.tss[0][gpu::kTssColorOp]=state.tss[0][gpu::kTssAlphaOp]=2;
        state.tss[0][gpu::kTssColorArg1]=state.tss[0][gpu::kTssAlphaArg1]=2;
        auto cube_pair=std::make_shared<ShaderPair>(); cube_pair->vertex=CompileHlsl(gpu::FixedVertexHlsl(0x209,true),true);
        cube_pair->fragment=CompileHlsl(gpu::PixelHlsl(gpu::MakePixelKey(state,cube_info)),false);
        cube_draw.shaders=cube_pair;
        for(auto& v:cube_draw.vertices) v.attributes[9]={1,0,0,1};
        assert(r.Execute(Frame({cube_draw},9))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
        // Real NV2A MOV instructions, decoded by SDK, executed by the GL vertex stage.
        std::vector<uint32_t> program={0,(1u<<21)|0x1b,2u<<26,(15u<<12)|(1u<<11),
                                      0,(1u<<21)|(3u<<9)|0x1b,2u<<26,(15u<<12)|(1u<<11)|(3u<<3)|1};
        auto nv=std::make_shared<ShaderPair>(); nv->vertex=CompileHlsl(gpu::VertexProgramHlsl(program,9),true); nv->fragment=pair->fragment;
        generated.shaders=nv;
        for(auto& v:generated.vertices) v.attributes[0][2]=.5f*state.zscale;
        assert(r.Execute(Frame({Clear(),generated}))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
        // A real NV2A MAD lowers to NoContraction in SPIR-V. GLSL 3.30 used to
        // discard it, allowing different rounding between EQUAL-depth passes.
        // Keep precise through lowering and compile/link/execute it on real GL.
        program={0,(4u<<21)|0x1b,(2u<<26)|(0x1bu<<17)|(3u<<11)|(0x1bu<<2),
                 (3u<<28)|(15u<<12)|(1u<<11),
                 0,(1u<<21)|(3u<<9)|0x1b,2u<<26,(15u<<12)|(1u<<11)|(3u<<3)|1};
        auto precise=std::make_shared<ShaderPair>();
        precise->vertex=CompileHlsl(gpu::VertexProgramHlsl(program,9),true);
        precise->fragment=pair->fragment;
        assert(precise->vertex.find("#version 400")!=std::string::npos);
        assert(precise->vertex.find("precise ")!=std::string::npos);
        generated.shaders=precise; generated.vertex_constants[0]={1,1,1,1};
        for(auto& v:generated.vertices) { v.attributes[0][0]-=1; v.attributes[0][1]-=1; }
        assert(r.Execute(Frame({Clear(),generated}))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
#endif
#ifdef PGR1_TEST_SDK_SHADERS
        // Exercise cache eviction with four sampled stages. Evicting after stage 0
        // was bound made that stage black in the same draw on the real renderer.
        auto cache_pair=std::make_shared<ShaderPair>(); cache_pair->vertex=pair->vertex;
        cache_pair->fragment=R"(#version 330
uniform sampler2D SPIRV_Cross_Combinedtex0smp0,SPIRV_Cross_Combinedtex1smp1,SPIRV_Cross_Combinedtex2smp2,SPIRV_Cross_Combinedtex3smp3;
out vec4 color;
void main() { color=(texture(SPIRV_Cross_Combinedtex0smp0,vec2(.5))+texture(SPIRV_Cross_Combinedtex1smp1,vec2(.5))+texture(SPIRV_Cross_Combinedtex2smp2,vec2(.5))+texture(SPIRV_Cross_Combinedtex3smp3,vec2(.5)))*.25; }
)";
        auto cache_draw=generated; cache_draw.shaders=cache_pair;
        for(auto& v:cache_draw.vertices) v.attributes[0]=v.position;
        for(unsigned iteration=0;iteration<70;iteration++) {
            for(auto& t:cache_draw.textures) { t.width=t.height=1; auto pixels=std::make_shared<TexturePixels>(); pixels->levels={{0xff00ff00}}; t.pixels=pixels; }
            assert(r.Execute(Frame({cache_draw}))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
            r.PollGpu();
        }
        // Two stages may share an image while using different samplers. Texture
        // object parameters would make the last stage silently override both.
        auto sampler_pair=std::make_shared<ShaderPair>(); sampler_pair->vertex=pair->vertex;
        sampler_pair->fragment=R"(#version 400
uniform sampler2D SPIRV_Cross_Combinedtex0smp0, SPIRV_Cross_Combinedtex1smp1;
out vec4 color;
void main() { color=vec4(texture(SPIRV_Cross_Combinedtex0smp0,vec2(1.25,.5)).r,texture(SPIRV_Cross_Combinedtex1smp1,vec2(1.25,.5)).g,0,1); }
)";
        auto shared_pixels=std::make_shared<TexturePixels>(); shared_pixels->levels={{0xffff0000,0xff00ff00}};
        auto sampler_draw=cache_draw; sampler_draw.shaders=sampler_pair; sampler_draw.textures={};
        for(unsigned i=0;i<2;i++) { auto& t=sampler_draw.textures[i];t.width=2;t.height=1;t.pixels=shared_pixels;t.wrap_u=i?3:1; }
        assert(r.Execute(Frame({sampler_draw}))); assert(r.Present(32,32)); pixel(16,16,{255,255,0,255});
        sampler_pair=std::make_shared<ShaderPair>(); sampler_pair->vertex=pair->vertex;
        sampler_pair->fragment=R"(#version 400
uniform sampler2D SPIRV_Cross_Combinedtex0smp0;out vec4 color;
void main() { color=texture(SPIRV_Cross_Combinedtex0smp0,vec2(2,.5)); }
)";
        sampler_draw.shaders=sampler_pair; sampler_draw.textures[0].wrap_u=4; sampler_draw.textures[0].border_color=0xff0000ff;
        assert(r.Execute(Frame({sampler_draw}))); assert(r.Present(32,32)); pixel(16,16,{0,0,255,255});
        auto lod_pixels=std::make_shared<TexturePixels>();
        lod_pixels->levels={std::vector<uint32_t>(16,0xffff0000),std::vector<uint32_t>(4,0xff00ff00),{0xff0000ff}};
        auto& lod=sampler_draw.textures[0];lod.width=lod.height=4;lod.pixels=lod_pixels;lod.wrap_u=3;lod.mip_filter=1;lod.max_mip=2;
        assert(r.Execute(Frame({sampler_draw}))); assert(r.Present(32,32)); pixel(16,16,{0,0,255,255});
        auto face_pair=std::make_shared<ShaderPair>(); face_pair->vertex=pair->vertex;
        face_pair->fragment="#version 330\nout vec4 color; void main() { color=gl_FrontFacing?vec4(0,1,0,1):vec4(1,0,0,1); }";
        cache_draw.shaders=face_pair; cache_draw.pipeline.front=GL_CW;
        assert(r.Execute(Frame({cache_draw}))); assert(r.Present(32,32)); pixel(16,16,{0,255,0,255});
        cache_draw.pipeline.front=GL_CCW;
        assert(r.Execute(Frame({cache_draw}))); assert(r.Present(32,32)); pixel(16,16,{255,0,0,255});
#endif
        // The screen target has actual output pixels; readbacks retain guest
        // dimensions. Resizing preserves color AND depth rather than leaving
        // an undefined frame or allowing far geometry through retained depth.
        r.SetAspect(0);
        r.SetOutputSize(64,64,32,32);
        auto near=Quad({1,0,0,1},.25f); near.pipeline.depth=true;near.pipeline.depth_write=true;near.pipeline.depth_func=GL_LESS;
        assert(r.Execute(Frame({Clear(),near}))); assert(r.Present(32,32));pixel(16,16,{255,0,0,255});
        assert(r.Capture("resolution-test.ppm"));
        {
            std::ifstream capture("resolution-test.ppm",std::ios::binary);std::string magic;unsigned w=0,h=0;
            capture>>magic>>w>>h;assert(magic=="P6" && w==64 && h==64);
        }
        r.SetOutputSize(96,64,32,32);
        auto far=Quad({0,0,1,1},.8f);far.pipeline=near.pipeline;
        assert(r.Execute(Frame({far})));assert(r.Present(32,32));pixel(16,16,{255,0,0,255});
        auto corner=Quad({0,1,0,1},.1f);corner.pipeline=near.pipeline;corner.pipeline.scissors={{0,0,8,8}};
        assert(r.Execute(Frame({corner})));assert(r.Present(32,32));pixel(2,2,{0,255,0,255});pixel(16,16,{255,0,0,255});
        Command scaled_read;scaled_read.kind=Command::Kind::Readback;scaled_read.target={1,2,32,32};
        scaled_read.readback=std::make_shared<std::promise<std::vector<uint32_t>>>();auto scaled_future=scaled_read.readback->get_future();
        assert(r.Execute(Frame({scaled_read})));const auto scaled_pixels=scaled_future.get();
        assert(scaled_pixels.size()==32*32 && scaled_pixels[16*32+16]==0xffff0000 && scaled_pixels[29*32+2]==0xff00ff00);
        Submission scaled_comparison;scaled_comparison.frame=999;scaled_comparison.reference_width=scaled_comparison.reference_height=32;
        scaled_comparison.reference_pixels.resize(32*32,0xffff0000);
        for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++) scaled_comparison.reference_pixels[y*32+x]=0xff00ff00;
        r.Compare(scaled_comparison);
        {
            std::ifstream report("compare.txt");std::string line;std::getline(report,line);assert(line.find("mean_RGB_error=0 ")!=std::string::npos);
            std::ifstream capture("compare-gpu.ppm",std::ios::binary);std::string magic;unsigned w=0,h=0;
            capture>>magic>>w>>h;assert(w==32 && h==32);
        }
        std::remove("compare.txt");std::remove("compare-gpu.ppm");std::remove("compare-reference.ppm");
        auto scaled_depth=scaled_read;scaled_depth.read_depth=true;
        scaled_depth.readback=std::make_shared<std::promise<std::vector<uint32_t>>>();auto scaled_z=scaled_depth.readback->get_future();
        assert(r.Execute(Frame({scaled_depth})));assert(std::abs(int(scaled_z.get()[16*32+16]>>8)-4194304)<=1);
        // The guest scissor follows the same HUD squeeze and pivot as the shader.
        auto squeeze=Quad({0,0,1,1});squeeze.pipeline.clip_scale=.5f;squeeze.pipeline.clip_pivot=16;
        squeeze.pipeline.scissors={{0,0,8,32}};
        assert(r.Execute(Frame({Clear(),squeeze})));assert(r.Present(32,32));
        pixel(2,16,{0,0,0,255});pixel(9,16,{0,0,255,255});pixel(14,16,{0,0,0,255});
        // Half-sized sampled surfaces/cube faces remain native, not screen-scaled.
        auto small=Clear(0xff00ff00);small.target={19,0,16,16};
        assert(r.Execute(Frame({small},19)));assert(r.Capture("resolution-test.ppm"));
        {
            std::ifstream capture("resolution-test.ppm",std::ios::binary);std::string magic;unsigned w=0,h=0;
            capture>>magic>>w>>h;assert(w==16 && h==16);
        }
        std::remove("resolution-test.ppm");
        assert(r.Present(0,0));
    }
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context); eglDestroySurface(display,surface); eglTerminate(display);
    std::puts("OpenGL pixel tests passed: geometry, indexed strip, clear masks, persistence, depth, stencil, scissor, blend, texture invalidation, alpha test, targets.");
}
