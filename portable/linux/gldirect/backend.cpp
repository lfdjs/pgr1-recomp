// Etapa 03C.1: SDK GPU backend hooked directly to SDL3/OpenGL process.
// Deliberately diagnostic: records title clears/presents but DOES NOT draw guest geometry.
// This never calls OpenGL from a guest/game thread.
#include <cstdint>
#include <memory>
#include "gpu/backend.h"
#include "gpu/texture.h"
#include "kernel/kernel.h"
#include "core/log.h"
#include "harness.h"
#include "bridge.h"

namespace gpu {
namespace {
class DiagnosticGLBackend final : public Backend {
public:
    bool Init(std::uint32_t w, std::uint32_t h) override {
        w_ = w; h_ = h;
        LOGR("gldirect: diagnostic OpenGL bridge %ux%u (guest Draw not implemented)", w, h);
        return true;
    }
    void Clear(const State&, std::uint32_t, const std::uint8_t*, std::uint32_t flags,
               std::uint32_t color, float, std::uint32_t) override {
        ++clears_;
        if (flags & 0xF0u) { last_clear_ = color; had_color_clear_ = true; }
        // No guest render-target or scissor emulation in this first milestone.
    }
    void Draw(const State&, std::uint32_t, std::uint32_t, std::uint32_t,
              const std::uint8_t*) override { ++draws_; }
    void Copy(const Surface& src, const Surface& dst, std::uint32_t n,
              std::uint32_t rects, std::uint32_t points) override {
        // Preserve the existing host-memory copy path needed by some XDK operations.
        CopySurfaceMemory(src, dst, n, rects, points);
    }
    void Present(const Surface&) override {
        ++frames_;
        harness::g_frames.store(frames_, std::memory_order_relaxed);
        pgr::gldirect::Publish({frames_, draws_, clears_, w_, h_, last_clear_, had_color_clear_});
        if (frames_ % 60 == 0)
            LOGI("gldirect: frame %llu, %llu guest draws skipped, %llu clears, vblank %llu",
                 static_cast<unsigned long long>(frames_),
                 static_cast<unsigned long long>(draws_),
                 static_cast<unsigned long long>(clears_),
                 static_cast<unsigned long long>(harness::g_vblanks.load()));
    }
    void Sync(std::uint32_t, std::uint32_t) override {}
    void SyncLate(std::uint32_t) override {}
    void Flush() override {}
    void SetGamma(const std::uint8_t*) override {}
    void GetReport(std::uint32_t report) override {
        // Match the headless harness's zero-visibility convention.
        kernel::W32(report + 0, static_cast<std::uint32_t>(frames_));
        kernel::W32(report + 4, 0);
        kernel::W32(report + 8, 0);
        kernel::W32(report + 12, 0);
    }
private:
    std::uint64_t frames_ = 0, draws_ = 0, clears_ = 0;
    std::uint32_t w_ = 640, h_ = 480, last_clear_ = 0xFF101929u;
    bool had_color_clear_ = false;
};
} // namespace
std::unique_ptr<Backend> CreateBackend() { return std::make_unique<DiagnosticGLBackend>(); }
std::unique_ptr<Backend> CreateFallbackBackend() { return nullptr; }
} // namespace gpu
