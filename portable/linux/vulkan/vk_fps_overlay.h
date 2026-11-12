#pragma once
#include <vulkan/vulkan.h>
namespace pgr::vulkan {
// Must be called inside the final color render pass, after postprocessing.
// A fixed bitmap font uses attachment clears: no texture, descriptor or upload.
void RecordFpsOverlay(VkCommandBuffer command,VkExtent2D extent,const char* text);
}
