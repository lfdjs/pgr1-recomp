// Compile-time/runtime contract tests against the *real* SDK headers.
// No game bytes are used and there is no graphical device requirement.
#include "cpu/x86_context.h"
#include "gpu/backend.h"
#include "kernel/memory.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>

static_assert(sizeof(X86Xmm) == 16, "Guest SSE register layout changed");
static_assert(alignof(X86Context) >= 64, "Guest CPU context alignment changed");
static_assert(sizeof(decltype(X86Context::eax)) == 4, "Guest register must be 32-bit");
static_assert(sizeof(decltype(X86FuncMapping::guest)) == 4, "Guest function VA must be 32-bit");
static_assert(std::is_abstract_v<gpu::Backend>, "The render backend must remain an interface");
static_assert(mem::kPage == 0x1000, "Xbox page size unexpectedly changed");
static_assert(mem::kPhysicalSize == 64u * 1024u * 1024u, "Console memory model unexpectedly changed");

class ContractBackend final : public gpu::Backend {
public:
    bool Init(std::uint32_t, std::uint32_t) override { return true; }
    void Clear(const gpu::State&, std::uint32_t, const std::uint8_t*,
               std::uint32_t, std::uint32_t, float, std::uint32_t) override { ++clears; }
    void Draw(const gpu::State&, std::uint32_t, std::uint32_t, std::uint32_t,
              const std::uint8_t*) override { ++draws; }
    void Copy(const gpu::Surface&, const gpu::Surface&, std::uint32_t,
              std::uint32_t, std::uint32_t) override { ++copies; }
    void Present(const gpu::Surface&) override { ++frames; }
    void Sync(std::uint32_t, std::uint32_t) override {}
    void SyncLate(std::uint32_t) override {}
    void Flush() override { ++flushes; }
    void SetGamma(const std::uint8_t*) override {}
    int clears = 0, draws = 0, copies = 0, frames = 0, flushes = 0;
};

int main() {
    ContractBackend backend;
    gpu::State state{};
    gpu::Surface target{};
    assert(backend.Init(640, 480));
    backend.Clear(state, 0, nullptr, gpu::kClearTarget, 0, 1.0f, 0);
    backend.Draw(state, 0, 0, 3, nullptr);
    backend.Copy(target, target, 0, 0, 0);
    backend.Present(target);
    backend.Flush();
    assert(backend.clears == 1 && backend.draws == 1);
    assert(backend.copies == 1 && backend.frames == 1 && backend.flushes == 1);
    const std::uint32_t fence = backend.Fence();
    assert(fence == 1 && !backend.FencePending(fence));
    std::cout << "SDK contracts: CPU layout / memory constants / GPU Backend OK\n";
}
