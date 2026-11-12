#pragma once
#include <vulkan/vulkan.h>
#include <vector>
namespace pgr::vulkan {
// A Vulkan 1.0 fullscreen triangle into swapchain images. The guest scene
// remains unchanged; this is a display-space FXAA/sharpen pass only.
class PostFx final {
public:
    PostFx(VkDevice device,VkFormat format,VkExtent2D extent,
           const std::vector<VkImage>& images,const char* vertex_spv,const char* fragment_spv,
           VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT,
           VkImageLayout final_layout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    ~PostFx();
    PostFx(const PostFx&)=delete;
    PostFx& operator=(const PostFx&)=delete;
    void Record(VkCommandBuffer command,VkImage source,VkImageView source_view,
                unsigned source_w,unsigned source_h,float aspect,unsigned sharpen,bool fxaa,unsigned index,
                const char* overlay=nullptr);
private:
    void Cleanup() noexcept;
    VkDevice device_;
    VkExtent2D extent_;
    VkDescriptorSetLayout descriptor_layout_=VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_=VK_NULL_HANDLE;
    VkRenderPass pass_=VK_NULL_HANDLE;
    VkPipeline pipeline_=VK_NULL_HANDLE;
    VkDescriptorPool pool_=VK_NULL_HANDLE;
    VkDescriptorSet descriptor_=VK_NULL_HANDLE;
    VkSampler sampler_=VK_NULL_HANDLE;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
};
}
