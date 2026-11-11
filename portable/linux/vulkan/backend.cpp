// Reuse the proven SDK state capture, primitive conversion, constants and
// immutable texture snapshots. Only shader target and image orientation differ.
// This source never constructs an OpenGL context or calls OpenGL.
#ifndef PGR1_NATIVE_VULKAN
#define PGR1_NATIVE_VULKAN 1
#endif
#include "../gldirect/backend.cpp"
