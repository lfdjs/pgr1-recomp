#pragma once
#include "commands.h"
#include <memory>
namespace pgr::gldirect {
// All methods, including destruction, require the owning thread's GL 3.3 context.
class Renderer {
public:
    using Proc = void (*)();
    using Loader = Proc (*)(const char*);
    explicit Renderer(Loader loader);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    bool Init();
    bool Execute(const Submission& frame);
    bool Present(int width, int height);
    void SetAspect(float aspect);
    struct GpuTime { std::uint64_t frame, nanoseconds; };
    std::vector<GpuTime> PollGpu();
    bool Capture(const char* path);
    void Compare(const Submission& frame);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
