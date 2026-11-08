#include "renderer.h"
#include <GL/glcorearb.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <map>
#include <utility>

namespace pgr::gldirect {
#define GL_FUNCTIONS(X) \
 X(PFNGLGETERRORPROC, GetError) X(PFNGLGETINTEGERVPROC, GetIntegerv) \
 X(PFNGLCREATESHADERPROC, CreateShader) X(PFNGLSHADERSOURCEPROC, ShaderSource) \
 X(PFNGLCOMPILESHADERPROC, CompileShader) X(PFNGLGETSHADERIVPROC, GetShaderiv) \
 X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) X(PFNGLDELETESHADERPROC, DeleteShader) \
 X(PFNGLCREATEPROGRAMPROC, CreateProgram) X(PFNGLLINKPROGRAMPROC, LinkProgram) \
 X(PFNGLATTACHSHADERPROC, AttachShader) X(PFNGLGETPROGRAMIVPROC, GetProgramiv) \
 X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) X(PFNGLDELETEPROGRAMPROC, DeleteProgram) \
 X(PFNGLUSEPROGRAMPROC, UseProgram) X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
 X(PFNGLUNIFORM4FPROC, Uniform4f) X(PFNGLUNIFORM1IPROC, Uniform1i) \
 X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) \
 X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) X(PFNGLGENBUFFERSPROC, GenBuffers) \
 X(PFNGLBINDBUFFERPROC, BindBuffer) X(PFNGLBUFFERDATAPROC, BufferData) \
 X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
 X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) X(PFNGLDRAWELEMENTSPROC, DrawElements) \
 X(PFNGLDRAWARRAYSPROC, DrawArrays) X(PFNGLGENTEXTURESPROC, GenTextures) \
 X(PFNGLBINDTEXTUREPROC, BindTexture) X(PFNGLTEXIMAGE2DPROC, TexImage2D) \
 X(PFNGLTEXPARAMETERIPROC, TexParameteri) X(PFNGLDELETETEXTURESPROC, DeleteTextures) \
 X(PFNGLACTIVETEXTUREPROC, ActiveTexture) X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) \
 X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
 X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers) \
 X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers) X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer) \
 X(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage) X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer) \
 X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers) X(PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer) \
 X(PFNGLVIEWPORTPROC, Viewport) X(PFNGLSCISSORPROC, Scissor) X(PFNGLENABLEPROC, Enable) \
 X(PFNGLDISABLEPROC, Disable) X(PFNGLCOLORMASKPROC, ColorMask) X(PFNGLDEPTHMASKPROC, DepthMask) \
 X(PFNGLDEPTHFUNCPROC, DepthFunc) X(PFNGLDEPTHRANGEPROC, DepthRange) X(PFNGLSTENCILMASKPROC, StencilMask) \
 X(PFNGLSTENCILFUNCPROC, StencilFunc) X(PFNGLSTENCILOPPROC, StencilOp) \
 X(PFNGLBLENDFUNCPROC, BlendFunc) X(PFNGLBLENDEQUATIONPROC, BlendEquation) \
 X(PFNGLBLENDCOLORPROC, BlendColor) X(PFNGLCULLFACEPROC, CullFace) X(PFNGLFRONTFACEPROC, FrontFace) \
 X(PFNGLCLEARCOLORPROC, ClearColor) X(PFNGLCLEARDEPTHPROC, ClearDepth) \
 X(PFNGLCLEARSTENCILPROC, ClearStencil) X(PFNGLCLEARPROC, Clear)
struct Renderer::Impl {
    Loader load;
#define DECL(type,name) type name=nullptr;
    GL_FUNCTIONS(DECL)
#undef DECL
    GLuint program=0, vao=0, vbo=0, ibo=0, texture=0;
    GLint limit=0;
    struct Image { GLuint object=0; unsigned w=0,h=0; bool opaque=false; };
    std::map<unsigned,Image> colors, depths;
    std::map<std::pair<unsigned,unsigned>,GLuint> fbos;
    unsigned front=0;
    bool valid=false, uploaded=false;
    Texture cached;
    explicit Impl(Loader l):load(l) {}
    bool Error(const char* where) {
        bool ok=true;
        for (GLenum e; (e=GetError())!=GL_NO_ERROR;) { std::fprintf(stderr,"gldirect: GL error 0x%x at %s\n",e,where); ok=false; }
        return ok;
    }
    GLuint Shader(GLenum type,const char* source) {
        GLuint shader=CreateShader(type); ShaderSource(shader,1,&source,nullptr); CompileShader(shader);
        GLint ok=0; GetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        if (!ok) { char log[4096]{}; GetShaderInfoLog(shader,sizeof(log),nullptr,log); std::fprintf(stderr,"gldirect: shader: %s\n",log); DeleteShader(shader); return 0; }
        return shader;
    }
    bool Bind(const Target& t) {
        if (!t.width || !t.height || t.width>unsigned(limit) || t.height>unsigned(limit)) return false;
        auto& c=colors[t.color];
        if (!c.object) GenTextures(1,&c.object);
        if (c.w!=t.width || c.h!=t.height || c.opaque!=t.opaque) {
            BindTexture(GL_TEXTURE_2D,c.object); TexImage2D(GL_TEXTURE_2D,0,t.opaque?GL_RGB8:GL_RGBA8,t.width,t.height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            c.w=t.width; c.h=t.height; c.opaque=t.opaque;
        }
        auto& f=fbos[{t.color,t.depth}]; if (!f) GenFramebuffers(1,&f);
        BindFramebuffer(GL_FRAMEBUFFER,f); FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,c.object,0);
        if (t.depth) {
            auto& d=depths[t.depth]; if (!d.object) GenRenderbuffers(1,&d.object);
            BindRenderbuffer(GL_RENDERBUFFER,d.object);
            if (d.w!=t.width || d.h!=t.height) { RenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,t.width,t.height); d.w=t.width; d.h=t.height; }
            FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,d.object);
        }
        if (CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) { std::fputs("gldirect: incomplete target\n",stderr); return false; }
        return true;
    }
    void Toggle(GLenum cap,bool on) { if(on) Enable(cap); else Disable(cap); }
    void Clip(Rect r,unsigned h) { Scissor(r.x,int(h)-r.y-r.h,std::max(0,r.w),std::max(0,r.h)); }
    static float Channel(unsigned c,unsigned shift) { return float((c>>shift)&255)/255; }
};
Renderer::Renderer(Loader loader):impl(std::make_unique<Impl>(loader)) {}
Renderer::~Renderer() {
    auto& g=*impl;
    if (!g.valid) return;
    for(auto [key,f]:g.fbos) g.DeleteFramebuffers(1,&f);
    for(auto [key,t]:g.colors) g.DeleteTextures(1,&t.object);
    for(auto [key,d]:g.depths) g.DeleteRenderbuffers(1,&d.object);
    g.DeleteTextures(1,&g.texture); g.DeleteBuffers(1,&g.vbo); g.DeleteBuffers(1,&g.ibo);
    g.DeleteVertexArrays(1,&g.vao); g.DeleteProgram(g.program);
}
bool Renderer::Init() {
    auto& g=*impl;
#define LOAD(type,name) g.name=reinterpret_cast<type>(g.load("gl" #name)); if(!g.name) { std::fprintf(stderr,"gldirect: missing gl%s\n",#name); return false; }
    GL_FUNCTIONS(LOAD)
#undef LOAD
    const char* vs=R"(#version 330 core
layout(location=0) in vec4 position;
layout(location=1) in vec4 color;
layout(location=2) in vec4 texcoord;
uniform vec4 viewport;
out vec4 diffuse; out vec2 uv;
void main() {
    float w=1.0/position.w;
    vec2 ndc=vec2((position.x+0.5-viewport.x)/viewport.z*2.0-1.0,
                   1.0-(position.y+0.5-viewport.y)/viewport.w*2.0);
    gl_Position=vec4(ndc*w,(2.0*position.z-1.0)*w,w);
    diffuse=color; uv=texcoord.xy;
})";
    const char* fs=R"(#version 330 core
in vec4 diffuse; in vec2 uv; out vec4 result;
uniform sampler2D image;
uniform int colorOp, alphaOp, alphaTest, alphaFunc, alphaRef;
bool compare(float a,float b) {
    if(alphaFunc==512) return false; if(alphaFunc==513) return a<b;
    if(alphaFunc==514) return a==b; if(alphaFunc==515) return a<=b;
    if(alphaFunc==516) return a>b; if(alphaFunc==517) return a!=b;
    if(alphaFunc==518) return a>=b; return true;
}
void main() {
    vec4 t=texture(image,uv); result=diffuse;
    if(colorOp==2) result.rgb=t.rgb;
    if(colorOp==4) result.rgb=t.rgb*diffuse.rgb;
    if(alphaOp==2) result.a=t.a;
    if(alphaOp==4) result.a=t.a*diffuse.a;
    if(alphaTest!=0 && !compare(result.a,float(alphaRef)/255.0)) discard;
})";
    GLuint v=g.Shader(GL_VERTEX_SHADER,vs), f=g.Shader(GL_FRAGMENT_SHADER,fs);
    if(!v || !f) { if(v)g.DeleteShader(v); if(f)g.DeleteShader(f); return false; }
    g.program=g.CreateProgram(); g.AttachShader(g.program,v); g.AttachShader(g.program,f); g.LinkProgram(g.program);
    g.DeleteShader(v); g.DeleteShader(f); GLint ok=0; g.GetProgramiv(g.program,GL_LINK_STATUS,&ok);
    if(!ok) { char log[4096]{}; g.GetProgramInfoLog(g.program,sizeof(log),nullptr,log); std::fprintf(stderr,"gldirect: link: %s\n",log); g.DeleteProgram(g.program); return false; }
    g.GenVertexArrays(1,&g.vao); g.BindVertexArray(g.vao);
    g.GenBuffers(1,&g.vbo); g.GenBuffers(1,&g.ibo); g.BindBuffer(GL_ARRAY_BUFFER,g.vbo);
    for(unsigned i=0;i<3;i++) { g.EnableVertexAttribArray(i); g.VertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(std::size_t(i)*16)); }
    g.GenTextures(1,&g.texture); g.GetIntegerv(GL_MAX_TEXTURE_SIZE,&g.limit); g.valid=true;
    return g.Error("initialization");
}
bool Renderer::Execute(const Submission& frame) {
    auto& g=*impl;
    for(const auto& c:frame.commands) {
        if(!g.Bind(c.target)) return false;
        if(c.kind==Command::Kind::Clear) {
            g.ColorMask(bool(c.flags&0x10),bool(c.flags&0x20),bool(c.flags&0x40),bool(c.flags&0x80));
            g.DepthMask(GL_TRUE); g.StencilMask(255);
            g.ClearColor(g.Channel(c.color,16),g.Channel(c.color,8),g.Channel(c.color,0),g.Channel(c.color,24));
            g.ClearDepth(c.z); g.ClearStencil(c.stencil);
            GLbitfield mask=(c.flags&0xf0 ? GL_COLOR_BUFFER_BIT:0)|(c.flags&1 ? GL_DEPTH_BUFFER_BIT:0)|(c.flags&2 ? GL_STENCIL_BUFFER_BIT:0);
            g.Toggle(GL_SCISSOR_TEST,!c.rects.empty());
            if(c.rects.empty()) g.Clear(mask);
            else for(auto r:c.rects) { g.Clip(r,c.target.height); g.Clear(mask); }
        } else {
            const auto& p=c.pipeline;
            g.Viewport(p.viewport.x,int(c.target.height)-p.viewport.y-p.viewport.h,p.viewport.w,p.viewport.h);
            g.DepthRange(p.min_z,p.max_z); g.Toggle(GL_DEPTH_TEST,p.depth); g.DepthMask(p.depth_write); if(p.depth) g.DepthFunc(p.depth_func);
            g.Toggle(GL_STENCIL_TEST,p.stencil); g.StencilMask(p.stencil_write); if(p.stencil) { g.StencilFunc(p.stencil_func,p.stencil_ref,p.stencil_mask);
            g.StencilOp(p.stencil_fail,p.stencil_zfail,p.stencil_pass); }
            g.Toggle(GL_BLEND,p.blend); if(p.blend) { g.BlendFunc(p.src_blend,p.dst_blend); g.BlendEquation(p.blend_op); }
            g.BlendColor(g.Channel(p.blend_color,16),g.Channel(p.blend_color,8),g.Channel(p.blend_color,0),g.Channel(p.blend_color,24));
            g.ColorMask(bool(p.color_mask&0x10000),bool(p.color_mask&0x100),bool(p.color_mask&1),bool(p.color_mask&0x1000000));
            g.Toggle(GL_CULL_FACE,p.cull!=0); if(p.cull) { g.CullFace(GL_FRONT); g.FrontFace(p.cull); }
            g.UseProgram(g.program); g.Uniform4f(g.GetUniformLocation(g.program,"viewport"),p.viewport.x,p.viewport.y,p.viewport.w,p.viewport.h);
            auto uniform=[&](const char* name,int value) { g.Uniform1i(g.GetUniformLocation(g.program,name),value); };
            uniform("image",0); uniform("colorOp",p.color_op); uniform("alphaOp",p.alpha_op);
            uniform("alphaTest",p.alpha_test); uniform("alphaFunc",p.alpha_func); uniform("alphaRef",p.alpha_ref);
            g.ActiveTexture(GL_TEXTURE0); g.BindTexture(GL_TEXTURE_2D,g.texture);
            const unsigned white=0xffffffff;
            // A bounded one-entry upload cache compares the actual decoded content. This
            // also invalidates direct guest writes, address reuse and palette changes.
            if(!g.uploaded || g.cached.width!=c.texture.width || g.cached.height!=c.texture.height || g.cached.bgra!=c.texture.bgra) {
                g.TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,c.texture.width?c.texture.width:1,c.texture.height?c.texture.height:1,0,GL_BGRA,GL_UNSIGNED_BYTE,c.texture.bgra.empty()?&white:c.texture.bgra.data());
                g.cached=c.texture; g.uploaded=true;
            }
            g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,c.texture.min_filter==2?GL_LINEAR:GL_NEAREST);
            g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,c.texture.mag_filter==2?GL_LINEAR:GL_NEAREST);
            auto wrap=[](unsigned mode) { return mode==1?GL_REPEAT:mode==2?GL_MIRRORED_REPEAT:GL_CLAMP_TO_EDGE; };
            g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,wrap(c.texture.wrap_u));
            g.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,wrap(c.texture.wrap_v));
            g.BindVertexArray(g.vao); g.BindBuffer(GL_ARRAY_BUFFER,g.vbo);
            g.BufferData(GL_ARRAY_BUFFER,c.vertices.size()*sizeof(Vertex),c.vertices.data(),GL_STREAM_DRAW);
            g.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,g.ibo);
            g.BufferData(GL_ELEMENT_ARRAY_BUFFER,c.indices.size()*4,c.indices.data(),GL_STREAM_DRAW);
            g.Toggle(GL_SCISSOR_TEST,!p.scissors.empty());
            auto draw=[&] { if(c.indices.empty()) g.DrawArrays(c.topology,0,c.vertices.size()); else g.DrawElements(c.topology,c.indices.size(),GL_UNSIGNED_INT,nullptr); };
            if(p.scissors.empty()) draw(); else for(auto r:p.scissors) { g.Clip(r,c.target.height); draw(); }
        }
        if(!g.Error("guest command")) return false;
    }
    g.front=frame.front;
    return true;
}
bool Renderer::Present(int width,int height) {
    auto& g=*impl;
    if(width<=0 || height<=0) return true;
    g.Disable(GL_SCISSOR_TEST); g.BindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    auto it=g.colors.find(g.front);
    if(it==g.colors.end()) { g.ColorMask(1,1,1,1); g.ClearColor(0,0,0,1); g.Clear(GL_COLOR_BUFFER_BIT); }
    else {
        // Read the color alone: another draw FBO may still reference an older-sized
        // depth attachment after guest memory is reused for a resized surface.
        auto& read_fbo=g.fbos[{g.front,0}];
        if(!read_fbo) g.GenFramebuffers(1,&read_fbo);
        g.BindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);
        g.FramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,it->second.object,0);
        if(g.CheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return false;
        g.BlitFramebuffer(0,0,it->second.w,it->second.h,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    return g.Error("present");
}
}
