#pragma once
#include <cstdint>

namespace pgr::gldirect {
// Diagnostic GPU frame, produced on the game thread, copied to SDL's main thread.
// No raw guest pointers or GPU handles cross this boundary.
struct Frame {
    std::uint64_t number = 0;
    std::uint64_t total_draws = 0;
    std::uint64_t total_clears = 0;
    std::uint32_t guest_width = 640;
    std::uint32_t guest_height = 480;
    std::uint32_t last_color = 0xFF101929u; // Xbox D3DCOLOR ARGB
    bool had_color_clear = false;
};
void Publish(const Frame& frame) noexcept;
// Returns false until the first Present. Calls may skip intermediary frames.
bool Latest(Frame& out) noexcept;
} // namespace pgr::gldirect
