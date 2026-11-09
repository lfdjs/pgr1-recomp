#pragma once
// API-neutral, owned representation of the Vulkan 04B pre-transformed geometry subset.
// A bridge from PGR1's existing gldirect::Submission is deliberately separate from Vulkan.
#include <array>
#include <cstdint>
#include <vector>

namespace pgr::vk {
struct Vertex {
    float x=0, y=0, z=0, rhw=1;
    float r=1, g=1, b=1, a=1;
};
struct Batch {
    std::uint32_t index_start=0, index_count=0;
};
struct Frame {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Batch> batches;
    std::array<float,4> clear{0.02f,0.02f,0.03f,1.0f};
    std::uint64_t input_draws=0, accepted_draws=0, rejected_programs=0, rejected_state=0, rejected_geometry=0;
};
// Build a deterministic Vulkan fixture with multiple independent batches, depth layers
// and indexed triangles. This proves pipeline/upload/recording on a real ICD; NOT a PGR1 scene.
Frame Fixture(std::uint64_t frame_index, std::uint32_t width, std::uint32_t height);
}
