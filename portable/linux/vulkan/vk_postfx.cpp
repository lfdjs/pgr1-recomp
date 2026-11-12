#include "vk_postfx.h"
#include "vk_fps_overlay.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace pgr::vulkan {
namespace {
void Check(VkResult r,const char* what) {
    if(r!=VK_SUCCESS) throw std::runtime_error(std::string("Vulkan postfx ")+what+": "+std::to_string(r));
}
std::vector<uint32_t> ReadSpv(const char* path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error(std::string("Vulkan postfx shader missing: ")+path);
    const std::streamoff bytes=file.tellg();
    if(bytes<20 || bytes%4)throw std::runtime_error("Invalid Vulkan postfx SPIR-V size");
    std::vector<uint32_t> binary(size_t(bytes)/4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(binary.data()),std::streamsize(bytes));
    if(!file || binary[0]!=0x07230203)throw std::runtime_error("Invalid Vulkan postfx SPIR-V header");
    return binary;
}
struct Push { float rect[4],inv[2],sharpen,fxaa; };
static_assert(sizeof(Push)==32);
}
PostFx::PostFx(VkDevice device,VkFormat format,VkExtent2D extent,
               const std::vector<VkImage>& images,const char* vert,const char* frag,
               VkSampleCountFlagBits samples,VkImageLayout final_layout)
    :device_(device),extent_(extent) {
    try {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding=0; binding.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount=1; binding.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.bindingCount=1; dl.pBindings=&binding;
        Check(vkCreateDescriptorSetLayout(device_,&dl,nullptr,&descriptor_layout_),"descriptor layout");
        VkPushConstantRange push{};push.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;push.size=sizeof(Push);
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount=1;pl.pSetLayouts=&descriptor_layout_;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
        Check(vkCreatePipelineLayout(device_,&pl,nullptr,&pipeline_layout_),"pipeline layout");
        VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sc.minFilter=sc.magFilter=VK_FILTER_LINEAR;sc.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sc.minLod=sc.maxLod=0; sc.maxAnisotropy=1;
        Check(vkCreateSampler(device_,&sc,nullptr,&sampler_),"sampler");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
        VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pc.maxSets=1;pc.poolSizeCount=1;pc.pPoolSizes=&size;
        Check(vkCreateDescriptorPool(device_,&pc,nullptr,&pool_),"descriptor pool");
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        da.descriptorPool=pool_;da.descriptorSetCount=1;da.pSetLayouts=&descriptor_layout_;
        Check(vkAllocateDescriptorSets(device_,&da,&descriptor_),"descriptor set");
        VkAttachmentDescription attachment{};
        attachment.format=format;attachment.samples=samples;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=final_layout;
        VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount=1;sub.pColorAttachments=&color;
        VkSubpassDependency dependency{};
        dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;
        dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount=1;rp.pAttachments=&attachment;rp.subpassCount=1;rp.pSubpasses=&sub;
        rp.dependencyCount=1;rp.pDependencies=&dependency;
        Check(vkCreateRenderPass(device_,&rp,nullptr,&pass_),"render pass");
        std::array<VkShaderModule,2> modules{};
        try {
            const char* paths[]={vert,frag};
            for(unsigned i=0;i<2;i++) {
                const auto binary=ReadSpv(paths[i]);
                VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                create.codeSize=binary.size()*sizeof(uint32_t);create.pCode=binary.data();
                Check(vkCreateShaderModule(device_,&create,nullptr,&modules[i]),"shader module");
            }
            VkPipelineShaderStageCreateInfo stages[2]{};
            for(unsigned i=0;i<2;i++) {
                stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
                stages[i].module=modules[i];stages[i].pName="main";
            }
            VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkViewport viewport{0,0,float(extent.width),float(extent.height),0,1};
            VkRect2D scissor{{0,0},extent};
            VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            vs.viewportCount=1;vs.pViewports=&viewport;vs.scissorCount=1;vs.pScissors=&scissor;
            VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;
            rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;rs.lineWidth=1.0f;
            VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            ms.rasterizationSamples=samples;
            VkPipelineColorBlendAttachmentState cb{};
            cb.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo cs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            cs.attachmentCount=1;cs.pAttachments=&cb;
            VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            gp.stageCount=2;gp.pStages=stages;gp.pVertexInputState=&vi;gp.pInputAssemblyState=&ia;
            gp.pViewportState=&vs;gp.pRasterizationState=&rs;gp.pMultisampleState=&ms;
            gp.pColorBlendState=&cs;gp.layout=pipeline_layout_;gp.renderPass=pass_;
            Check(vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&gp,nullptr,&pipeline_),"pipeline");
        }catch(...) {
            for(auto module:modules)if(module)vkDestroyShaderModule(device_,module,nullptr);
            throw;
        }
        for(auto module:modules)vkDestroyShaderModule(device_,module,nullptr);
        views_.reserve(images.size());framebuffers_.reserve(images.size());
        for(auto image:images) {
            VkImageViewCreateInfo iv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            iv.image=image;iv.viewType=VK_IMAGE_VIEW_TYPE_2D;iv.format=format;
            iv.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            VkImageView view=VK_NULL_HANDLE;
            Check(vkCreateImageView(device_,&iv,nullptr,&view),"swapchain view");
            views_.push_back(view);
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass=pass_;fb.attachmentCount=1;fb.pAttachments=&views_.back();
            fb.width=extent.width;fb.height=extent.height;fb.layers=1;
            VkFramebuffer framebuffer=VK_NULL_HANDLE;
            Check(vkCreateFramebuffer(device_,&fb,nullptr,&framebuffer),"framebuffer");
            framebuffers_.push_back(framebuffer);
        }
    }catch(...) { Cleanup(); throw; }
}
PostFx::~PostFx() { Cleanup(); }
void PostFx::Cleanup() noexcept {
    for(auto fb:framebuffers_)vkDestroyFramebuffer(device_,fb,nullptr);
    framebuffers_.clear();
    for(auto view:views_)vkDestroyImageView(device_,view,nullptr);
    views_.clear();
    if(pipeline_)vkDestroyPipeline(device_,pipeline_,nullptr);
    if(pass_)vkDestroyRenderPass(device_,pass_,nullptr);
    if(pool_)vkDestroyDescriptorPool(device_,pool_,nullptr);
    if(sampler_)vkDestroySampler(device_,sampler_,nullptr);
    if(pipeline_layout_)vkDestroyPipelineLayout(device_,pipeline_layout_,nullptr);
    if(descriptor_layout_)vkDestroyDescriptorSetLayout(device_,descriptor_layout_,nullptr);
    pipeline_=VK_NULL_HANDLE; pass_=VK_NULL_HANDLE; pool_=VK_NULL_HANDLE; sampler_=VK_NULL_HANDLE;
    pipeline_layout_=VK_NULL_HANDLE; descriptor_layout_=VK_NULL_HANDLE;
}
void PostFx::Record(VkCommandBuffer command,VkImage source,VkImageView source_view,
                    unsigned source_w,unsigned source_h,float aspect,unsigned sharpen,bool fxaa,unsigned index,const char* overlay) {
    if(!source_w||!source_h || index>=framebuffers_.size())throw std::runtime_error("PostFx invalid render extent/index");
    VkImageMemoryBarrier source_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    source_barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    source_barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    source_barrier.oldLayout=source_barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    source_barrier.srcQueueFamilyIndex=source_barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    source_barrier.image=source;source_barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&source_barrier);
    VkDescriptorImageInfo image{sampler_,source_view,VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet=descriptor_;write.dstBinding=0;write.descriptorCount=1;
    write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&image;
    vkUpdateDescriptorSets(device_,1,&write,0,nullptr);
    VkClearValue clear{};clear.color.float32[3]=1.0f;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass=pass_;begin.framebuffer=framebuffers_[index];
    begin.renderArea={{0,0},extent_};begin.clearValueCount=1;begin.pClearValues=&clear;
    vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_layout_,0,1,&descriptor_,0,nullptr);
    double w=extent_.width,h=extent_.height;
    if(aspect<=0)aspect=double(source_w)/source_h;
    if(w/h>aspect)w=h*aspect;else h=w/aspect;
    Push push{};push.rect[0]=float((extent_.width-w)*.5);push.rect[1]=float((extent_.height-h)*.5);
    push.rect[2]=float(w);push.rect[3]=float(h);
    push.inv[0]=1.0f/source_w;push.inv[1]=1.0f/source_h;
    push.sharpen=std::min(100u,sharpen)*.01f;push.fxaa=fxaa?1.f:0.f;
    vkCmdPushConstants(command,pipeline_layout_,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),&push);
    vkCmdDraw(command,3,1,0,0);
    if(overlay && *overlay)RecordFpsOverlay(command,extent_,overlay);
    vkCmdEndRenderPass(command);
}
}
