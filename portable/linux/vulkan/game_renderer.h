#pragma once
#include "../gldirect/commands.h"
#include <memory>
struct SDL_Window;
namespace pgr::vulkan {
// Vulkan objects live on the consumer thread. A null window selects offscreen
// execution for integration tests; it never counts as a swapchain presentation.
class Renderer {
public:
    struct Stats {
        struct GpuSample { std::uint64_t frame=0; double milliseconds=0; };
        double fence_ms=0, acquire_ms=0, execute_ms=0, present_ms=0, gpu_ms=0;
        double shader_ms=0, cpu_ms=0;
        double presented_fps=0,presented_frame_ms=0;
        std::uint64_t accepted=0, rejected=0, indexed=0, nonindexed=0, gpu_frame=0;
        std::uint64_t staging_allocations=0, staging_reuses=0, descriptor_creates=0,
            descriptor_resets=0, framebuffer_hits=0, framebuffer_creates=0;
        // Dynamic reflection-cube cache diagnostics, per submitted batch.
        std::uint64_t cube_cache_creates=0, cube_cache_hits=0, cube_face_copies=0;
        std::uint64_t render_passes=0, batched_groups=0, batched_draws=0;
        // EN: Vulkan draw-state calls avoided inside unchanged render passes.
        // PT-BR: Comandos de estado Vulkan poupados no mesmo render pass.
        std::uint64_t pipeline_bind_skips=0, viewport_set_skips=0, blend_set_skips=0;
        // EN: Work and reusable resources per submission, including hidden GPU prefixes.
        // PT-BR: Trabalho/recursos por submissao, incluindo prefixos sem apresentacao.
        std::uint64_t visibility_queries=0, visibility_query_pool_creates=0,
            visibility_query_pool_reuses=0, visibility_deferred_submissions=0;
        std::uint64_t readback_requests=0, readback_bytes=0, readback_buffer_creates=0,
            readback_buffer_reuses=0, submissions=0, nonpresent_submissions=0,
            pipeline_creates=0;
        double readback_resolve_ms=0;
        bool presented=false;
        bool acquire_timeout=false;
        std::vector<GpuSample> gpu_samples;
    };
    explicit Renderer(SDL_Window* window);
    ~Renderer();
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    bool Render(gldirect::Submission& frame);
    // EN: Nonblocking fence check on the owning Vulkan thread; resolves the
    // producer completion token even when there is no subsequent submission.
    // PT-BR: Consulta sem bloqueio que conclui promessas mesmo sem novo frame.
    bool PollCompletion();
    void Resize();
    void SetAspect(float aspect);
    // Captures the same display-space FXAA/sharpen pipeline, including offscreen tests.
    struct DisplayPixels { unsigned width=0,height=0; std::vector<std::uint32_t> pixels; };
    DisplayPixels ReadDisplay();
    bool Capture(const char* path);
    void Compare(const gldirect::Submission& frame);
    // Drain the final submitted work and publish its completed timestamp samples.
    void Finish();
    const Stats& LastStats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
