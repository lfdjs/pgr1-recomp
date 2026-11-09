#pragma once
#include "vk_guest_frame.h"
namespace pgr::gldirect { struct Submission; }
namespace pgr::vk {
// Parses the ALREADY OWNED, API-neutral SDK command stream produced by gldirect.
// 04B supports only simple pretransformed 2D colored triangles to the front buffer.
// Shader programs, texture stages, offscreen passes, alpha, depth, and partial clears
// are deliberately rejected. The caller must not use this as a complete game renderer.
Frame FromSubmission(const gldirect::Submission& commands, unsigned guest_width, unsigned guest_height);
}
