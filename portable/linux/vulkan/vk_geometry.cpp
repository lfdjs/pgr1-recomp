#include "vk_geometry.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

#ifndef PGR1_VK_SHADER_DIR
#define PGR1_VK_SHADER_DIR "."
#endif
namespace pgr::vk {
namespace {
void Check(VkResult r,const char* where) {
    if(r!=VK_SUCCESS) throw std::runtime_error(std::string(where)+": VkResult="+std::to_string(int(r)));
}
}
Geometry::Geometry(VkPhysicalDevice physical,VkDevice logical): physical_(physical),device_(logical) {
    InitBuffers();
}
Geometry::~Geometry() {
    ClearSwapchain();
    if(mapped_) vkUnmapMemory(device_,memory_);
    if(upload_) vkDestroyBuffer(device_,upload_,nullptr);
    if(memory_) vkFreeMemory(device_,memory_,nullptr);
}
void Geometry::ClearSwapchain() {
    for(auto f:framebuffers_) vkDestroyFramebuffer(device_,f,nullptr);
    framebuffers_.clear();
    if(pipeline_) vkDestroyPipeline(device_,pipeline_,nullptr);
    pipeline_=VK_NULL_HANDLE;
    if(layout_) vkDestroyPipelineLayout(device_,layout_,nullptr);
    layout_=VK_NULL_HANDLE;
    if(render_pass_) vkDestroyRenderPass(device_,render_pass_,nullptr);
    render_pass_=VK_NULL_HANDLE;
    for(auto v:views_) vkDestroyImageView(device_,v,nullptr);
    views_.clear();
}
void Geometry::InitBuffers() {
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size=capacity_;bc.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bc.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    Check(vkCreateBuffer(device_,&bc,nullptr,&upload_),"vkCreateBuffer");
    VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device_,upload_,&req);
    VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(physical_,&props);
    const auto wanted=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    std::uint32_t idx=std::numeric_limits<std::uint32_t>::max();
    for(std::uint32_t i=0;i<props.memoryTypeCount;i++) if((req.memoryTypeBits&(1u<<i)) &&
        (props.memoryTypes[i].propertyFlags&wanted)==wanted) {idx=i;break;}
    if(idx==std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Vulkan device has no HOST_VISIBLE|HOST_COHERENT upload memory");
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize=req.size;ai.memoryTypeIndex=idx;
    Check(vkAllocateMemory(device_,&ai,nullptr,&memory_),"vkAllocateMemory");
    Check(vkBindBufferMemory(device_,upload_,memory_,0),"vkBindBufferMemory");
    Check(vkMapMemory(device_,memory_,0,capacity_,0,&mapped_),"vkMapMemory");
}
VkShaderModule Geometry::Shader(const char* name) {
    const auto path=std::string(PGR1_VK_SHADER_DIR)+"/"+name;
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("Cannot open Vulkan shader: "+path);
    std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>{f},{});
    if(bytes.empty() || bytes.size()%4) throw std::runtime_error("Invalid SPIR-V length: "+path);
    std::vector<std::uint32_t> aligned(bytes.size()/4);
    std::memcpy(aligned.data(),bytes.data(),bytes.size());
    if(aligned.front()!=0x07230203) throw std::runtime_error("Invalid SPIR-V magic: "+path);
    VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create.codeSize=bytes.size();create.pCode=aligned.data();
    VkShaderModule result=VK_NULL_HANDLE;
    Check(vkCreateShaderModule(device_,&create,nullptr,&result),"vkCreateShaderModule");
    return result;
}
void Geometry::Prepare(VkFormat format,VkExtent2D extent,const std::vector<VkImage>& images) {
    ClearSwapchain();
    if(!extent.width || !extent.height || images.empty()) throw std::runtime_error("Invalid Vulkan swapchain extent/images");
    format_=format;extent_=extent;
    VkAttachmentDescription attachment{};
    attachment.format=format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
    VkSubpassDependency depend{};depend.srcSubpass=VK_SUBPASS_EXTERNAL;depend.dstSubpass=0;
    depend.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    depend.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    depend.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpc{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpc.attachmentCount=1;rpc.pAttachments=&attachment;rpc.subpassCount=1;rpc.pSubpasses=&subpass;
    rpc.dependencyCount=1;rpc.pDependencies=&depend;
    Check(vkCreateRenderPass(device_,&rpc,nullptr,&render_pass_),"vkCreateRenderPass");
    for(auto image:images) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;
        vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount=1;vi.subresourceRange.layerCount=1;
        VkImageView view=VK_NULL_HANDLE;Check(vkCreateImageView(device_,&vi,nullptr,&view),"vkCreateImageView");
        views_.push_back(view);
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass=render_pass_;fi.attachmentCount=1;fi.pAttachments=&views_.back();
        fi.width=extent.width;fi.height=extent.height;fi.layers=1;
        VkFramebuffer framebuffer=VK_NULL_HANDLE;
        Check(vkCreateFramebuffer(device_,&fi,nullptr,&framebuffer),"vkCreateFramebuffer");
        framebuffers_.push_back(framebuffer);
    }
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT,0,4*sizeof(float)};
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.pushConstantRangeCount=1;lc.pPushConstantRanges=&push;
    Check(vkCreatePipelineLayout(device_,&lc,nullptr,&layout_),"vkCreatePipelineLayout");
    VkShaderModule vertex=Shader("xyzw_color.vert.spv"),fragment=VK_NULL_HANDLE;
    try {fragment=Shader("xyzw_color.frag.spv");}
    catch(...) {vkDestroyShaderModule(device_,vertex,nullptr);throw;}
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[0].pName="main";
    stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;stages[1].pName="main";
    VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription,2> attrs{{
        {0,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,x)},
        {1,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,r)}
    }};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount=1;input.pVertexBindingDescriptions=&binding;
    input.vertexAttributeDescriptionCount=attrs.size();input.pVertexAttributeDescriptions=attrs.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount=1;vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;
    raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState color_blend{};
    color_blend.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount=1;blend.pAttachments=&color_blend;
    const VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount=2;dynamic.pDynamicStates=states;
    VkGraphicsPipelineCreateInfo pc{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pc.stageCount=2;pc.pStages=stages;pc.pVertexInputState=&input;pc.pInputAssemblyState=&assembly;
    pc.pViewportState=&vp;pc.pRasterizationState=&raster;pc.pMultisampleState=&multisample;
    pc.pColorBlendState=&blend;pc.pDynamicState=&dynamic;
    pc.layout=layout_;pc.renderPass=render_pass_;pc.subpass=0;
    const auto status=vkCreateGraphicsPipelines(device_,VK_NULL_HANDLE,1,&pc,nullptr,&pipeline_);
    vkDestroyShaderModule(device_,vertex,nullptr);vkDestroyShaderModule(device_,fragment,nullptr);
    Check(status,"vkCreateGraphicsPipelines");
}
void Geometry::Record(VkCommandBuffer cb,std::uint32_t image_index,const Frame& frame) {
    if(image_index>=framebuffers_.size()) throw std::out_of_range("Invalid Vulkan image index");
    const auto vb=frame.vertices.size()*sizeof(Vertex),ib=frame.indices.size()*sizeof(std::uint32_t);
    if(vb+ib>capacity_) throw std::runtime_error("Vulkan test upload exceeds 16 MiB; reject rather than overflow");
    if(vb) std::memcpy(mapped_,frame.vertices.data(),vb);
    if(ib) std::memcpy(static_cast<std::uint8_t*>(mapped_)+vb,frame.indices.data(),ib);
    VkClearValue clear{};
    for(unsigned i=0;i<4;i++) clear.color.float32[i]=frame.clear[i];
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass=render_pass_;begin.framebuffer=framebuffers_[image_index];
    begin.renderArea.extent=extent_;begin.clearValueCount=1;begin.pClearValues=&clear;
    vkCmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{};vp.width=float(extent_.width);vp.height=float(extent_.height);vp.maxDepth=1;
    vkCmdSetViewport(cb,0,1,&vp);
    VkRect2D sc{};sc.extent=extent_;vkCmdSetScissor(cb,0,1,&sc);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_);
    VkDeviceSize offset=0;vkCmdBindVertexBuffers(cb,0,1,&upload_,&offset);
    if(ib) vkCmdBindIndexBuffer(cb,upload_,vb,VK_INDEX_TYPE_UINT32);
    const float viewport[4]={float(extent_.width),float(extent_.height),0,0};
    vkCmdPushConstants(cb,layout_,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(viewport),viewport);
    for(const auto& b:frame.batches) {
        if(std::size_t(b.index_start)+b.index_count>frame.indices.size())
            throw std::runtime_error("Vulkan draw index bounds invalid");
        if(b.index_count) vkCmdDrawIndexed(cb,b.index_count,1,b.index_start,0,0);
    }
    vkCmdEndRenderPass(cb);
}
}
