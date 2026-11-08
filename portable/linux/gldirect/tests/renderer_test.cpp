#include "../renderer.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <array>
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
    EGLint contextAttrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
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
        assert(r.Present(0,0));
    }
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context); eglDestroySurface(display,surface); eglTerminate(display);
    std::puts("OpenGL pixel tests passed: geometry, indexed strip, clear masks, persistence, depth, stencil, scissor, blend, texture invalidation, alpha test, targets.");
}
