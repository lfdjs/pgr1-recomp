#include "renderer.h"
#include <GL/glcorearb.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <map>
#include <deque>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <utility>

namespace pgr::gldirect {
#define GL_FUNCTIONS(X) \
 X(PFNGLBINDBUFFERRANGEPROC, BindBufferRange) X(PFNGLDRAWELEMENTSBASEVERTEXPROC, DrawElementsBaseVertex) \
 X(PFNGLPIXELSTOREIPROC, PixelStorei) X(PFNGLFLUSHPROC, Flush) \
 X(PFNGLPOLYGONOFFSETPROC, PolygonOffset) X(PFNGLPOLYGONMODEPROC, PolygonMode) \
 X(PFNGLPROVOKINGVERTEXPROC, ProvokingVertex) \
 X(PFNGLGENQUERIESPROC, GenQueries) X(PFNGLQUERYCOUNTERPROC, QueryCounter) \
 X(PFNGLGETQUERYOBJECTIVPROC, GetQueryObjectiv) X(PFNGLGETQUERYOBJECTUI64VPROC, GetQueryObjectui64v) \
 X(PFNGLDELETEQUERIESPROC, DeleteQueries) X(PFNGLREADPIXELSPROC, ReadPixels) \
 X(PFNGLGETATTRIBLOCATIONPROC, GetAttribLocation) X(PFNGLGETUNIFORMBLOCKINDEXPROC, GetUniformBlockIndex) \
 X(PFNGLUNIFORMBLOCKBINDINGPROC, UniformBlockBinding) X(PFNGLBINDBUFFERBASEPROC, BindBufferBase) \
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
    GLuint program=0, vao=0, vbo=0, ibo=0, texture=0, uniforms[2]{}, cube_fbo=0;
    std::map<std::shared_ptr<const ShaderPair>,GLuint,std::owner_less<std::shared_ptr<const ShaderPair>>> programs;
    std::map<std::shared_ptr<const TexturePixels>,GLuint,std::owner_less<std::shared_ptr<const TexturePixels>>> images;
    GLint limit=0, uniform_alignment=256;
    std::map<GLuint,GLuint> program_vaos;
    std::map<GLuint,std::array<unsigned,6>> sampler_states;
    struct Offset { GLint vertex=0; size_t index=0, vc=0, dc=0; };
    std::vector<Offset> offsets;
    std::vector<Vertex> vertex_staging;
    std::vector<uint32_t> index_staging;
    std::vector<uint8_t> uniform_staging;
    void Stage(const Submission& frame) {
        vertex_staging.clear(); index_staging.clear(); uniform_staging.clear(); offsets.resize(frame.commands.size());
        auto align=[&](size_t size) { return (size+uniform_alignment-1)/uniform_alignment*uniform_alignment; };
        for(size_t i=0;i<frame.commands.size();i++) {
            const auto& c=frame.commands[i]; if(c.kind!=Command::Kind::Draw) continue;
            auto& o=offsets[i]; o.vertex=vertex_staging.size(); o.index=index_staging.size()*4;
            vertex_staging.insert(vertex_staging.end(),c.vertices.begin(),c.vertices.end());
            index_staging.insert(index_staging.end(),c.indices.begin(),c.indices.end());
            if(c.shaders) {
                o.vc=align(uniform_staging.size()); uniform_staging.resize(o.vc+sizeof(c.vertex_constants));
                std::memcpy(uniform_staging.data()+o.vc,c.vertex_constants.data(),sizeof(c.vertex_constants));
                o.dc=align(uniform_staging.size()); uniform_staging.resize(o.dc+c.draw_constants.size());
                std::memcpy(uniform_staging.data()+o.dc,c.draw_constants.data(),c.draw_constants.size());
            }
        }
        BindVertexArray(vao); BindBuffer(GL_ARRAY_BUFFER,vbo);
        BufferData(GL_ARRAY_BUFFER,vertex_staging.size()*sizeof(Vertex),vertex_staging.data(),GL_STREAM_DRAW);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
        BufferData(GL_ELEMENT_ARRAY_BUFFER,index_staging.size()*4,index_staging.data(),GL_STREAM_DRAW);
        BindBuffer(GL_UNIFORM_BUFFER,uniforms[0]);
        BufferData(GL_UNIFORM_BUFFER,uniform_staging.size(),uniform_staging.data(),GL_STREAM_DRAW);
    }
    struct Image { GLuint object=0; unsigned w=0,h=0; bool opaque=false; unsigned format=0; };
    std::map<unsigned,Image> colors, depths;
    std::map<std::pair<unsigned,unsigned>,GLuint> fbos;
    struct Query { GLuint ids[2]; uint64_t frame; };
    std::deque<Query> queries;
    Image displayed; GLuint display_fbo=0; bool has_display=false;
    unsigned front=0; float output_aspect=0;
    std::array<int,4> last_scissor={-1,-1,-1,-1};
    bool valid=false, uploaded=false, debug=false;
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
    GLuint Program(const std::shared_ptr<const ShaderPair>& pair) {
        if(auto i=programs.find(pair);i!=programs.end()) return i->second;
        GLuint v=Shader(GL_VERTEX_SHADER,pair->vertex.c_str());
        GLuint f=Shader(GL_FRAGMENT_SHADER,pair->fragment.c_str());
        if(!v || !f) { if(v)DeleteShader(v); if(f)DeleteShader(f); return 0; }
        GLuint p=CreateProgram(); AttachShader(p,v); AttachShader(p,f); LinkProgram(p);
        DeleteShader(v); DeleteShader(f); GLint ok=0; GetProgramiv(p,GL_LINK_STATUS,&ok);
        if(!ok) { char log[8192]{}; GetProgramInfoLog(p,sizeof(log),nullptr,log); std::fprintf(stderr,"gldirect shader link: %s\n",log); DeleteProgram(p); return 0; }
        for(unsigned i=0;i<2;i++) {
            auto index=GetUniformBlockIndex(p,i==0?"VC":"DC");
            if(index!=GL_INVALID_INDEX) UniformBlockBinding(p,index,i);
        }
        UseProgram(p);
        for(unsigned i=0;i<4;i++) {
            auto n=std::to_string(i);
            Uniform1i(GetUniformLocation(p,("SPIRV_Cross_Combinedtex"+n+"smp"+n).c_str()),i);
        }
        GLuint array=0; GenVertexArrays(1,&array); BindVertexArray(array);
        BindBuffer(GL_ARRAY_BUFFER,vbo); BindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
        for(unsigned r=0;r<16;r++) {
            GLint loc=GetAttribLocation(p,("i_v"+std::to_string(r)).c_str()); if(loc<0) continue;
            EnableVertexAttribArray(loc);
            VertexAttribPointer(loc,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,attributes)+r*16));
        }
        program_vaos[p]=array;
        programs.emplace(pair,p); return p;
    }
    bool Textures(const std::array<Texture,4>& textures) {
        GLint draw_fbo=0,read_fbo=0; GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_fbo); GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_fbo);
        // Reserve all four stages before binding any of them: eviction in the
        // middle of this draw would unbind a texture just installed on an earlier unit.
        if(images.size()+4>256) { for(auto& [key,id]:images) DeleteTextures(1,&id); images.clear(); sampler_states.clear(); }
        for(unsigned i=0;i<4;i++) {
            const auto& t=textures[i]; ActiveTexture(GL_TEXTURE0+i);
            GLuint object=0; GLenum target=t.cube?GL_TEXTURE_CUBE_MAP:GL_TEXTURE_2D;
            if(t.render_target) {
                auto image=colors.find(t.render_target);
                if(image==colors.end()) { std::fputs("gldirect: missing sampled GPU surface\n",stderr); return false; }
                object=image->second.object;
            } else if(t.pixels) {
                auto found=images.find(t.pixels);
                if(found!=images.end()) object=found->second;
                else {
                    GenTextures(1,&object); BindTexture(target,object);
                    unsigned levels=t.pixels->levels.size()/(t.cube?6:1);
                    for(unsigned face=0;face<(t.cube?6u:1u);face++) for(unsigned level=0;level<levels;level++) {
                        const auto& pixels=t.pixels->levels[face*levels+level];
                        TexImage2D(t.cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:GL_TEXTURE_2D,level,GL_RGBA8,
                            std::max(1u,t.width>>level),std::max(1u,t.height>>level),0,GL_BGRA,GL_UNSIGNED_BYTE,pixels.data());
                    }
                    TexParameteri(target,GL_TEXTURE_MAX_LEVEL,levels-1);
                    images.emplace(t.pixels,object);
                }
            }
            BindTexture(target,object);
            if(!object) continue;
            const std::array<unsigned,6> state={t.wrap_u,t.wrap_v,t.min_filter,t.mag_filter,t.mip_filter,unsigned(t.cube)};
            if(!sampler_states.contains(object) || sampler_states[object]!=state) {
            sampler_states[object]=state;
            auto wrap=[](unsigned v) { return v==1?GL_REPEAT:v==2?GL_MIRRORED_REPEAT:v==4?GL_CLAMP_TO_BORDER:GL_CLAMP_TO_EDGE; };
            TexParameteri(target,GL_TEXTURE_WRAP_S,wrap(t.wrap_u)); TexParameteri(target,GL_TEXTURE_WRAP_T,wrap(t.wrap_v));
            TexParameteri(target,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
            TexParameteri(target,GL_TEXTURE_MAG_FILTER,t.mag_filter==1?GL_NEAREST:GL_LINEAR);
            GLenum filter=t.min_filter==1?GL_NEAREST:GL_LINEAR;
            if(t.mip_filter && !t.render_target) filter=t.mip_filter==1 ? (t.min_filter==1?GL_NEAREST_MIPMAP_NEAREST:GL_LINEAR_MIPMAP_NEAREST) : (t.min_filter==1?GL_NEAREST_MIPMAP_LINEAR:GL_LINEAR_MIPMAP_LINEAR);
            TexParameteri(target,GL_TEXTURE_MIN_FILTER,filter);
            }
            if(t.cube) for(unsigned face=0;face<6;face++) if(t.face_targets[face]) {
                auto source=colors.find(t.face_targets[face]);
                if(source==colors.end()) continue;
                auto& read=fbos[{t.face_targets[face],0}]; if(!read) GenFramebuffers(1,&read);
                BindFramebuffer(GL_READ_FRAMEBUFFER,read);
                FramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source->second.object,0);
                if(!cube_fbo) GenFramebuffers(1,&cube_fbo);
                BindFramebuffer(GL_DRAW_FRAMEBUFFER,cube_fbo);
                FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,object,0);
                Disable(GL_SCISSOR_TEST);
                BlitFramebuffer(0,0,source->second.w,source->second.h,0,t.height,t.width,0,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            }
        }
        BindFramebuffer(GL_DRAW_FRAMEBUFFER,draw_fbo); BindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);
        return true;
    }
    bool Snapshot(unsigned color) {
        auto it=colors.find(color); has_display=it!=colors.end();
        if(!has_display) return true;
        const auto& source=it->second;
        if(!displayed.object) GenTextures(1,&displayed.object);
        ActiveTexture(GL_TEXTURE0); BindTexture(GL_TEXTURE_2D,displayed.object);
        if(displayed.w!=source.w || displayed.h!=source.h) {
            TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,source.w,source.h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            displayed.w=source.w; displayed.h=source.h;
        }
        if(!display_fbo) GenFramebuffers(1,&display_fbo);
        BindFramebuffer(GL_DRAW_FRAMEBUFFER,display_fbo);
        FramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,displayed.object,0);
        auto& read=fbos[{color,0}]; if(!read) GenFramebuffers(1,&read);
        BindFramebuffer(GL_READ_FRAMEBUFFER,read);
        FramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source.object,0);
        Disable(GL_SCISSOR_TEST);
        BlitFramebuffer(0,0,source.w,source.h,0,0,source.w,source.h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        return Error("completed frame snapshot");
    }
    bool Bind(const Target& t) {
        if(!t.width || !t.height || t.width>unsigned(limit) || t.height>unsigned(limit)) return false;
        auto& c=colors[t.color];
        if(!c.object) GenTextures(1,&c.object);
        bool resized=c.w!=t.width || c.h!=t.height || c.opaque!=t.opaque || c.format!=t.format;
        if(resized) {
            BindTexture(GL_TEXTURE_2D,c.object);
            TexImage2D(GL_TEXTURE_2D,0,(t.format==0x05 || t.format==0x11)?GL_RGB565:t.opaque?GL_RGB8:GL_RGBA8,t.width,t.height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            sampler_states.erase(c.object);
            c.w=t.width; c.h=t.height; c.opaque=t.opaque; c.format=t.format;
        }
        auto& f=fbos[{t.color,t.depth}]; bool fresh=!f;
        if(fresh) GenFramebuffers(1,&f);
        BindFramebuffer(GL_FRAMEBUFFER,f);
        if(fresh) FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,c.object,0);
        if(t.depth) {
            auto& d=depths[t.depth]; if(!d.object) GenRenderbuffers(1,&d.object);
            bool changed=d.w!=t.width || d.h!=t.height || d.format!=t.depth_format;
            bool depth16=t.depth_format==0x2c || t.depth_format==0x30;
            if(changed) {
                BindRenderbuffer(GL_RENDERBUFFER,d.object);
                RenderbufferStorage(GL_RENDERBUFFER,depth16?GL_DEPTH_COMPONENT16:GL_DEPTH24_STENCIL8,t.width,t.height);
                d.w=t.width; d.h=t.height; d.format=t.depth_format; resized=true;
            }
            if(fresh || changed) {
                FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,0);
                FramebufferRenderbuffer(GL_FRAMEBUFFER,depth16?GL_DEPTH_ATTACHMENT:GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,d.object);
            }
        }
        if((fresh || resized) && CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {
            std::fputs("gldirect: incomplete target\n",stderr); return false;
        }
        return true;
    }
    void Toggle(GLenum cap,bool on) { if(on) Enable(cap); else Disable(cap); }
    void Clip(Rect r,unsigned h) {
        const std::array<int,4> box={r.x,int(h)-r.y-r.h,std::max(0,r.w),std::max(0,r.h)};
        if(box!=last_scissor) { Scissor(box[0],box[1],box[2],box[3]); last_scissor=box; }
    }
    static float Channel(unsigned c,unsigned shift) { return float((c>>shift)&255)/255; }
};
Renderer::Renderer(Loader loader):impl(std::make_unique<Impl>(loader)) {}
Renderer::~Renderer() {
    auto& g=*impl;
    if (!g.valid) return;
    for(auto& [key,array]:g.program_vaos) g.DeleteVertexArrays(1,&array);
    for(auto& [key,p]:g.programs) g.DeleteProgram(p);
    for(auto& [key,t]:g.images) g.DeleteTextures(1,&t);
    g.DeleteBuffers(2,g.uniforms); g.DeleteFramebuffers(1,&g.cube_fbo);
    g.DeleteTextures(1,&g.displayed.object); g.DeleteFramebuffers(1,&g.display_fbo);
    for(auto q:g.queries) g.DeleteQueries(2,q.ids);
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
    const char* debugging=std::getenv("PGR1_GL_DEBUG"); g.debug=debugging && *debugging && *debugging!='0';
    auto debug=reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKPROC>(g.load("glDebugMessageCallback"));
    if(g.debug && debug) {
        g.Enable(GL_DEBUG_OUTPUT); g.Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        debug([](GLenum,GLenum type,GLuint,GLenum severity,GLsizei,const GLchar* message,const void*) {
            if(type==GL_DEBUG_TYPE_ERROR || severity==GL_DEBUG_SEVERITY_HIGH)
                std::fprintf(stderr,"gldirect driver: %s\n",message);
        },nullptr);
    }
    g.PixelStorei(GL_PACK_ALIGNMENT,1); g.PixelStorei(GL_UNPACK_ALIGNMENT,1);
    g.ProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
    g.GenVertexArrays(1,&g.vao); g.BindVertexArray(g.vao);
    g.GenBuffers(1,&g.vbo); g.GenBuffers(1,&g.ibo); g.GenBuffers(2,g.uniforms); g.BindBuffer(GL_ARRAY_BUFFER,g.vbo);
    for(unsigned i=0;i<3;i++) { g.EnableVertexAttribArray(i); g.VertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(std::size_t(i)*16)); }
    g.GenTextures(1,&g.texture); g.GetIntegerv(GL_MAX_TEXTURE_SIZE,&g.limit); g.GetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,&g.uniform_alignment); g.valid=true;
    return g.Error("initialization");
}
bool Renderer::Execute(const Submission& frame) {
    auto& g=*impl;
    Impl::Query query{}; query.frame=frame.frame;
    bool measure=g.queries.size()<64;
    if(measure) { g.GenQueries(2,query.ids); g.QueryCounter(query.ids[0],GL_TIMESTAMP); }
    struct EndQuery {
        Impl& g; Impl::Query q; bool measure;
        ~EndQuery() { if(measure) { g.QueryCounter(q.ids[1],GL_TIMESTAMP); g.queries.push_back(q); } g.Flush(); }
    } end{g,query,measure};
    g.Stage(frame);
    for(size_t command=0;command<frame.commands.size();command++) {
        const auto& c=frame.commands[command]; const auto& offset=g.offsets[command];
        if(!g.Bind(c.target)) return false;
        if(c.kind==Command::Kind::Readback) {
            std::vector<uint32_t> pixels(size_t(c.target.width)*c.target.height);
            if(c.read_depth && (c.target.depth_format==0x2c || c.target.depth_format==0x30)) {
                std::vector<uint16_t> depth(pixels.size());
                g.ReadPixels(0,0,c.target.width,c.target.height,GL_DEPTH_COMPONENT,GL_UNSIGNED_SHORT,depth.data());
                std::copy(depth.begin(),depth.end(),pixels.begin());
            } else g.ReadPixels(0,0,c.target.width,c.target.height,c.read_depth?GL_DEPTH_STENCIL:GL_BGRA,c.read_depth?GL_UNSIGNED_INT_24_8:GL_UNSIGNED_BYTE,pixels.data());
            if(!g.Error("readback")) { c.readback->set_exception(std::make_exception_ptr(std::runtime_error("GL readback failed"))); return false; }
            c.readback->set_value(std::move(pixels));
        } else if(c.kind==Command::Kind::Copy) {
            auto source=g.colors.find(c.source); if(source==g.colors.end()) return false;
            auto& f=g.fbos[{c.source,0}]; if(!f) g.GenFramebuffers(1,&f);
            g.BindFramebuffer(GL_READ_FRAMEBUFFER,f);
            g.FramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source->second.object,0);
            g.Disable(GL_SCISSOR_TEST);
            for(size_t i=0;i<c.rects.size();i++) {
                auto a=c.rects[i], b=c.destination_rects[i];
                g.BlitFramebuffer(a.x,int(c.source_height)-a.y-a.h,a.x+a.w,int(c.source_height)-a.y,
                    b.x,int(c.target.height)-b.y-b.h,b.x+b.w,int(c.target.height)-b.y,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            }
        } else if(c.kind==Command::Kind::Clear) {
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
            g.PolygonMode(GL_FRONT_AND_BACK,p.fill);
            g.Toggle(GL_POLYGON_OFFSET_FILL,p.depth_bias!=0 || p.depth_slope!=0); g.PolygonOffset(p.depth_slope,p.depth_bias);
            g.Viewport(p.viewport.x,int(c.target.height)-p.viewport.y-p.viewport.h,p.viewport.w,p.viewport.h);
            g.DepthRange(p.min_z,p.max_z); g.Toggle(GL_DEPTH_TEST,p.depth); g.DepthMask(p.depth_write); if(p.depth) g.DepthFunc(p.depth_func);
            g.Toggle(GL_STENCIL_TEST,p.stencil); g.StencilMask(p.stencil_write); if(p.stencil) { g.StencilFunc(p.stencil_func,p.stencil_ref,p.stencil_mask);
            g.StencilOp(p.stencil_fail,p.stencil_zfail,p.stencil_pass); }
            g.Toggle(GL_BLEND,p.blend); if(p.blend) { g.BlendFunc(p.src_blend,p.dst_blend); g.BlendEquation(p.blend_op); }
            g.BlendColor(g.Channel(p.blend_color,16),g.Channel(p.blend_color,8),g.Channel(p.blend_color,0),g.Channel(p.blend_color,24));
            g.ColorMask(bool(p.color_mask&0x10000),bool(p.color_mask&0x100),bool(p.color_mask&1),bool(p.color_mask&0x1000000));
            g.Toggle(GL_CULL_FACE,p.cull!=0);
            const GLenum front=p.front==GL_CCW?GL_CCW:GL_CW;
            g.FrontFace(front); if(p.cull) g.CullFace(p.cull==front?GL_FRONT:GL_BACK);
            g.BindVertexArray(g.vao); g.BindBuffer(GL_ARRAY_BUFFER,g.vbo);
            if(c.shaders) {
                GLuint p=g.Program(c.shaders); if(!p) return false;
                g.UseProgram(p);
                g.Viewport(0,0,c.target.width,c.target.height); g.DepthRange(0,1);
                g.BindVertexArray(g.program_vaos[p]);
                g.BindBufferRange(GL_UNIFORM_BUFFER,0,g.uniforms[0],offset.vc,sizeof(c.vertex_constants));
                g.BindBufferRange(GL_UNIFORM_BUFFER,1,g.uniforms[0],offset.dc,c.draw_constants.size());
                if(!g.Textures(c.textures)) return false;
            } else {
                // Retain the original FVF fixture path for independent pixel regression tests.
                for(unsigned i=0;i<3;i++) { g.EnableVertexAttribArray(i); g.VertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(size_t(i)*16)); }
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
            }
            g.Enable(GL_SCISSOR_TEST);
            auto draw=[&] { if(c.indices.empty()) g.DrawArrays(c.topology,offset.vertex,c.vertices.size()); else g.DrawElementsBaseVertex(c.topology,c.indices.size(),GL_UNSIGNED_INT,reinterpret_cast<void*>(offset.index),offset.vertex); };
            if(p.scissors.empty()) { g.Clip(p.viewport,c.target.height); draw(); }
            else for(auto r:p.scissors) {
                int x=std::max(r.x,p.viewport.x),y=std::max(r.y,p.viewport.y);
                int right=std::min(r.x+r.w,p.viewport.x+p.viewport.w),bottom=std::min(r.y+r.h,p.viewport.y+p.viewport.h);
                g.Clip({x,y,std::max(0,right-x),std::max(0,bottom-y)},c.target.height); draw();
            }
        }
        if(g.debug && !g.Error("guest command")) return false;
    }
    if(frame.present) { g.front=frame.front; if(!g.Snapshot(frame.front)) return false; }
    return g.Error("guest batch");
}
void Renderer::SetAspect(float aspect) { impl->output_aspect=aspect; }
bool Renderer::Present(int width,int height) {
    auto& g=*impl;
    if(width<=0 || height<=0) return true;
    g.Disable(GL_SCISSOR_TEST); g.BindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    if(!g.has_display) { g.ColorMask(1,1,1,1); g.ClearColor(0,0,0,1); g.Clear(GL_COLOR_BUFFER_BIT); }
    else {
        g.BindFramebuffer(GL_READ_FRAMEBUFFER,g.display_fbo);
        int w=width,h=height;
        if(g.output_aspect>0) {
            if(float(width)/height>g.output_aspect) w=int(height*g.output_aspect+.5f);
            else h=int(width/g.output_aspect+.5f);
            g.ColorMask(1,1,1,1); g.ClearColor(0,0,0,1); g.Clear(GL_COLOR_BUFFER_BIT);
        }
        int x=(width-w)/2,y=(height-h)/2;
        g.BlitFramebuffer(0,0,g.displayed.w,g.displayed.h,x,y,x+w,y+h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    return g.Error("present");
}
}

namespace pgr::gldirect {
std::vector<Renderer::GpuTime> Renderer::PollGpu() {
    auto& g=*impl; std::vector<GpuTime> out;
    while(!g.queries.empty()) {
        auto q=g.queries.front(); GLint done=0;
        g.GetQueryObjectiv(q.ids[1],GL_QUERY_RESULT_AVAILABLE,&done); if(!done) break;
        GLuint64 begin=0,end=0; g.GetQueryObjectui64v(q.ids[0],GL_QUERY_RESULT,&begin); g.GetQueryObjectui64v(q.ids[1],GL_QUERY_RESULT,&end);
        out.push_back({q.frame,end-begin}); g.DeleteQueries(2,q.ids); g.queries.pop_front();
    }
    return out;
}
bool Renderer::Capture(const char* path) {
    auto& g=*impl; if(!g.has_display) return false;
    g.BindFramebuffer(GL_READ_FRAMEBUFFER,g.display_fbo);
    std::vector<unsigned char> px(size_t(g.displayed.w)*g.displayed.h*4);
    g.ReadPixels(0,0,g.displayed.w,g.displayed.h,GL_RGBA,GL_UNSIGNED_BYTE,px.data());
    std::ofstream out(path,std::ios::binary); out<<"P6\n"<<g.displayed.w<<" "<<g.displayed.h<<"\n255\n";
    for(int y=int(g.displayed.h)-1;y>=0;y--) for(unsigned x=0;x<g.displayed.w;x++) out.write(reinterpret_cast<char*>(px.data()+(size_t(y)*g.displayed.w+x)*4),3);
    return out.good() && g.Error("capture");
}
}

namespace pgr::gldirect {
void Renderer::Compare(const Submission& frame) {
    if(frame.reference_pixels.empty()) return;
    auto& g=*impl;
    const unsigned w=frame.reference_width,h=frame.reference_height;
    if(!g.has_display) return;
    std::vector<uint32_t> actual(size_t(w)*h);
    g.BindFramebuffer(GL_READ_FRAMEBUFFER,g.display_fbo);
    g.ReadPixels(0,0,w,h,GL_BGRA,GL_UNSIGNED_BYTE,actual.data());
    uint64_t error=0,different=0; unsigned max_error=0;
    std::ofstream ref("compare-reference.ppm",std::ios::binary); ref<<"P6\n"<<w<<" "<<h<<"\n255\n";
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
        uint32_t a=actual[size_t(h-1-y)*w+x], b=frame.reference_pixels[size_t(y)*w+x];
        unsigned pixel_error=0;
        for(unsigned shift:{0u,8u,16u}) {
            unsigned delta=unsigned(std::abs(int((a>>shift)&255)-int((b>>shift)&255)));
            error+=delta; max_error=std::max(max_error,delta); pixel_error=std::max(pixel_error,delta);
        }
        different+=pixel_error>4;
        char rgb[3]={char(b>>16),char(b>>8),char(b)}; ref.write(rgb,3);
    }
    Capture("compare-gpu.ppm");
    std::ofstream report("compare.txt");
    report<<"frame="<<frame.frame<<" mean_RGB_error="<<double(error)/(w*h*3)
          <<" max_channel_error="<<max_error<<" pixels_error_above_4="<<different<<"/"<<w*h<<"\n";
    std::fprintf(stderr,"gldirect: CPU/GPU frame %llu compared; see compare.txt and compare-*.ppm\n",(unsigned long long)frame.frame);
}
}
