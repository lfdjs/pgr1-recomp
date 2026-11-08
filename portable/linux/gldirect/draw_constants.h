#pragma once
#include "gpu/shaders.h"
namespace pgr::gldirect {
// Portable packing for the SDK's existing VC/DC HLSL uniform blocks.
gpu::DrawConstants Constants(const gpu::State& state, unsigned width, unsigned height, bool program);
}
