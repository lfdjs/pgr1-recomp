#pragma once
#include "vk_guest_frame.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace pgr::vk {
// First native Vulkan indexed-draw path. Graphics calls stay on the owning SDL thread.
// The caller waits for the previous submission's fence BEFORE calling Record.
class Geometry {
public:
    Geometry(VkPhysicalDevice physical, VkDevice logical);
    Geometry(const Geometry&)=delete;
    Geometry& operator=(const Geometry&)=delete;
    ~Geometry();
    // Resizing and format changes require vkDeviceWaitIdle before this call.
    void Prepare(VkFormat format, VkExtent2D extent, const std::vector<VkImage>& images);
    void Record(VkCommandBuffer cb, std::uint32_t index, const Frame& frame);
private:
    VkPhysicalDevice physical_;
    VkDevice device_;
    VkRenderPass render_pass_=VK_NULL_HANDLE;
    VkPipelineLayout layout_=VK_NULL_HANDLE;
    VkPipeline pipeline_=VK_NULL_HANDLE;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    VkFormat format_=VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    VkBuffer upload_=VK_NULL_HANDLE;
    VkDeviceMemory memory_=VK_NULL_HANDLE;
    void* mapped_=nullptr;
    VkDeviceSize capacity_=16ull*1024*1024;
    void ClearSwapchain();
    void InitBuffers();
    VkShaderModule Shader(const char* name);
};
}
