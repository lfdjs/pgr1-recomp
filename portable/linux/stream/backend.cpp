// Stage 03 experimental Linux backend: shares selected CPU-reference frames via mmap.
// This implements the SDK's gpu::Backend contract for the diagnostic harness only.
// It is NOT an accelerated Xbox 3D renderer: Draw still uses reference::Draw.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/texture.h"
#include "harness.h"
#include "kernel/kernel.h"
#include "reference_renderer.h"

#include "frame_ipc.h"

namespace gpu {
namespace {

class ReferenceIpcBackend final : public Backend {
public:
    bool Init(std::uint32_t w, std::uint32_t h) override {
        LOGI("renderer: PGR1 Linux diagnostic stream %ux%u (reference CPU, shared memory)", w, h);
        if (const char* p = std::getenv("PGR1_FRAME_FILE"); p && *p) {
            if (!file_.Open(p, true)) {
                LOGE("renderer: cannot open PGR1_FRAME_FILE: %s", p);
                return false;
            }
        } else {
            LOGE("renderer: PGR1_FRAME_FILE missing");
            return false;
        }
        std::filesystem::create_directories(app::ExeDir() / "cap");
        return true;
    }

    void Clear(const State& s, std::uint32_t count, const std::uint8_t* rects,
               std::uint32_t flags, std::uint32_t color, float z, std::uint32_t stencil) override {
        if (drawing_) reference::Clear(s, count, rects, flags, color, z, stencil);
    }

    void Draw(const State& s, std::uint32_t prim, std::uint32_t start, std::uint32_t count,
              const std::uint8_t* indices) override {
        ++draws_;
        if (drawing_) reference::Draw(s, prim, start, count, indices);
    }

    void Copy(const Surface& src, const Surface& dst, std::uint32_t count,
              std::uint32_t rects, std::uint32_t points) override {
        if (drawing_) reference::Copy(src, dst, count, rects, points);
        else CopySurfaceMemory(src, dst, count, rects, points);
    }

    void Present(const Surface& front) override {
        if (drawing_) {
            TextureDesc desc;
            if (!DescribeSurface(front, desc) || Format(desc.format).host != Host::BGRA8) {
                if (!warned_) {
                    LOGE("renderer: frame surface is not BGRA8; streaming unsupported, check format");
                    warned_ = true;
                }
            } else if (desc.width > pgr::stream::kMaxWidth || desc.height > pgr::stream::kMaxHeight) {
                if (!warned_) {
                    LOGE("renderer: frame %ux%u exceeds IPC limit %ux%u", desc.width, desc.height,
                         pgr::stream::kMaxWidth, pgr::stream::kMaxHeight);
                    warned_ = true;
                }
            } else {
                std::uint32_t pitch = 0;
                std::vector<std::uint8_t> bgra;
                DecodeLevel(desc, 0, 0, static_cast<const std::uint8_t*>(kernel::HostPtr(desc.data)),
                            nullptr, bgra, pitch);
                if (pitch >= desc.width * 4 && bgra.size() >= std::size_t(pitch) * desc.height) {
                    if (file_.Publish(desc.width, desc.height, frames_ + 1, bgra.data(), pitch)) {
                        ++published_;
                    }
                } else if (!warned_) {
                    LOGE("renderer: decoded frame buffer has invalid row stride or size");
                    warned_ = true;
                }
            }
        }
        ++frames_;
        harness::g_frames = frames_;
        const long interval = std::max(1L, harness::Env("PGR1_REF", 30));
        drawing_ = ((frames_ + 1) % std::uint64_t(interval)) == 0;
        if (drawing_) reference::BeginFrame();
        if (frames_ % 60 == 0)
            LOGI("renderer: frame %llu, %llu draws, %llu streamed, vblank %llu",
                 static_cast<unsigned long long>(frames_),
                 static_cast<unsigned long long>(draws_),
                 static_cast<unsigned long long>(published_),
                 static_cast<unsigned long long>(harness::g_vblanks.load()));
    }

    void Sync(std::uint32_t, std::uint32_t) override {}
    void SyncLate(std::uint32_t) override {}
    void Flush() override {}
    void SetGamma(const std::uint8_t*) override {}
    void GetReport(std::uint32_t report) override {
        // Preserve the same zero-pixel visibility report convention as the SDK null backend.
        kernel::W32(report + 0, static_cast<std::uint32_t>(frames_));
        kernel::W32(report + 4, 0);
        kernel::W32(report + 8, 0);
        kernel::W32(report + 12, 0);
    }

private:
    pgr::stream::File file_;
    bool drawing_ = false;
    bool warned_ = false;
    std::uint64_t frames_ = 0, draws_ = 0, published_ = 0;
};

}  // namespace

std::unique_ptr<Backend> CreateBackend() { return std::make_unique<ReferenceIpcBackend>(); }
std::unique_ptr<Backend> CreateFallbackBackend() { return nullptr; }

}  // namespace gpu
