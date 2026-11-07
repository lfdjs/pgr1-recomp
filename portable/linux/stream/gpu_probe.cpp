// Real desktop OpenGL 3.3 bring-up test: a moving gradient triangle.
// Standalone proof that SDL3, GL context, GLSL shader compilation and draw calls work.
// NOT connected to Xbox's GPU state, the PGR1 game's shaders or guest textures.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>

namespace {
using GLenum = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02;

#define GL_PROC(TYPE,NAME) \
    auto NAME = reinterpret_cast<TYPE>(SDL_GL_GetProcAddress(#NAME)); \
    if (!NAME) { std::fprintf(stderr, "OpenGL missing %s\n", #NAME); return 2; }

}  // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("PGR1 Portable - OpenGL pipeline test", 960, 540,
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) { std::fprintf(stderr, "%s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx) { std::fprintf(stderr, "GL context: %s\n", SDL_GetError()); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    GL_PROC(const unsigned char* (*)(GLenum), glGetString)
    std::printf("OpenGL vendor: %s\nrenderer: %s\nversion: %s\n",
                glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GL_PROC(GLuint (*)(GLenum), glCreateShader)
    GL_PROC(void (*)(GLuint, GLsizei, const char* const*, const GLint*), glShaderSource)
    GL_PROC(void (*)(GLuint), glCompileShader)
    GL_PROC(void (*)(GLuint, GLenum, GLint*), glGetShaderiv)
    GL_PROC(void (*)(GLuint, GLsizei, GLsizei*, char*), glGetShaderInfoLog)
    GL_PROC(GLuint (*)(), glCreateProgram)
    GL_PROC(void (*)(GLuint, GLuint), glAttachShader)
    GL_PROC(void (*)(GLuint), glLinkProgram)
    GL_PROC(void (*)(GLuint, GLenum, GLint*), glGetProgramiv)
    GL_PROC(void (*)(GLuint), glUseProgram)
    GL_PROC(void (*)(GLsizei, GLuint*), glGenVertexArrays)
    GL_PROC(void (*)(GLuint), glBindVertexArray)
    GL_PROC(void (*)(GLint, GLint, GLsizei, GLsizei), glViewport)
    GL_PROC(void (*)(float, float, float, float), glClearColor)
    GL_PROC(void (*)(unsigned int), glClear)
    GL_PROC(void (*)(GLenum, GLint, GLsizei), glDrawArrays)
    GL_PROC(void (*)(GLuint), glDeleteShader)
    GL_PROC(void (*)(GLuint), glDeleteProgram)
    GL_PROC(void (*)(GLsizei, const GLuint*), glDeleteVertexArrays)
    GL_PROC(GLint (*)(GLuint, const char*), glGetUniformLocation)
    GL_PROC(void (*)(GLint, float), glUniform1f)

    const char* vert = R"GLSL(#version 330 core
    out vec3 vColor;
    uniform float uTime;
    const vec2 p[3] = vec2[](vec2(-0.7,-0.6),vec2(0.7,-0.6),vec2(0.0,0.7));
    const vec3 c[3] = vec3[](vec3(1,0.3,0.2),vec3(0.2,0.9,0.4),vec3(0.3,0.5,1));
    void main(){ vec2 q=p[gl_VertexID]; q.y += 0.1*sin(uTime+q.x*4.0); gl_Position=vec4(q,0,1); vColor=c[gl_VertexID]; }
    )GLSL";
    const char* frag = R"GLSL(#version 330 core
    in vec3 vColor; out vec4 outColor;
    void main(){outColor=vec4(vColor,1.0);}
    )GLSL";
    auto shader = [&](GLenum type, const char* src) -> GLuint {
        GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);
        GLint ok=0;
        glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok) { char msg[4096]{}; glGetShaderInfoLog(id, sizeof msg, nullptr, msg); std::fprintf(stderr, "GLSL error: %s\n", msg); return 0; }
        return id;
    };
    GLuint vs=shader(GL_VERTEX_SHADER,vert), fs=shader(GL_FRAGMENT_SHADER,frag);
    if (!vs || !fs) return 3;
    GLuint program=glCreateProgram();
    glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
    GLint linked=0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) { std::fprintf(stderr, "GLSL link failed\n"); return 3; }
    glDeleteShader(vs); glDeleteShader(fs);
    GLuint vao=0; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glUseProgram(program);
    GLint time_uniform=glGetUniformLocation(program,"uTime");
    SDL_GL_SetSwapInterval(1);
    bool running=true;
    while (running) {
        SDL_Event e{};
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE)) running=false;
        }
        int w=0,h=0;
        SDL_GetWindowSizeInPixels(window,&w,&h);
        glViewport(0,0,w,h);
        glClearColor(0.035f,0.055f,0.10f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform1f(time_uniform, float(SDL_GetTicks())*0.001f);
        glDrawArrays(GL_TRIANGLES,0,3);
        SDL_GL_SwapWindow(window);
    }
    glDeleteVertexArrays(1,&vao);
    glDeleteProgram(program);
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
