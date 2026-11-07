#include "bridge.h"
#include <mutex>
namespace pgr::gldirect {
namespace { std::mutex g_mutex; Frame g_last{}; bool g_valid = false; }
void Publish(const Frame& frame) noexcept {
    std::lock_guard<std::mutex> guard(g_mutex);
    g_last = frame;
    g_valid = true;
}
bool Latest(Frame& out) noexcept {
    std::lock_guard<std::mutex> guard(g_mutex);
    if (!g_valid) return false;
    out = g_last;
    return true;
}
} // namespace pgr::gldirect
