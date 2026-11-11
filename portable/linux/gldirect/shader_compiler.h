#pragma once
#include <string>
#include <array>
#include <future>
namespace pgr::gldirect {
// Compile SDK-generated HLSL, preserving intermediate SPIR-V for other host APIs.
// Throws with the diagnostic path on failure; never replaces a shader with a flat color.
std::string CompileHlsl(const std::string& source, bool vertex);
// Vulkan 1.0 SPIR-V with an explicit, shared descriptor/input ABI. Binary bytes
// are returned in an owning string; no SPIRV-Cross or GLSL conversion occurs.
// Set 0: VC b0=0, DC b1=1, textures t0..3=2..5, samplers s0..3=6..9.
// Vertex attributes use location=NV2A register; vertex Y is inverted for a
// positive-height Vulkan viewport. The SDK's [0,1] clip-space depth is retained.
std::string CompileHlslSpirv(const std::string& source, bool vertex);
std::shared_future<std::array<std::string,2>> CompileHlslPairSpirvAsync(std::string vertex, std::string fragment);
}
