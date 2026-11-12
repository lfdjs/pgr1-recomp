#pragma once
// Native Vulkan-only, cross-thread controls: the recompiled title owns the
// menu, while SDL/Vulkan rendering and DirectSound mixing run on other threads.
#include <atomic>
#include <cstdint>
namespace pgr::vulkan::quality {
inline std::atomic<unsigned> render_percent{100};  // 0: original guest size; 25..200: display percent
inline std::atomic<bool> fxaa{true};
inline std::atomic<bool> postfx_supported{true};
inline std::atomic<unsigned> aniso_supported_max{16};
inline std::atomic<unsigned> sharpen_percent{0};
inline std::atomic<bool> sharp_scaling{false};
inline std::atomic<unsigned> msaa{0}; // app enum: 0 off, 1 2x, 2 4x (8x unavailable)
inline std::atomic<unsigned> msaa_supported_mask{1}; // VkSampleCountFlagBits, published by renderer
inline std::atomic<unsigned> msaa_applied{0};
inline std::atomic<bool> show_fps{false};
inline std::atomic<bool> alpha_coverage{false};
inline std::atomic<bool> vsync{true};
inline std::atomic<unsigned> fps_cap{60};
inline std::atomic<unsigned> aspect{0};
inline std::atomic<unsigned> output_width{0}, output_height{0}; // 0: desktop
inline std::atomic<unsigned> display_mode{1}; // app::DisplayMode
inline std::atomic<unsigned> display_revision{0}; // release/acquire publication for SDL thread
inline std::atomic<unsigned> aniso{0};  // 0: original Xbox sampler state
inline std::atomic<unsigned> spatial_percent{100}; // 3D voices only; 100: as game requests
inline std::atomic<bool> reflection_mips{true};
}
