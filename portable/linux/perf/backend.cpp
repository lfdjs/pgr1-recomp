// PGR1 Linux Stage 03B: instrumented CPU-reference renderer.
// The Xbox GPU operations STILL run through reference::Draw on the CPU.
// This file is linked into a separate profiling harness, never the known-good headless binary.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/texture.h"
#include "harness.h"
#include "kernel/kernel.h"
#include "reference_renderer.h"
#include "../stream/frame_ipc.h"

namespace gpu {
namespace {
using Clock = std::chrono::steady_clock;
using Ns = std::chrono::nanoseconds;

std::uint64_t ElapsedNs(Clock::time_point before) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<Ns>(Clock::now() - before).count());
}

std::uint64_t PositiveEnv(const char* key, std::uint64_t fallback) {
    const char* raw = std::getenv(key);
    if (!raw || !*raw || *raw == '-') return fallback;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(raw, &end, 10);
    return end != raw && *end == '\0' && v > 0 ? v : fallback;
}

class ProfileBackend final : public Backend {
public:
    ~ProfileBackend() override {
        if (csv_) std::fclose(csv_);
    }

    bool Init(std::uint32_t width, std::uint32_t height) override {
        LOGI("renderer: PGR1 profiling reference backend %ux%u", width, height);
        const char* frame_file = std::getenv("PGR1_FRAME_FILE");
        if (!frame_file || !*frame_file || !frame_.Open(frame_file, true)) {
            LOGE("perf: unable to open PGR1_FRAME_FILE for mmap IPC");
            return false;
        }
        const auto path = app::ExeDir() / "pgr1_perf.csv";
        csv_ = std::fopen(path.string().c_str(), "w");
        if (!csv_) {
            LOGE("perf: unable to create %s", path.string().c_str());
            return false;
        }
        std::fputs("capture_frame,game_draws,reference_draw_ms,reference_clear_ms,reference_copy_ms,"
                   "decode_ms,ipc_ms,sampled_wall_ms,interval,next_interval\n", csv_);
        std::fflush(csv_);
        interval_ = std::clamp<std::uint64_t>(PositiveEnv("PGR1_REF", 60), 1, 5000);
        minimum_ = std::clamp<std::uint64_t>(PositiveEnv("PGR1_REF_MIN", 30), 1, 5000);
        maximum_ = std::clamp<std::uint64_t>(PositiveEnv("PGR1_REF_MAX", 600), minimum_, 5000);
        budget_ms_ = double(std::clamp<std::uint64_t>(PositiveEnv("PGR1_REF_BUDGET_MS", 8), 1, 10000));
        adaptive_ = std::string(std::getenv("PGR1_REF_ADAPT") ? std::getenv("PGR1_REF_ADAPT") : "1") != "0";
        LOGI("perf: initial interval %llu frames, adaptive=%s, target CPU cost %.0f ms/frame",
             static_cast<unsigned long long>(interval_), adaptive_ ? "yes" : "no", budget_ms_);
        next_capture_ = interval_;
        return true;
    }

    void Clear(const State& state, std::uint32_t count, const std::uint8_t* rects,
               std::uint32_t flags, std::uint32_t color, float depth, std::uint32_t stencil) override {
        if (!drawing_) return;
        const auto start = Clock::now();
        reference::Clear(state, count, rects, flags, color, depth, stencil);
        clear_ns_ += ElapsedNs(start);
    }

    void Draw(const State& state, std::uint32_t primitive, std::uint32_t first,
              std::uint32_t count, const std::uint8_t* indices) override {
        ++game_draws_;
        if (!drawing_) return;
        const auto start = Clock::now();
        reference::Draw(state, primitive, first, count, indices);
        draw_ns_ += ElapsedNs(start);
    }

    void Copy(const Surface& from, const Surface& to, std::uint32_t count,
              std::uint32_t rects, std::uint32_t points) override {
        if (drawing_) {
            const auto start = Clock::now();
            reference::Copy(from, to, count, rects, points);
            copy_ns_ += ElapsedNs(start);
        } else {
            CopySurfaceMemory(from, to, count, rects, points);
        }
    }

    void Present(const Surface& front) override {
        const auto frame_no = frames_ + 1;
        if (drawing_) {
            std::uint64_t decode_ns = 0, ipc_ns = 0;
            TextureDesc desc;
            if (DescribeSurface(front, desc) && Format(desc.format).host == Host::BGRA8 &&
                desc.width <= pgr::stream::kMaxWidth && desc.height <= pgr::stream::kMaxHeight) {
                std::uint32_t pitch = 0;
                std::vector<std::uint8_t> bgra;
                auto start = Clock::now();
                DecodeLevel(desc, 0, 0,
                            static_cast<const std::uint8_t*>(kernel::HostPtr(desc.data)),
                            nullptr, bgra, pitch);
                decode_ns = ElapsedNs(start);
                if (pitch >= desc.width * 4 && bgra.size() >= std::size_t(pitch) * desc.height) {
                    start = Clock::now();
                    if (frame_.Publish(desc.width, desc.height, frame_no, bgra.data(), pitch)) ++published_;
                    ipc_ns = ElapsedNs(start);
                } else if (!warned_) {
                    LOGE("perf: invalid source surface pitch/size");
                    warned_ = true;
                }
            } else if (!warned_) {
                LOGE("perf: unsupported front-buffer surface for BGRA8 stream");
                warned_ = true;
            }

            const double wall = double(ElapsedNs(start_sample_)) / 1e6;
            const double draw = double(draw_ns_) / 1e6;
            const double clear = double(clear_ns_) / 1e6;
            const double copy = double(copy_ns_) / 1e6;
            const double decode = double(decode_ns) / 1e6;
            const double ipc = double(ipc_ns) / 1e6;
            const auto used_interval = interval_;
            const double sampled_cpu_ms = draw + clear + copy + decode + ipc;
            if (adaptive_) {
                // Avoid a single expensive reference frame imposing its full cost on every game frame.
                // This does NOT remove the stall within the individual sampled frame.
                const auto suggested = static_cast<std::uint64_t>(
                    std::clamp(std::ceil(sampled_cpu_ms / budget_ms_),
                               double(minimum_), double(maximum_)));
                interval_ = std::clamp<std::uint64_t>((interval_ + suggested + 1) / 2,
                                                       minimum_, maximum_);
            }
            const auto next_interval = interval_;
            if (csv_) {
                std::fprintf(csv_, "%llu,%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%llu\n",
                             static_cast<unsigned long long>(frame_no),
                             static_cast<unsigned long long>(game_draws_),
                             draw, clear, copy, decode, ipc, wall,
                             static_cast<unsigned long long>(used_interval),
                             static_cast<unsigned long long>(next_interval));
                std::fflush(csv_);
            }
            LOGI("perf: capture frame %llu: reference %.1f ms (draw %.1f, clear %.1f), "
                 "wall %.1f ms, next interval %llu",
                 static_cast<unsigned long long>(frame_no), sampled_cpu_ms,
                 draw, clear, wall, static_cast<unsigned long long>(next_interval));
            next_capture_ = frame_no + interval_;
        }

        frames_ = frame_no;
        harness::g_frames = frames_;
        drawing_ = (frames_ + 1 >= next_capture_);
        if (drawing_) {
            draw_ns_ = clear_ns_ = copy_ns_ = 0;
            game_draws_ = 0;
            start_sample_ = Clock::now();
            reference::BeginFrame();
        }
        if (frames_ % 60 == 0) {
            LOGI("renderer: frame %llu, %llu streamed, vblank %llu",
                 static_cast<unsigned long long>(frames_),
                 static_cast<unsigned long long>(published_),
                 static_cast<unsigned long long>(harness::g_vblanks.load()));
        }
    }

    void Sync(std::uint32_t, std::uint32_t) override {}
    void SyncLate(std::uint32_t) override {}
    void Flush() override {}
    void SetGamma(const std::uint8_t*) override {}
    void GetReport(std::uint32_t report) override {
        kernel::W32(report, static_cast<std::uint32_t>(frames_));
        kernel::W32(report + 4, 0);
        kernel::W32(report + 8, 0);
        kernel::W32(report + 12, 0);
    }

private:
    pgr::stream::File frame_;
    std::FILE* csv_ = nullptr;
    bool drawing_ = false, warned_ = false, adaptive_ = true;
    std::uint64_t frames_ = 0, published_ = 0;
    std::uint64_t interval_ = 60, minimum_ = 30, maximum_ = 600, next_capture_ = 60;
    std::uint64_t game_draws_ = 0, draw_ns_ = 0, clear_ns_ = 0, copy_ns_ = 0;
    double budget_ms_ = 8;
    Clock::time_point start_sample_{};
};
} // namespace

std::unique_ptr<Backend> CreateBackend() { return std::make_unique<ProfileBackend>(); }
std::unique_ptr<Backend> CreateFallbackBackend() { return nullptr; }
} // namespace gpu
