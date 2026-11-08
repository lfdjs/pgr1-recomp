#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace pgr::gldirect {
// Owned, API-neutral draw data. Guest pointers and SDK objects never reach GL.
struct Vertex { std::array<float,4> position, color, uv; };
struct Target { std::uint32_t color=0, depth=0, width=640, height=480; bool opaque=false; };
struct Rect { int x=0, y=0, w=640, h=480; };
struct Pipeline {
    Rect viewport;
    float min_z=0, max_z=1;
    bool depth=false, depth_write=false, blend=false, stencil=false, alpha_test=false;
    std::uint32_t depth_func=0x207, src_blend=1, dst_blend=0, blend_op=0x8006;
    std::uint32_t stencil_func=0x207, stencil_ref=0, stencil_mask=255, stencil_write=255;
    std::uint32_t stencil_fail=0x1e00, stencil_zfail=0x1e00, stencil_pass=0x1e00;
    std::uint32_t color_mask=0x01010101, blend_color=0, cull=0, front=0x900;
    std::uint32_t alpha_func=0x207, alpha_ref=0, color_op=1, alpha_op=1;
    std::vector<Rect> scissors;
};
struct Texture {
    std::uint32_t width=0, height=0;
    std::vector<std::uint32_t> bgra;
    std::uint32_t wrap_u=3, wrap_v=3, min_filter=1, mag_filter=1;
};
struct Command {
    enum class Kind { Clear, Draw } kind=Kind::Clear;
    Target target;
    Pipeline pipeline;
    std::uint32_t flags=0, color=0, stencil=0;
    float z=1;
    std::vector<Rect> rects;
    std::uint32_t topology=4; // GL_POINTS / LINES / TRIANGLES (+ strips)
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Texture texture;
};
struct Submission {
    std::uint64_t frame=0, draws=0, unsupported=0;
    std::uint32_t front=0;
    std::vector<Command> commands;
};
// Bounded FIFO: preserve incremental frames, clear/draw ordering and guest buffer lifetimes.
// Stop releases a producer blocked by a closing window.
void Submit(Submission frame);
bool Take(Submission& frame);
void Stop();
} // namespace pgr::gldirect
