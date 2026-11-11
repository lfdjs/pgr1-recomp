#include "game_renderer.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace pgr::vulkan {
using namespace gldirect;
namespace {
using Clock=std::chrono::steady_clock;
double Millis(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
void Check(VkResult r,const char* what) {
    if(r!=VK_SUCCESS) throw std::runtime_error(std::string(what)+" VkResult="+std::to_string(r));
}
float Channel(uint32_t c,unsigned shift) { return float((c>>shift)&255)/255; }
VkFormat ColorFormat(unsigned f) {
    switch(f) {
    case 0x06: case 0x07: case 0x12: case 0x1e: return VK_FORMAT_B8G8R8A8_UNORM;
    case 0x05: case 0x11: return VK_FORMAT_R5G6B5_UNORM_PACK16;
    default: throw std::runtime_error("unsupported Vulkan color target format "+std::to_string(f));
    }
}
VkFormat DepthFormat(unsigned f) {
    if(f==0x2c || f==0x30) return VK_FORMAT_D16_UNORM;
    if(f==0x2a || f==0x2e) return VK_FORMAT_D24_UNORM_S8_UINT;
    throw std::runtime_error("unsupported Vulkan depth target format "+std::to_string(f));
}
VkCompareOp CompareOp(unsigned f) {
    if(f<0x200 || f>0x207) throw std::runtime_error("unsupported compare function "+std::to_string(f));
    return VkCompareOp(f-0x200);
}
VkStencilOp Stencil(unsigned f) {
    switch(f) {
    case 0x1e00:return VK_STENCIL_OP_KEEP; case 0:return VK_STENCIL_OP_ZERO;
    case 0x1e01:return VK_STENCIL_OP_REPLACE; case 0x1e02:return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case 0x1e03:return VK_STENCIL_OP_DECREMENT_AND_CLAMP; case 0x150a:return VK_STENCIL_OP_INVERT;
    case 0x8507:return VK_STENCIL_OP_INCREMENT_AND_WRAP; case 0x8508:return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default:throw std::runtime_error("unsupported stencil operation "+std::to_string(f));
    }
}
VkBlendFactor Blend(unsigned f) {
    switch(f) {
    case 0:return VK_BLEND_FACTOR_ZERO; case 1:return VK_BLEND_FACTOR_ONE;
    case 0x300:return VK_BLEND_FACTOR_SRC_COLOR; case 0x301:return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 0x302:return VK_BLEND_FACTOR_SRC_ALPHA; case 0x303:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 0x304:return VK_BLEND_FACTOR_DST_ALPHA; case 0x305:return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 0x306:return VK_BLEND_FACTOR_DST_COLOR; case 0x307:return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 0x308:return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case 0x8001:return VK_BLEND_FACTOR_CONSTANT_COLOR; case 0x8002:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 0x8003:return VK_BLEND_FACTOR_CONSTANT_ALPHA; case 0x8004:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    default:throw std::runtime_error("unsupported blend factor "+std::to_string(f));
    }
}
VkBlendOp BlendOp(unsigned f) {
    switch(f) {
    case 0x8006:return VK_BLEND_OP_ADD; case 0x800a:return VK_BLEND_OP_SUBTRACT;
    case 0x800b:return VK_BLEND_OP_REVERSE_SUBTRACT; case 0x8007:return VK_BLEND_OP_MIN;
    case 0x8008:return VK_BLEND_OP_MAX;
    default:throw std::runtime_error("unsupported blend operation "+std::to_string(f));
    }
}
VkPrimitiveTopology Topology(unsigned f) {
    switch(f) {
    case 0:return VK_PRIMITIVE_TOPOLOGY_POINT_LIST; case 1:return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case 3:return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; case 4:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case 5:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    default:throw std::runtime_error("unsupported assembled topology "+std::to_string(f));
    }
}
VkRect2D Clip(Rect r,unsigned w,unsigned h) {
    int x=std::clamp(r.x,0,int(w)),y=std::clamp(r.y,0,int(h));
    return {{x,y},{unsigned(std::max(0,std::min(int(w),r.x+std::max(0,r.w))-x)),
                     unsigned(std::max(0,std::min(int(h),r.y+std::max(0,r.h))-y))}};
}
template<class T> void Key(std::string& k,const T& v) { k.append(reinterpret_cast<const char*>(&v),sizeof(v)); }
}
struct Renderer::Impl {
    SDL_Window* window;
    VkInstance instance=VK_NULL_HANDLE;
    VkSurfaceKHR surface=VK_NULL_HANDLE;
    VkPhysicalDevice gpu=VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    uint32_t family=0,timestamp_bits=0;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandBuffer cmd=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    bool pending=false,resize=true;
    float output_aspect=0;
    std::shared_ptr<std::promise<void>> pending_completion;
    VkSemaphore acquired=VK_NULL_HANDLE;
    VkSwapchainKHR swapchain=VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat swap_format=VK_FORMAT_UNDEFINED;
    std::vector<VkImage> swap_images;
    std::vector<VkSemaphore> finished;
    VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache=VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;
    VkQueryPool queries=VK_NULL_HANDLE;
    VkQueryPool occlusion_pool=VK_NULL_HANDLE;
    struct Counted { std::shared_ptr<std::promise<uint64_t>> promise; uint32_t index; };
    std::vector<Counted> counted;
    uint64_t query_frame=0;
    Stats stats;
    struct Buffer {
        VkDevice device; VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
        void* mapped=nullptr; VkDeviceSize size=0;
        explicit Buffer(VkDevice d):device(d){}
        ~Buffer(){ if(mapped) vkUnmapMemory(device,memory); if(buffer) vkDestroyBuffer(device,buffer,nullptr); if(memory) vkFreeMemory(device,memory,nullptr); }
    };
    struct Image {
        VkDevice device; VkImage image=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
        VkImageView view=VK_NULL_HANDLE,sample=VK_NULL_HANDLE;
        VkFormat format; unsigned w,h,levels,layers; VkImageAspectFlags aspect;
        explicit Image(VkDevice d):device(d){}
        ~Image(){ if(sample && sample!=view) vkDestroyImageView(device,sample,nullptr); if(view) vkDestroyImageView(device,view,nullptr);
            if(image) vkDestroyImage(device,image,nullptr); if(memory) vkFreeMemory(device,memory,nullptr); }
    };
    using ImagePtr=std::shared_ptr<Image>;
    std::map<uint32_t,ImagePtr> colors,depths;
    std::map<std::weak_ptr<const TexturePixels>,ImagePtr,std::owner_less<std::weak_ptr<const TexturePixels>>> textures;
    // GPU-reflection cubes share immutable CPU texels but contain faces that the
    // game renders into separate targets. Rebuilding/uploading all six faces on
    // every car draw is prohibitively expensive on RADV/Polaris.
    struct DynamicCube {
        ImagePtr image;
        std::array<uint32_t,6> face_targets{};
        std::array<uint64_t,6> copied_versions{};
    };
    std::map<std::weak_ptr<const TexturePixels>,DynamicCube,
        std::owner_less<std::weak_ptr<const TexturePixels>>> dynamic_cubes;
    std::map<uint32_t,uint64_t> color_versions;
    uint64_t color_serial=0;
    ImagePtr white,white_cube,front;
    std::vector<ImagePtr> retained;
    std::vector<std::unique_ptr<Buffer>> buffers;
    std::vector<VkFramebuffer> framebuffers;
    std::map<std::pair<VkImageView,VkImageView>,VkFramebuffer> framebuffer_cache;
    std::map<std::pair<VkFormat,VkFormat>,VkRenderPass> passes;
    std::map<std::string,VkPipeline> pipelines;
    std::map<std::shared_ptr<const ShaderPair>,std::array<VkShaderModule,2>,std::owner_less<std::shared_ptr<const ShaderPair>>> modules;
    std::map<std::string,VkSampler> samplers;
    std::set<std::string> warnings;
    struct Read { std::shared_ptr<std::promise<std::vector<uint32_t>>> promise; Buffer* buffer; ImagePtr image; VkDeviceSize stencil_offset; };
    std::vector<Read> reads;
    struct Offset { VkDeviceSize vertex=0,index=0,vc=0,dc=0; };
    std::vector<Offset> offsets;
    Buffer* staged=nullptr;
    explicit Impl(SDL_Window* w):window(w){}
    ~Impl() {
        if(device) vkDeviceWaitIdle(device);
        ReleaseFrame(); front.reset(); white.reset(); white_cube.reset(); textures.clear(); colors.clear(); depths.clear();
        if(device) {
            for(auto& [k,p]:pipelines) vkDestroyPipeline(device,p,nullptr);
            for(auto& [k,p]:modules) for(auto m:p) vkDestroyShaderModule(device,m,nullptr);
            for(auto& [k,p]:passes) vkDestroyRenderPass(device,p,nullptr);
            for(auto& [k,s]:samplers) vkDestroySampler(device,s,nullptr);
            if(queries) vkDestroyQueryPool(device,queries,nullptr);
            if(descriptor_pool) vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
            if(pipeline_cache) vkDestroyPipelineCache(device,pipeline_cache,nullptr);
            if(layout) vkDestroyPipelineLayout(device,layout,nullptr);
            if(descriptors) vkDestroyDescriptorSetLayout(device,descriptors,nullptr);
            for(auto s:finished) vkDestroySemaphore(device,s,nullptr);
            if(acquired) vkDestroySemaphore(device,acquired,nullptr);
            if(fence) vkDestroyFence(device,fence,nullptr);
            if(pool) vkDestroyCommandPool(device,pool,nullptr);
            if(swapchain) vkDestroySwapchainKHR(device,swapchain,nullptr);
            vkDestroyDevice(device,nullptr);
        }
        if(surface) SDL_Vulkan_DestroySurface(instance,surface,nullptr);
        if(instance) vkDestroyInstance(instance,nullptr);
    }
    void Init() {
        Uint32 count=0; const char* const* extensions=nullptr;
        if(window) { extensions=SDL_Vulkan_GetInstanceExtensions(&count); if(!extensions) throw std::runtime_error(SDL_GetError()); }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName="PGR1 real runtime native Vulkan"; app.apiVersion=VK_API_VERSION_1_0;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; create.pApplicationInfo=&app;
        create.enabledExtensionCount=count; create.ppEnabledExtensionNames=extensions;
        Check(vkCreateInstance(&create,nullptr,&instance),"vkCreateInstance");
        if(window && !SDL_Vulkan_CreateSurface(window,instance,nullptr,&surface)) throw std::runtime_error(SDL_GetError());
        uint32_t n=0; Check(vkEnumeratePhysicalDevices(instance,&n,nullptr),"physical device count");
        std::vector<VkPhysicalDevice> devices(n); Check(vkEnumeratePhysicalDevices(instance,&n,devices.data()),"physical devices");
        int best=-1; const char* requested=std::getenv("PGR1_VK_DEVICE");
        for(auto candidate:devices) {
            VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(candidate,&p);
            std::string name=p.deviceName,match=requested?requested:"";
            for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
            for(auto& c:match)c=char(std::tolower(static_cast<unsigned char>(c)));
            if(!match.empty() && name.find(match)==std::string::npos) continue;
            if(surface) {
                uint32_t ne=0; Check(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&ne,nullptr),"extension count");
                std::vector<VkExtensionProperties> ex(ne); Check(vkEnumerateDeviceExtensionProperties(candidate,nullptr,&ne,ex.data()),"device extensions");
                if(std::none_of(ex.begin(),ex.end(),[](auto& e){ return !strcmp(e.extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME); }))continue;
            }
            uint32_t nf=0; vkGetPhysicalDeviceQueueFamilyProperties(candidate,&nf,nullptr);
            std::vector<VkQueueFamilyProperties> families(nf); vkGetPhysicalDeviceQueueFamilyProperties(candidate,&nf,families.data());
            for(unsigned i=0;i<nf;i++) {
                VkBool32 present=VK_TRUE; if(surface) Check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,surface,&present),"surface support");
                if(!(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT) || !present) continue;
                if(surface) {
                    uint32_t formats=0,modes=0;
                    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate,surface,&formats,nullptr),"surface formats");
                    Check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate,surface,&modes,nullptr),"surface modes");
                    if(!formats || !modes)continue;
                }
                int score=p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?2:1;
                if(score>best) { best=score; gpu=candidate; family=i; properties=p; timestamp_bits=families[i].timestampValidBits; }
            }
        }
        if(!gpu)throw std::runtime_error(std::string("No compatible Vulkan graphics/present GPU for PGR1_VK_DEVICE=")+(requested?requested:"auto"));
        VkPhysicalDeviceFeatures available{}; vkGetPhysicalDeviceFeatures(gpu,&available);
        features.fillModeNonSolid=available.fillModeNonSolid;
        features.samplerAnisotropy=available.samplerAnisotropy;
        features.shaderClipDistance=available.shaderClipDistance;
        features.largePoints=available.largePoints;
        features.occlusionQueryPrecise=available.occlusionQueryPrecise;
        float priority=1; VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qc.queueFamilyIndex=family; qc.queueCount=1; qc.pQueuePriorities=&priority;
        const char* swap_extension=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dc.queueCreateInfoCount=1; dc.pQueueCreateInfos=&qc; dc.pEnabledFeatures=&features;
        if(surface) { dc.enabledExtensionCount=1; dc.ppEnabledExtensionNames=&swap_extension; }
        Check(vkCreateDevice(gpu,&dc,nullptr,&device),"vkCreateDevice"); vkGetDeviceQueue(device,family,0,&queue);
        std::printf("PGR1 native Vulkan GAME backend: %s vendor=0x%x api=%u.%u.%u; Vulkan 1.0, extension=%s\n",properties.deviceName,properties.vendorID,
            VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),VK_VERSION_PATCH(properties.apiVersion),surface?swap_extension:"none (offscreen test)");
        std::printf("Vulkan enabled queried features: fillModeNonSolid=%u samplerAnisotropy=%u shaderClipDistance=%u largePoints=%u; timestamps=%u\n",
            features.fillModeNonSolid,features.samplerAnisotropy,features.shaderClipDistance,features.largePoints,timestamp_bits);
        std::printf("Vulkan precise occlusion queries: %s\n",features.occlusionQueryPrecise?"enabled":"unavailable; counted draws explicitly fail");
        std::fprintf(stderr,"Vulkan 04C limitation: targets start cleared; CPU-written target contents are not imported. Partial retained-surface rendering after CPU edits needs 04D resource invalidation.\n");
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pc.queueFamilyIndex=family; pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Check(vkCreateCommandPool(device,&pc,nullptr,&pool),"command pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool=pool; ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount=1;
        Check(vkAllocateCommandBuffers(device,&ca,&cmd),"command buffer");
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fc.flags=VK_FENCE_CREATE_SIGNALED_BIT; Check(vkCreateFence(device,&fc,nullptr,&fence),"fence");
        if(surface) { VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; Check(vkCreateSemaphore(device,&sc,nullptr,&acquired),"acquire semaphore"); }
        std::array<VkDescriptorSetLayoutBinding,10> bindings{};
        for(unsigned i=0;i<10;i++) { bindings[i].binding=i; bindings[i].descriptorCount=1; bindings[i].stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i<6?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER; }
        VkDescriptorSetLayoutCreateInfo ds{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; ds.bindingCount=bindings.size(); ds.pBindings=bindings.data();
        Check(vkCreateDescriptorSetLayout(device,&ds,nullptr,&descriptors),"descriptor layout");
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pl.setLayoutCount=1; pl.pSetLayouts=&descriptors;
        Check(vkCreatePipelineLayout(device,&pl,nullptr,&layout),"pipeline layout");
        VkPipelineCacheCreateInfo cache{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO}; Check(vkCreatePipelineCache(device,&cache,nullptr,&pipeline_cache),"pipeline cache");
        if(timestamp_bits) { VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; query.queryType=VK_QUERY_TYPE_TIMESTAMP; query.queryCount=2;
            Check(vkCreateQueryPool(device,&query,nullptr,&queries),"timestamp queries"); }
    }
    uint32_t MemoryType(uint32_t mask,VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties mem{}; vkGetPhysicalDeviceMemoryProperties(gpu,&mem);
        for(unsigned i=0;i<mem.memoryTypeCount;i++) if((mask&(1u<<i)) && (mem.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw std::runtime_error("Vulkan memory type unavailable for flags="+std::to_string(flags));
    }
    VkFormat DepthStorage(unsigned guest_format) {
        const auto wanted=DepthFormat(guest_format);
        VkFormatProperties fp{}; vkGetPhysicalDeviceFormatProperties(gpu,wanted,&fp);
        if(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)return wanted;
        if(wanted==VK_FORMAT_D24_UNORM_S8_UINT) {
            vkGetPhysicalDeviceFormatProperties(gpu,VK_FORMAT_D32_SFLOAT_S8_UINT,&fp);
            if(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                if(warnings.insert("D24 storage").second)std::fprintf(stderr,
                    "Vulkan: D24S8 storage unavailable; using queried D32_SFLOAT_S8_UINT, CPU readback quantizes to Xbox D24S8. Multipass depth parity requires visual comparison.\n");
                return VK_FORMAT_D32_SFLOAT_S8_UINT;
            }
        }
        throw std::runtime_error("no supported Vulkan storage for Xbox depth format "+std::to_string(guest_format));
    }
    Buffer* NewBuffer(VkDeviceSize size,VkBufferUsageFlags usage,const void* data=nullptr) {
        auto b=std::make_unique<Buffer>(device); b->size=std::max<VkDeviceSize>(size,16);
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size=b->size; ci.usage=usage; ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        Check(vkCreateBuffer(device,&ci,nullptr,&b->buffer),"vkCreateBuffer");
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device,b->buffer,&requirements);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=requirements.size;
        alloc.memoryTypeIndex=MemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        Check(vkAllocateMemory(device,&alloc,nullptr,&b->memory),"host buffer vkAllocateMemory");
        Check(vkBindBufferMemory(device,b->buffer,b->memory,0),"bind buffer");
        Check(vkMapMemory(device,b->memory,0,VK_WHOLE_SIZE,0,&b->mapped),"map host buffer");
        if(data && size)std::memcpy(b->mapped,data,size);
        auto* result=b.get(); buffers.push_back(std::move(b)); return result;
    }
    void Barrier() {
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);
    }
    ImagePtr NewImage(unsigned w,unsigned h,VkFormat format,unsigned levels=1,unsigned layers=1) {
        if(!w || !h || w>properties.limits.maxImageDimension2D || h>properties.limits.maxImageDimension2D) throw std::runtime_error("Vulkan image extent out of device limits");
        auto image=std::make_shared<Image>(device); image->w=w; image->h=h; image->format=format; image->levels=levels; image->layers=layers;
        const bool depth=format==VK_FORMAT_D16_UNORM || format==VK_FORMAT_D24_UNORM_S8_UINT || format==VK_FORMAT_D32_SFLOAT_S8_UINT;
        image->aspect=depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
        if(format==VK_FORMAT_D24_UNORM_S8_UINT || format==VK_FORMAT_D32_SFLOAT_S8_UINT)image->aspect|=VK_IMAGE_ASPECT_STENCIL_BIT;
        VkFormatProperties fp{}; vkGetPhysicalDeviceFormatProperties(gpu,format,&fp);
        const auto required=depth?VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if((fp.optimalTilingFeatures&required)!=required)throw std::runtime_error("Vulkan required attachment/sample format unsupported: "+std::to_string(format));
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ci.imageType=VK_IMAGE_TYPE_2D; ci.format=format; ci.extent={w,h,1}; ci.mipLevels=levels; ci.arrayLayers=layers;
        ci.samples=VK_SAMPLE_COUNT_1_BIT; ci.tiling=VK_IMAGE_TILING_OPTIMAL; ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        ci.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
        if(layers==6)ci.flags=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        Check(vkCreateImage(device,&ci,nullptr,&image->image),"vkCreateImage");
        VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device,image->image,&requirements);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=requirements.size; alloc.memoryTypeIndex=MemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(device,&alloc,nullptr,&image->memory),"image vkAllocateMemory"); Check(vkBindImageMemory(device,image->image,image->memory,0),"bind image");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=image->image; view.viewType=layers==6?VK_IMAGE_VIEW_TYPE_CUBE:VK_IMAGE_VIEW_TYPE_2D; view.format=format;
        view.subresourceRange={image->aspect,0,levels,0,layers}; Check(vkCreateImageView(device,&view,nullptr,&image->view),"image view");
        image->sample=image->view;
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.image=image->image; b.subresourceRange=view.subresourceRange; b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
        if(depth) { VkClearDepthStencilValue value{1,0}; vkCmdClearDepthStencilImage(cmd,image->image,VK_IMAGE_LAYOUT_GENERAL,&value,1,&view.subresourceRange); }
        else { VkClearColorValue value{}; vkCmdClearColorImage(cmd,image->image,VK_IMAGE_LAYOUT_GENERAL,&value,1,&view.subresourceRange); }
        Barrier(); retained.push_back(image); return image;
    }
    ImagePtr TargetImage(const Target& target,bool depth=false) {
        auto& table=depth?depths:colors; const auto address=depth?target.depth:target.color;
        if(!address)throw std::runtime_error("missing Vulkan target guest address");
        auto& image=table[address]; const auto format=depth?DepthStorage(target.depth_format):ColorFormat(target.format);
        if(!image || image->w!=target.width || image->h!=target.height || image->format!=format) {
            if(image)retained.push_back(image);
            // Opaque guest formats are lowered by the SDK's kTexOpaque shader
            // flag. Keep the storage/view neutral when Xbox reinterprets ARGB.
            image=NewImage(target.width,target.height,format);
            // Reused Xbox addresses can refer to newly allocated GPU images.
            // A new source invalidates the corresponding reflection face.
            if(!depth) color_versions[address]=++color_serial;
        }
        return image;
    }
    VkRenderPass Pass(VkFormat color,VkFormat depth) {
        auto key=std::make_pair(color,depth); auto it=passes.find(key); if(it!=passes.end())return it->second;
        std::array<VkAttachmentDescription,2> a{}; a[0].format=color; a[0].samples=VK_SAMPLE_COUNT_1_BIT;
        a[0].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD; a[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE; a[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; a[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[0].initialLayout=a[0].finalLayout=VK_IMAGE_LAYOUT_GENERAL;
        a[1]=a[0]; a[1].format=depth; a[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD; a[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
        VkAttachmentReference c{0,VK_IMAGE_LAYOUT_GENERAL},d{1,VK_IMAGE_LAYOUT_GENERAL};
        VkSubpassDescription sub{}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount=1; sub.pColorAttachments=&c; if(depth!=VK_FORMAT_UNDEFINED)sub.pDepthStencilAttachment=&d;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL; dependencies[0].dstSubpass=0;
        dependencies[0].srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; dependencies[0].dstStageMask=VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        dependencies[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT; dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass=0; dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL; dependencies[1].srcStageMask=VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT; dependencies[1].dstStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; dependencies[1].dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; info.attachmentCount=depth==VK_FORMAT_UNDEFINED?1:2; info.pAttachments=a.data(); info.subpassCount=1; info.pSubpasses=&sub; info.dependencyCount=2; info.pDependencies=dependencies;
        VkRenderPass pass=VK_NULL_HANDLE; Check(vkCreateRenderPass(device,&info,nullptr,&pass),"render pass"); passes[key]=pass; return pass;
    }
    VkRenderPass BeginTarget(const Command& c) {
        auto color=TargetImage(c.target); ImagePtr depth; if(c.target.depth)depth=TargetImage(c.target,true);
        VkRenderPass pass=Pass(color->format,depth?depth->format:VK_FORMAT_UNDEFINED);
        VkImageView attachments[]={color->view,depth?depth->view:VK_NULL_HANDLE};
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fb.renderPass=pass; fb.attachmentCount=depth?2:1; fb.pAttachments=attachments; fb.width=color->w; fb.height=color->h; fb.layers=1;
        const auto key=std::make_pair(attachments[0],attachments[1]);
        auto found=framebuffer_cache.find(key); VkFramebuffer framebuffer=VK_NULL_HANDLE;
        if(found!=framebuffer_cache.end())framebuffer=found->second;
        else { Check(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer),"framebuffer"); framebuffers.push_back(framebuffer); framebuffer_cache[key]=framebuffer; }
        Barrier(); VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; begin.renderPass=pass; begin.framebuffer=framebuffer; begin.renderArea.extent={color->w,color->h};
        vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE); return pass;
    }
    std::array<VkShaderModule,2>& Modules(const std::shared_ptr<const ShaderPair>& pair) {
        auto it=modules.find(pair); if(it!=modules.end())return it->second;
        if(!pair || !pair->spirv.valid())throw std::runtime_error("draw has no SDK Vulkan shader pair");
        auto start=Clock::now(); const auto& code=pair->spirv.get(); stats.shader_ms+=Millis(start);
        std::array<VkShaderModule,2> output{};
        try { for(unsigned i=0;i<2;i++) {
            if(code[i].size()<20 || code[i].size()%4)throw std::runtime_error("invalid SDK SPIR-V binary");
            std::vector<uint32_t> aligned(code[i].size()/4); std::memcpy(aligned.data(),code[i].data(),code[i].size());
            VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize=code[i].size(); ci.pCode=aligned.data();
            Check(vkCreateShaderModule(device,&ci,nullptr,&output[i]),"SDK shader module");
        }} catch(...) { for(auto m:output)if(m)vkDestroyShaderModule(device,m,nullptr); throw; }
        return modules.emplace(pair,output).first->second;
    }
    VkPipeline PipelineFor(const Command& c,VkRenderPass pass) {
        const auto& p=c.pipeline; std::string key; Key(key,pass); Key(key,c.shaders.get()); Key(key,c.topology);
        if(c.visibility && !features.occlusionQueryPrecise)throw std::runtime_error("precise visibility counts unavailable on this GPU");
        if(c.topology==0 || p.fill==0x1b00)throw std::runtime_error("point rasterization requires native PointSize shader lowering");
        if(p.stencil && c.target.depth && DepthFormat(c.target.depth_format)==VK_FORMAT_D16_UNORM)
            throw std::runtime_error("stencil test on a depth-only D16 surface");
        if(p.blend) {
            VkFormatProperties format{}; vkGetPhysicalDeviceFormatProperties(gpu,ColorFormat(c.target.format),&format);
            if(!(format.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT))throw std::runtime_error("target format does not support blending");
        }
        for(auto v:{p.fill,p.depth_func,p.src_blend,p.dst_blend,p.blend_op,p.color_mask,p.cull,p.front,p.stencil_func,p.stencil_fail,p.stencil_zfail,p.stencil_pass,p.stencil_mask,p.stencil_write,p.stencil_ref})Key(key,v);
        for(auto b:{p.depth,p.depth_write,p.blend,p.stencil})Key(key,b); Key(key,p.depth_bias); Key(key,p.depth_slope);
        auto found=pipelines.find(key); if(found!=pipelines.end())return found->second;
        auto& shaders=Modules(c.shaders);
        VkPipelineShaderStageCreateInfo stages[2]{};
        for(unsigned i=0;i<2;i++) { stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT; stages[i].module=shaders[i]; stages[i].pName="main"; }
        VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription,16> attrs{};
        for(unsigned i=0;i<16;i++)attrs[i]={i,0,VK_FORMAT_R32G32B32A32_SFLOAT,uint32_t(offsetof(Vertex,attributes)+i*16)};
        VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vertex.vertexBindingDescriptionCount=1; vertex.pVertexBindingDescriptions=&binding; vertex.vertexAttributeDescriptionCount=attrs.size(); vertex.pVertexAttributeDescriptions=attrs.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology=Topology(c.topology);
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount=viewport.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.lineWidth=1;
        raster.polygonMode=p.fill==0x1b02?VK_POLYGON_MODE_FILL:p.fill==0x1b01?VK_POLYGON_MODE_LINE:VK_POLYGON_MODE_POINT;
        if(raster.polygonMode!=VK_POLYGON_MODE_FILL && !features.fillModeNonSolid)throw std::runtime_error("fillModeNonSolid unavailable");
        // Positive Vulkan viewport + inverted shader Y preserves the guest's
        // clockwise/counterclockwise visual winding in framebuffer coordinates.
        raster.frontFace=p.front==0x901?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;
        raster.cullMode=p.cull?(p.cull==p.front?VK_CULL_MODE_FRONT_BIT:VK_CULL_MODE_BACK_BIT):VK_CULL_MODE_NONE;
        raster.depthBiasEnable=p.depth_bias!=0 || p.depth_slope!=0; raster.depthBiasConstantFactor=p.depth_bias; raster.depthBiasSlopeFactor=p.depth_slope;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable=p.depth; ds.depthWriteEnable=p.depth_write; ds.depthCompareOp=CompareOp(p.depth_func); ds.stencilTestEnable=p.stencil;
        ds.front={Stencil(p.stencil_fail),Stencil(p.stencil_pass),Stencil(p.stencil_zfail),CompareOp(p.stencil_func),p.stencil_mask,p.stencil_write,p.stencil_ref}; ds.back=ds.front;
        VkPipelineColorBlendAttachmentState blend{}; blend.blendEnable=p.blend; blend.srcColorBlendFactor=blend.srcAlphaBlendFactor=Blend(p.src_blend); blend.dstColorBlendFactor=blend.dstAlphaBlendFactor=Blend(p.dst_blend); blend.colorBlendOp=blend.alphaBlendOp=BlendOp(p.blend_op);
        blend.colorWriteMask=(p.color_mask&0x10000?VK_COLOR_COMPONENT_R_BIT:0)|(p.color_mask&0x100?VK_COLOR_COMPONENT_G_BIT:0)|(p.color_mask&1?VK_COLOR_COMPONENT_B_BIT:0)|(p.color_mask&0x1000000?VK_COLOR_COMPONENT_A_BIT:0);
        VkPipelineColorBlendStateCreateInfo bc{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; bc.attachmentCount=1; bc.pAttachments=&blend;
        VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_BLEND_CONSTANTS};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount=3; dynamic.pDynamicStates=states;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; info.stageCount=2; info.pStages=stages; info.pVertexInputState=&vertex; info.pInputAssemblyState=&assembly; info.pViewportState=&viewport; info.pRasterizationState=&raster; info.pMultisampleState=&ms; info.pDepthStencilState=&ds; info.pColorBlendState=&bc; info.pDynamicState=&dynamic; info.layout=layout; info.renderPass=pass;
        VkPipeline result=VK_NULL_HANDLE; auto start=Clock::now(); Check(vkCreateGraphicsPipelines(device,pipeline_cache,1,&info,nullptr,&result),"SDK graphics pipeline"); stats.shader_ms+=Millis(start); pipelines[key]=result; return result;
    }
    VkSampler Sampler(const Texture& t,unsigned levels) {
        std::string key; for(auto v:{t.wrap_u,t.wrap_v,t.wrap_w,t.min_filter,t.mag_filter,t.mip_filter,t.max_mip,t.max_anisotropy,t.border_color,levels})Key(key,v); Key(key,t.lod_bias);
        auto it=samplers.find(key); if(it!=samplers.end())return it->second;
        // Match sdk/runtime/gpu/d3d_common.cpp::Address. Xbox mode 5 is
        // clamp-to-edge here; desktop D3D's numeric MIRROR_ONCE is unrelated.
        auto wrap=[](unsigned v) { switch(v) {case 1:return VK_SAMPLER_ADDRESS_MODE_REPEAT; case 2:return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; case 3:case 5:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; case 4:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            default:throw std::runtime_error("unsupported texture address mode "+std::to_string(v));} };
        VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; ci.magFilter=t.mag_filter<2?VK_FILTER_NEAREST:VK_FILTER_LINEAR; ci.minFilter=t.min_filter<2?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
        ci.mipmapMode=t.mip_filter==2?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.addressModeU=wrap(t.wrap_u); ci.addressModeV=wrap(t.wrap_v); ci.addressModeW=wrap(t.wrap_w);
        ci.mipLodBias=std::clamp(t.lod_bias,-properties.limits.maxSamplerLodBias,properties.limits.maxSamplerLodBias);
        ci.minLod=std::min(float(t.max_mip),float(levels-1)); ci.maxLod=t.mip_filter?float(levels-1):ci.minLod;
        ci.anisotropyEnable=features.samplerAnisotropy && (t.min_filter==3 || t.mag_filter==3); ci.maxAnisotropy=std::clamp(float(t.max_anisotropy),1.0f,properties.limits.maxSamplerAnisotropy);
        if(t.border_color==0)ci.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        else if(t.border_color==0xff000000)ci.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        else if(t.border_color==0xffffffff)ci.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        else if(t.wrap_u==4 || t.wrap_v==4 || t.wrap_w==4)throw std::runtime_error("custom texture border color requires later shader lowering");
        VkSampler result=VK_NULL_HANDLE; Check(vkCreateSampler(device,&ci,nullptr,&result),"sampler"); samplers[key]=result; return result;
    }
    ImagePtr TextureImage(const Texture& t) {
        if(t.render_target) { auto it=colors.find(t.render_target); if(it==colors.end())throw std::runtime_error("sampled GPU target missing"); return it->second; }
        const bool dynamic=std::any_of(t.face_targets.begin(),t.face_targets.end(),[](auto a){return a!=0;});
        if(!t.pixels) {
            auto& fallback=t.cube?white_cube:white;
            if(!fallback) { Texture empty=t; auto px=std::make_shared<TexturePixels>(); px->levels.resize(t.cube?6:1,std::vector<uint32_t>{0xffffffff}); empty.pixels=px; empty.width=empty.height=1; fallback=TextureImage(empty); }
            return fallback;
        }
        auto cached=textures.find(t.pixels); if(!dynamic && cached!=textures.end())return cached->second;
        unsigned layers=t.cube?6:1;
        if(t.pixels->levels.empty() || t.pixels->levels.size()%layers)throw std::runtime_error("incomplete cube/mip snapshot");
        unsigned levels=t.pixels->levels.size()/layers;
        if(dynamic && !t.cube)throw std::runtime_error("dynamic reflection faces require a cubemap");
        DynamicCube* entry=nullptr;
        bool reuse=false;
        if(dynamic) {
            auto [it,inserted]=dynamic_cubes.try_emplace(std::weak_ptr<const TexturePixels>(t.pixels));
            (void)inserted;
            entry=&it->second;
            reuse=entry->image && entry->image->w==t.width && entry->image->h==t.height &&
                  entry->image->levels==levels && entry->face_targets==t.face_targets;
        }
        auto image=reuse?entry->image:NewImage(t.width,t.height,VK_FORMAT_B8G8R8A8_UNORM,levels,layers);
        if(reuse)++stats.cube_cache_hits;
        else {
            // Upload immutable CPU texels only once per reflection cube. GPU
            // faces are refreshed separately, based on actual target writes.
            for(unsigned face=0;face<layers;face++)for(unsigned level=0;level<levels;level++) {
                unsigned w=std::max(1u,t.width>>level),h=std::max(1u,t.height>>level); const auto& pixels=t.pixels->levels[face*levels+level];
                if(pixels.size()!=size_t(w)*h)throw std::runtime_error("texture mip snapshot size mismatch");
                auto* upload=NewBuffer(pixels.size()*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,pixels.data());
                VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,face,1}; copy.imageExtent={w,h,1};
                vkCmdCopyBufferToImage(cmd,upload->buffer,image->image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
            }
            Barrier();
            if(dynamic) {
                entry->image=image;
                entry->face_targets=t.face_targets;
                entry->copied_versions.fill(0);
                ++stats.cube_cache_creates;
            } else textures[t.pixels]=image;
        }
        if(dynamic)for(unsigned face=0;face<6;face++)if(t.face_targets[face]) {
            const uint32_t address=t.face_targets[face];
            auto source=colors.find(address); if(source==colors.end())throw std::runtime_error("cubemap face target missing");
            const uint64_t version=color_versions[address];
            if(!reuse || entry->copied_versions[face]!=version) {
                // Ordered copy only when a real GPU target changed. A later
                // draw in this submission sees the new face; earlier draws
                // remain ordered by Blit's barriers, without extra GPU work.
                Blit(source->second,image,{0,0,int(source->second->w),int(source->second->h)},
                    {0,0,int(image->w),int(image->h)},face);
                entry->copied_versions[face]=version;
                ++stats.cube_face_copies;
            }
        }
        return image;
    }
    void Blit(const ImagePtr& source,const ImagePtr& target,Rect a,Rect b,unsigned layer=0) {
        if(source==target)throw std::runtime_error("GPU resource feedback/copy overlap unsupported");
        auto check=[](Rect r,unsigned w,unsigned h){return r.x>=0 && r.y>=0 && r.w>0 && r.h>0 && uint64_t(r.x)+r.w<=w && uint64_t(r.y)+r.h<=h;};
        if(!check(a,source->w,source->h)||!check(b,target->w,target->h))throw std::runtime_error("CopyRects extent out of range");
        VkFormatProperties sf{},tf{}; vkGetPhysicalDeviceFormatProperties(gpu,source->format,&sf); vkGetPhysicalDeviceFormatProperties(gpu,target->format,&tf);
        if(!(sf.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)||!(tf.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))throw std::runtime_error("image blit format unsupported");
        Barrier(); VkImageBlit copy{}; copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,layer,1};
        copy.srcOffsets[0]={a.x,a.y,0}; copy.srcOffsets[1]={a.x+a.w,a.y+a.h,1}; copy.dstOffsets[0]={b.x,b.y,0}; copy.dstOffsets[1]={b.x+b.w,b.y+b.h,1};
        vkCmdBlitImage(cmd,source->image,VK_IMAGE_LAYOUT_GENERAL,target->image,VK_IMAGE_LAYOUT_GENERAL,1,&copy,VK_FILTER_NEAREST); Barrier();
    }
    VkDescriptorSet Bindings(const Command& c,const Offset& offset) {
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; allocate.descriptorPool=descriptor_pool; allocate.descriptorSetCount=1; allocate.pSetLayouts=&descriptors;
        VkDescriptorSet set=VK_NULL_HANDLE; Check(vkAllocateDescriptorSets(device,&allocate,&set),"allocate draw descriptors");
        VkDescriptorBufferInfo uniform[2]={{staged->buffer,offset.vc,sizeof(c.vertex_constants)},{staged->buffer,offset.dc,c.draw_constants.size()}};
        std::array<VkDescriptorImageInfo,8> images{};
        for(unsigned i=0;i<4;i++) {
            if(c.textures[i].render_target==c.target.color || (c.textures[i].cube && std::find(c.textures[i].face_targets.begin(),c.textures[i].face_targets.end(),c.target.color)!=c.textures[i].face_targets.end()))throw std::runtime_error("render target sampling feedback");
            auto image=TextureImage(c.textures[i]); retained.push_back(image);
            images[i].imageView=image->sample; images[i].imageLayout=VK_IMAGE_LAYOUT_GENERAL; images[i+4].sampler=Sampler(c.textures[i],image->levels);
        }
        std::array<VkWriteDescriptorSet,10> write{};
        for(unsigned i=0;i<10;i++) { write[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; write[i].dstSet=set; write[i].dstBinding=i; write[i].descriptorCount=1;
            write[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:i<6?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER;
            if(i<2)write[i].pBufferInfo=&uniform[i]; else write[i].pImageInfo=&images[i-2]; }
        vkUpdateDescriptorSets(device,write.size(),write.data(),0,nullptr); return set;
    }
    void Stage(const Submission& frame) {
        std::vector<uint8_t> data; offsets.resize(frame.commands.size());
        auto append=[&](const void* p,size_t size,size_t alignment) { size_t start=(data.size()+alignment-1)/alignment*alignment; data.resize(start+std::max(size,size_t(16))); if(size)std::memcpy(data.data()+start,p,size); return VkDeviceSize(start); };
        for(size_t i=0;i<frame.commands.size();i++) { const auto& c=frame.commands[i]; if(c.kind!=Command::Kind::Draw)continue; auto& o=offsets[i];
            o.vertex=append(c.vertices.data(),c.vertices.size()*sizeof(Vertex),16); o.index=append(c.indices.data(),c.indices.size()*4,4);
            o.vc=append(c.vertex_constants.data(),sizeof(c.vertex_constants),properties.limits.minUniformBufferOffsetAlignment);
            o.dc=append(c.draw_constants.data(),c.draw_constants.size(),properties.limits.minUniformBufferOffsetAlignment);
        }
        staged=NewBuffer(data.size(),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,data.data());
        if(descriptor_pool)vkDestroyDescriptorPool(device,descriptor_pool,nullptr); descriptor_pool=VK_NULL_HANDLE;
        unsigned count=std::max(size_t(1),frame.commands.size());
        VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count*2},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,count*4},{VK_DESCRIPTOR_TYPE_SAMPLER,count*4}};
        VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; ci.maxSets=count; ci.poolSizeCount=3; ci.pPoolSizes=sizes;
        Check(vkCreateDescriptorPool(device,&ci,nullptr,&descriptor_pool),"draw descriptor pool");
        const uint32_t query_count=std::count_if(frame.commands.begin(),frame.commands.end(),[](const auto& c){return bool(c.visibility);});
        if(query_count) {
            VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; query.queryType=VK_QUERY_TYPE_OCCLUSION; query.queryCount=query_count;
            Check(vkCreateQueryPool(device,&query,nullptr,&occlusion_pool),"precise occlusion pool");
            vkCmdResetQueryPool(cmd,occlusion_pool,0,query_count);
        }
    }
    void Readback(const ImagePtr& image,std::shared_ptr<std::promise<std::vector<uint32_t>>> promise) {
        VkDeviceSize bytes=VkDeviceSize(image->w)*image->h*(image->format==VK_FORMAT_D16_UNORM || image->format==VK_FORMAT_R5G6B5_UNORM_PACK16?2:4);
        VkDeviceSize stencil_offset=(bytes+3)&~VkDeviceSize(3);
        bool stencil=bool(image->aspect&VK_IMAGE_ASPECT_STENCIL_BIT);
        auto* buffer=NewBuffer(stencil?stencil_offset+VkDeviceSize(image->w)*image->h:bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Barrier(); VkBufferImageCopy copies[2]{}; copies[0].imageSubresource={image->aspect&~VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1}; copies[0].imageExtent={image->w,image->h,1};
        copies[1]=copies[0]; copies[1].bufferOffset=stencil_offset; copies[1].imageSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;
        vkCmdCopyImageToBuffer(cmd,image->image,VK_IMAGE_LAYOUT_GENERAL,buffer->buffer,stencil?2:1,copies);
        reads.push_back({std::move(promise),buffer,image,stencil_offset});
    }
    void ResolveReads() {
        for(auto& r:reads) {
            std::vector<uint32_t> pixels(size_t(r.image->w)*r.image->h);
            if(r.image->format==VK_FORMAT_D16_UNORM) { auto* p=static_cast<const uint16_t*>(r.buffer->mapped); std::copy_n(p,pixels.size(),pixels.begin()); }
            else if(r.image->format==VK_FORMAT_R5G6B5_UNORM_PACK16) { auto* p=static_cast<const uint16_t*>(r.buffer->mapped); for(size_t i=0;i<pixels.size();i++) { uint32_t v=p[i]; pixels[i]=0xff000000|(((v>>11)&31)*255/31<<16)|(((v>>5)&63)*255/63<<8)|((v&31)*255/31); } }
            else if(r.image->format==VK_FORMAT_D24_UNORM_S8_UINT) { auto* p=static_cast<const uint32_t*>(r.buffer->mapped); auto* s=static_cast<const uint8_t*>(r.buffer->mapped)+r.stencil_offset;
                for(size_t i=0;i<pixels.size();i++)pixels[i]=(p[i]<<8)|s[i]; }
            else if(r.image->format==VK_FORMAT_D32_SFLOAT_S8_UINT) { auto* p=static_cast<const float*>(r.buffer->mapped); auto* s=static_cast<const uint8_t*>(r.buffer->mapped)+r.stencil_offset;
                for(size_t i=0;i<pixels.size();i++)pixels[i]=(uint32_t(std::llround(std::clamp(double(p[i]),0.0,1.0)*16777215.0))<<8)|s[i]; }
            else std::memcpy(pixels.data(),r.buffer->mapped,pixels.size()*4);
            r.promise->set_value(std::move(pixels));
        }
        reads.clear();
    }
    void Wait() {
        if(!pending)return; auto start=Clock::now(); Check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"wait GPU fence"); stats.fence_ms+=Millis(start); pending=false;
        if(queries) { uint64_t values[2]{}; Check(vkGetQueryPoolResults(device,queries,0,2,sizeof(values),values,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT),"GPU timestamps");
            uint64_t mask=timestamp_bits==64?~uint64_t(0):(uint64_t(1)<<timestamp_bits)-1; stats.gpu_ms=double((values[1]-values[0])&mask)*properties.limits.timestampPeriod/1e6; stats.gpu_frame=query_frame;
            stats.gpu_samples.push_back({query_frame,stats.gpu_ms}); }
        ResolveReads();
        if(!counted.empty()) {
            std::vector<uint64_t> values(counted.size());
            Check(vkGetQueryPoolResults(device,occlusion_pool,0,values.size(),values.size()*sizeof(uint64_t),values.data(),sizeof(uint64_t),VK_QUERY_RESULT_64_BIT),"precise visibility results");
            for(const auto& result:counted)result.promise->set_value(values[result.index]);
            counted.clear();
        }
        if(pending_completion) { pending_completion->set_value(); pending_completion.reset(); }
    }
    void ReleaseFrame() {
        for(auto f:framebuffers)vkDestroyFramebuffer(device,f,nullptr); framebuffers.clear();
        framebuffer_cache.clear();
        buffers.clear(); retained.clear(); reads.clear();
        if(occlusion_pool)vkDestroyQueryPool(device,occlusion_pool,nullptr);
        occlusion_pool=VK_NULL_HANDLE; counted.clear();
        for(auto it=textures.begin();it!=textures.end();)if(it->first.expired())it=textures.erase(it);else ++it;
        // Do not retain obsolete cubemaps after the producer drops its CPU
        // snapshot. Active snapshots remain cached across game submissions.
        for(auto it=dynamic_cubes.begin();it!=dynamic_cubes.end();)
            if(it->first.expired())it=dynamic_cubes.erase(it);else ++it;
    }
    void Recreate() {
        int w=0,h=0; SDL_GetWindowSizeInPixels(window,&w,&h); if(w<=0 || h<=0)return;
        // Presentation completion has no core fence in Vulkan 1.0. Idle is used
        // only for swapchain retirement and shutdown, never for normal frames.
        Check(vkDeviceWaitIdle(device),"swapchain retirement");
        VkSurfaceCapabilitiesKHR caps{}; Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu,surface,&caps),"surface capabilities");
        if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("swapchain transfer destination unsupported");
        VkExtent2D chosen=caps.currentExtent;
        if(chosen.width==UINT32_MAX)chosen={std::clamp(unsigned(w),caps.minImageExtent.width,caps.maxImageExtent.width),std::clamp(unsigned(h),caps.minImageExtent.height,caps.maxImageExtent.height)};
        if(!chosen.width || !chosen.height)return;
        uint32_t count=0; Check(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu,surface,&count,nullptr),"surface format count"); std::vector<VkSurfaceFormatKHR> formats(count); Check(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu,surface,&count,formats.data()),"surface formats");
        if(formats.empty())throw std::runtime_error("surface has no formats");
        auto selected=formats.front(); if(selected.format==VK_FORMAT_UNDEFINED)selected.format=VK_FORMAT_B8G8R8A8_UNORM;
        for(auto f:formats)if(f.format==VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)selected=f;
        VkFormatProperties fp{}; vkGetPhysicalDeviceFormatProperties(gpu,selected.format,&fp); if(!(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))throw std::runtime_error("swapchain format lacks blit destination");
        VkCompositeAlphaFlagBitsKHR alpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if(!(caps.supportedCompositeAlpha&alpha))for(auto bit:{VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})if(caps.supportedCompositeAlpha&bit){alpha=bit;break;}
        unsigned image_count=caps.minImageCount+1; if(caps.maxImageCount)image_count=std::min(image_count,caps.maxImageCount);
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; ci.surface=surface; ci.minImageCount=image_count; ci.imageFormat=selected.format; ci.imageColorSpace=selected.colorSpace; ci.imageExtent=chosen; ci.imageArrayLayers=1; ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT; ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE; ci.preTransform=caps.currentTransform; ci.compositeAlpha=alpha; ci.presentMode=VK_PRESENT_MODE_FIFO_KHR; ci.clipped=VK_TRUE; ci.oldSwapchain=swapchain;
        VkSwapchainKHR replacement=VK_NULL_HANDLE; Check(vkCreateSwapchainKHR(device,&ci,nullptr,&replacement),"recreate swapchain");
        if(swapchain)vkDestroySwapchainKHR(device,swapchain,nullptr); swapchain=replacement; extent=chosen; swap_format=selected.format;
        for(auto s:finished)vkDestroySemaphore(device,s,nullptr); finished.clear();
        Check(vkGetSwapchainImagesKHR(device,swapchain,&count,nullptr),"swapchain image count"); swap_images.resize(count); Check(vkGetSwapchainImagesKHR(device,swapchain,&count,swap_images.data()),"swapchain images");
        VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; for(unsigned i=0;i<count;i++){ VkSemaphore s=VK_NULL_HANDLE; Check(vkCreateSemaphore(device,&sc,nullptr,&s),"per-image present semaphore"); finished.push_back(s); }
        resize=false; std::printf("Vulkan game swapchain %ux%u images=%u FIFO\n",extent.width,extent.height,count);
    }
    void Begin() {
        Check(vkResetCommandBuffer(cmd,0),"reset commands"); VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; Check(vkBeginCommandBuffer(cmd,&begin),"begin commands");
        if(queries){ vkCmdResetQueryPool(cmd,queries,0,2); vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,0); }
    }
    void Submit(bool present,unsigned image_index,uint64_t frame) {
        if(queries)vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,1);
        Check(vkEndCommandBuffer(cmd),"end commands"); Check(vkResetFences(device,1,&fence),"reset fence");
        VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT; VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO}; info.commandBufferCount=1; info.pCommandBuffers=&cmd;
        if(present){info.waitSemaphoreCount=1; info.pWaitSemaphores=&acquired; info.pWaitDstStageMask=&stage; info.signalSemaphoreCount=1; info.pSignalSemaphores=&finished.at(image_index);}
        Check(vkQueueSubmit(queue,1,&info,fence),"submit native game commands"); pending=true; query_frame=frame;
        if(present) {
            VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount=1; pi.pWaitSemaphores=&finished.at(image_index); pi.swapchainCount=1; pi.pSwapchains=&swapchain; pi.pImageIndices=&image_index;
            auto start=Clock::now(); VkResult r=vkQueuePresentKHR(queue,&pi); stats.present_ms=Millis(start);
            if(r==VK_ERROR_OUT_OF_DATE_KHR || r==VK_SUBOPTIMAL_KHR)resize=true; else Check(r,"present native game image");
            stats.presented=r==VK_SUCCESS || r==VK_SUBOPTIMAL_KHR;
        }
        if(!reads.empty() || !counted.empty())Wait();
    }
    void RecordPresent(unsigned image_index) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.image=swap_images.at(image_index); barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkClearColorValue black{}; vkCmdClearColorImage(cmd,barrier.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&black,1,&barrier.subresourceRange);
        Barrier();
        // Preserve the game's surface aspect. Presentation is a GPU transfer of
        // the actual guest target; there is no CPU rasterizer or fixture here.
        const double aspect=output_aspect>0?output_aspect:double(front->w)/front->h;
        unsigned w=extent.width,h=extent.height;
        if(double(w)/h>aspect)w=std::max(1u,unsigned(std::lround(h*aspect)));
        else h=std::max(1u,unsigned(std::lround(w/aspect)));
        VkImageBlit blit{}; blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; blit.srcOffsets[1]={int(front->w),int(front->h),1}; blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        blit.dstOffsets[0]={int((extent.width-w)/2),int((extent.height-h)/2),0}; blit.dstOffsets[1]={int((extent.width+w)/2),int((extent.height+h)/2),1};
        vkCmdBlitImage(cmd,front->image,VK_IMAGE_LAYOUT_GENERAL,barrier.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=0;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }
    void Reject(const std::string& why,uint64_t frame) {
        ++stats.rejected; if(warnings.insert(why).second)std::fprintf(stderr,"Vulkan frame %llu rejected draw: %s; affected geometry absent (counted in metrics)\n",(unsigned long long)frame,why.c_str());
    }
    bool Render(Submission& frame) {
        stats={}; auto total=Clock::now(); Wait(); ReleaseFrame(); Begin(); Stage(frame);
        auto execute=Clock::now();
        for(size_t i=0;i<frame.commands.size();i++) {
            auto& c=frame.commands[i];
            if(c.kind==Command::Kind::Readback) { auto image=TargetImage(c.target,c.read_depth); Readback(image,c.readback); continue; }
            if(c.kind==Command::Kind::Copy) {
                auto source=colors.find(c.source); if(source==colors.end())throw std::runtime_error("Copy source was never rendered");
                auto target=TargetImage(c.target);
                if(c.rects.size()!=c.destination_rects.size())throw std::runtime_error("invalid owned CopyRects payload");
                for(size_t j=0;j<c.rects.size();j++)Blit(source->second,target,c.rects[j],c.destination_rects[j]);
                color_versions[c.target.color]=++color_serial; continue;
            }
            if(c.kind==Command::Kind::Clear) {
                if((c.flags&0xf0) && (c.flags&0xf0)!=0xf0)throw std::runtime_error("partial color-channel Clear unsupported; aborting rather than corrupting retained target");
                BeginTarget(c); std::vector<VkClearAttachment> clears;
                if(c.flags&0xf0) { VkClearAttachment a{}; a.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; a.clearValue.color={{Channel(c.color,16),Channel(c.color,8),Channel(c.color,0),Channel(c.color,24)}}; clears.push_back(a); }
                if((c.flags&3) && c.target.depth) { VkClearAttachment a{}; a.aspectMask=(c.flags&1?VK_IMAGE_ASPECT_DEPTH_BIT:0)|((c.flags&2) && DepthFormat(c.target.depth_format)==VK_FORMAT_D24_UNORM_S8_UINT?VK_IMAGE_ASPECT_STENCIL_BIT:0); a.clearValue.depthStencil={c.z,c.stencil}; if(a.aspectMask)clears.push_back(a); }
                std::vector<VkClearRect> rects; for(auto r:c.rects) { auto rect=Clip(r,c.target.width,c.target.height); if(rect.extent.width && rect.extent.height)rects.push_back({rect,0,1}); }
                if(c.rects.empty())rects.push_back({{{0,0},{c.target.width,c.target.height}},0,1});
                if(!clears.empty() && !rects.empty())vkCmdClearAttachments(cmd,clears.size(),clears.data(),rects.size(),rects.data());
                vkCmdEndRenderPass(cmd); color_versions[c.target.color]=++color_serial; continue;
            }
            // Validate and create resources outside the render pass; uploads and
            // barriers must never be recorded inside a Vulkan 1.0 subpass.
            VkPipeline pipeline=VK_NULL_HANDLE; VkDescriptorSet set=VK_NULL_HANDLE;
            try {
                if(c.vertices.empty() || c.draw_constants.empty())throw std::runtime_error("empty owned vertex/uniform data");
                if(c.draw_constants.size()>properties.limits.maxUniformBufferRange)throw std::runtime_error("DC exceeds uniform limit");
                for(auto index:c.indices)if(index>=c.vertices.size())throw std::runtime_error("index outside owned vertex buffer");
                auto color=TargetImage(c.target); VkFormat depth=VK_FORMAT_UNDEFINED; if(c.target.depth)depth=TargetImage(c.target,true)->format;
                pipeline=PipelineFor(c,Pass(color->format,depth)); set=Bindings(c,offsets[i]);
            } catch(const std::exception& e) {
                Reject(e.what(),frame.frame);
                if(c.visibility)c.visibility->set_exception(std::current_exception());
                continue;
            }
            BeginTarget(c); vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
            VkViewport viewport{0,0,float(c.target.width),float(c.target.height),0,1}; vkCmdSetViewport(cmd,0,1,&viewport);
            const auto& p=c.pipeline; float blend[4]={Channel(p.blend_color,16),Channel(p.blend_color,8),Channel(p.blend_color,0),Channel(p.blend_color,24)}; vkCmdSetBlendConstants(cmd,blend);
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);
            vkCmdBindVertexBuffers(cmd,0,1,&staged->buffer,&offsets[i].vertex);
            if(!c.indices.empty())vkCmdBindIndexBuffer(cmd,staged->buffer,offsets[i].index,VK_INDEX_TYPE_UINT32);
            uint32_t query=uint32_t(counted.size());
            if(c.visibility)vkCmdBeginQuery(cmd,occlusion_pool,query,VK_QUERY_CONTROL_PRECISE_BIT);
            auto draw=[&](Rect r) {
                int x=std::max(r.x,p.viewport.x),y=std::max(r.y,p.viewport.y); int right=std::min(r.x+r.w,p.viewport.x+p.viewport.w),bottom=std::min(r.y+r.h,p.viewport.y+p.viewport.h);
                auto scale=[&](int v){return int(std::lround(p.clip_pivot+(v-p.clip_pivot)*p.clip_scale));};
                auto scissor=Clip({scale(x),y,std::max(0,scale(right)-scale(x)),std::max(0,bottom-y)},c.target.width,c.target.height); vkCmdSetScissor(cmd,0,1,&scissor);
                if(c.indices.empty()) { vkCmdDraw(cmd,c.vertices.size(),1,0,0); ++stats.nonindexed; }
                else { vkCmdDrawIndexed(cmd,c.indices.size(),1,0,0,0); ++stats.indexed; }
            };
            if(p.scissors.empty())draw(p.viewport); else for(auto r:p.scissors)draw(r);
            if(c.visibility) { vkCmdEndQuery(cmd,occlusion_pool,query); counted.push_back({c.visibility,query}); }
            vkCmdEndRenderPass(cmd); color_versions[c.target.color]=++color_serial; ++stats.accepted;
        }
        Barrier(); stats.execute_ms=Millis(execute);
        bool present=false; unsigned image_index=0;
        if(frame.present) {
            auto it=colors.find(frame.front); if(it==colors.end())throw std::runtime_error("Present surface has no captured Vulkan commands"); front=it->second;
            if(surface) {
                if(resize)Recreate();
                if(!resize) { auto acquire=Clock::now(); VkResult r=vkAcquireNextImageKHR(device,swapchain,100000000,acquired,VK_NULL_HANDLE,&image_index); stats.acquire_ms=Millis(acquire);
                    if(r==VK_TIMEOUT || r==VK_NOT_READY)stats.acquire_timeout=true;
                    else if(r==VK_ERROR_OUT_OF_DATE_KHR)resize=true;
                    else { if(r==VK_SUBOPTIMAL_KHR)resize=true; else Check(r,"acquire game swapchain image"); present=true; RecordPresent(image_index); }
                }
            }
        }
        pending_completion=frame.completion;
        Submit(present,image_index,frame.frame);
        // Fence/Busy polling on the producer must make progress even if this is
        // the final prefix and no subsequent submission arrives to drain it.
        if(pending_completion)Wait();
        stats.cpu_ms=Millis(total); return true;
    }
};
Renderer::Renderer(SDL_Window* window):impl_(std::make_unique<Impl>(window)){ impl_->Init(); }
Renderer::~Renderer()=default;
bool Renderer::Render(Submission& frame) {
    try { return impl_->Render(frame); }
    catch(const std::exception& e) {
        auto error=std::current_exception(); for(auto& c:frame.commands)if(c.readback) { try { c.readback->set_exception(error); }catch(const std::future_error&){} }
        if(frame.completion) { try { frame.completion->set_exception(error); }catch(const std::future_error&){} }
        for(auto& c:frame.commands)if(c.visibility) { try { c.visibility->set_exception(error); }catch(const std::future_error&){} }
        impl_->pending_completion.reset();
        Fail(std::string("native Vulkan: ")+e.what()); std::fprintf(stderr,"native Vulkan failed: %s\n",e.what()); return false;
    }
}
void Renderer::Resize(){impl_->resize=true;}
void Renderer::SetAspect(float aspect){impl_->output_aspect=std::isfinite(aspect) && aspect>0?aspect:0;}
void Renderer::Finish(){impl_->stats={};impl_->Wait();}
const Renderer::Stats& Renderer::LastStats()const{return impl_->stats;}
bool Renderer::Capture(const char* path) {
    try {
        auto& g=*impl_; g.Wait(); if(!g.front)return false; g.ReleaseFrame(); g.Begin();
        auto promise=std::make_shared<std::promise<std::vector<uint32_t>>>(); auto future=promise->get_future(); g.Readback(g.front,promise); g.Submit(false,0,0); auto pixels=future.get();
        std::ofstream out(path,std::ios::binary); out<<"P6\n"<<g.front->w<<" "<<g.front->h<<"\n255\n";
        for(auto p:pixels){ char rgb[3]={char(p>>16),char(p>>8),char(p)}; out.write(rgb,3); }return bool(out);
    }catch(const std::exception& e){std::fprintf(stderr,"Vulkan capture failed: %s\n",e.what());return false;}
}
void Renderer::Compare(const Submission& frame) {
    if(frame.reference_pixels.empty())return;
    try {
        auto& g=*impl_; g.Wait();
        if(!g.front || g.front->w!=frame.reference_width || g.front->h!=frame.reference_height)
            throw std::runtime_error("reference/front dimensions differ");
        g.ReleaseFrame(); g.Begin();
        auto promise=std::make_shared<std::promise<std::vector<uint32_t>>>(); auto future=promise->get_future();
        g.Readback(g.front,promise); g.Submit(false,0,0); auto actual=future.get();
        if(actual.size()!=frame.reference_pixels.size())throw std::runtime_error("reference/front payload differs");
        uint64_t error=0,different=0; unsigned maximum=0;
        auto save=[&](const char* path,const std::vector<uint32_t>& pixels) {
            std::ofstream out(path,std::ios::binary); out<<"P6\n"<<g.front->w<<" "<<g.front->h<<"\n255\n";
            for(auto p:pixels) { char rgb[3]={char(p>>16),char(p>>8),char(p)};out.write(rgb,3); }
            if(!out)throw std::runtime_error(std::string("cannot write ")+path);
        };
        save("compare-reference.ppm",frame.reference_pixels); save("compare-vulkan.ppm",actual);
        for(size_t i=0;i<actual.size();i++) {
            unsigned pixel_error=0;
            for(unsigned shift:{0u,8u,16u}) {
                unsigned delta=unsigned(std::abs(int((actual[i]>>shift)&255)-int((frame.reference_pixels[i]>>shift)&255)));
                error+=delta; maximum=std::max(maximum,delta);pixel_error=std::max(pixel_error,delta);
            }
            different+=pixel_error>4;
        }
        std::ofstream report("compare.txt"); report<<"frame="<<frame.frame<<" mean_RGB_error="<<double(error)/(actual.size()*3)
            <<" max_channel_error="<<maximum<<" pixels_error_above_4="<<different<<"/"<<actual.size()<<"\n";
        std::fprintf(stderr,"Vulkan/CPU comparison frame %llu: see compare.txt and compare-*.ppm\n",(unsigned long long)frame.frame);
    }catch(const std::exception& e) { std::fprintf(stderr,"Vulkan comparison failed: %s\n",e.what()); }
}
}
