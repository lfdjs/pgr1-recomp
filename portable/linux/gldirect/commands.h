#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <future>

namespace pgr::gldirect {
// Owned, API-neutral draw data. Guest pointers and SDK objects never reach GL.
struct Vertex {
    std::array<float,4> position, color, uv;
    std::array<std::array<float,4>,16> attributes{};
};
struct ShaderPair { std::string vertex, fragment; };
struct TexturePixels { std::vector<std::vector<std::uint32_t>> levels; };
struct Target { std::uint32_t color=0, depth=0, width=640, height=480; bool opaque=false; std::uint32_t format=0x12, depth_format=0x2e; };
struct Rect { int x=0, y=0, w=640, h=480; };
struct Pipeline {
    Rect viewport;
    float clip_scale=1, clip_pivot=0;
    float min_z=0, max_z=1, depth_bias=0, depth_slope=0;
    std::uint32_t fill=0x1b02;
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
    std::uint32_t wrap_u=3, wrap_v=3, min_filter=1, mag_filter=1, mip_filter=0;
    std::uint32_t wrap_w=3, border_color=0, max_mip=0, max_anisotropy=1;
    float lod_bias=0;
    bool cube=false;
    std::uint32_t render_target=0;
    std::array<std::uint32_t,6> face_targets{};
    std::shared_ptr<const TexturePixels> pixels;
};
struct Command {
    enum class Kind { Clear, Draw, Copy, Readback } kind=Kind::Clear;
    Target target;
    std::uint32_t source=0, source_height=0;
    bool read_depth=false;
    std::vector<Rect> destination_rects;
    std::shared_ptr<std::promise<std::vector<std::uint32_t>>> readback;
    Pipeline pipeline;
    std::uint32_t flags=0, color=0, stencil=0;
    float z=1;
    std::vector<Rect> rects;
    std::uint32_t topology=4; // GL_POINTS / LINES / TRIANGLES (+ strips)
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Texture texture; // legacy FVF test path
    std::array<Texture,4> textures;
    std::shared_ptr<const ShaderPair> shaders;
    std::array<std::array<float,4>,192> vertex_constants{};
    std::vector<std::uint8_t> draw_constants;
    // Optional float offset in the opaque uniform payload. Pixel W-depth slope
    // is in guest pixels and must follow the physical render-target scale.
    std::uint32_t depth_slope_offset=~0u;
};
struct Submission {
    std::uint64_t frame=0, draws=0, unsupported=0;
    std::uint64_t supported=0, rejected_draws=0, draw_ns=0, max_draw_ns=0, decode_ns=0, queue_wait_ns=0;
    std::uint64_t texture_check_ns=0, assemble_ns=0;
    std::uint32_t front=0;
    bool present=true;
    std::uint32_t reference_width=0, reference_height=0;
    std::vector<std::uint32_t> reference_pixels;
    std::vector<Command> commands;
};
// Bounded FIFO: preserve incremental frames, clear/draw ordering and guest buffer lifetimes.
// Stop releases a producer blocked by a closing window.
void Submit(Submission frame);
bool Take(Submission& frame);
void Stop();
void Fail(std::string reason);
std::string Failure();
} // namespace pgr::gldirect
