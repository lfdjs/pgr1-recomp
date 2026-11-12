#include "game_renderer.h"
#include "vk_postfx.h"
#include "quality_state.h"
#include "present_counter.h"
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
#include <cstdlib>
#include <limits>
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
    uint32_t instance_api=VK_API_VERSION_1_0;
    bool depth_resolve=false;
    VkSampleCountFlags msaa_supported=VK_SAMPLE_COUNT_1_BIT;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    uint32_t family=0,timestamp_bits=0;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkCommandBuffer cmd=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    bool pending=false,resize=true;
    bool applied_vsync=true;
    float output_aspect=0;
    unsigned screen_w=0,screen_h=0;
    // 04D.3 virtual display target scaling. 0 keeps guest resolution;
    // 50..100 is a percentage of the current SDL output pixel extent.
    // Higher resolution costs additional GPU work; no FPS promise.
    unsigned internal_percent=100;
    std::shared_ptr<std::promise<void>> pending_completion;
    VkSemaphore acquired=VK_NULL_HANDLE;
    VkSwapchainKHR swapchain=VK_NULL_HANDLE;
    // 04D.4 FXAA and RECOMP quality controls: swapchain presentation postpass.
    std::unique_ptr<PostFx> postfx;
    VkExtent2D extent{};
    VkFormat swap_format=VK_FORMAT_UNDEFINED;
    std::vector<VkImage> swap_images;
    std::vector<VkSemaphore> finished;
    VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache=VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;
    uint32_t descriptor_capacity=0; // 04D.2: reset instead of recreating each submission
    VkQueryPool queries=VK_NULL_HANDLE;
    VkQueryPool occlusion_pool=VK_NULL_HANDLE;
    struct Counted { std::shared_ptr<std::promise<uint64_t>> promise; uint32_t index;unsigned samples=1; };
    std::vector<Counted> counted;
    uint64_t query_frame=0;
    Stats stats;
    PresentCounter present_counter;
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
        VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
        std::shared_ptr<Image> multisample; // resolved image remains sampleable/readable
        std::unique_ptr<PostFx> seed; // GPU seed after CopyRects, created only when used
        bool needs_seed=false;
        unsigned guest_w=0,guest_h=0; // 04D.3: guest address space stays unchanged
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
    std::vector<std::unique_ptr<PostFx>> retired_postfx;
    std::unique_ptr<Buffer> stage_storage; // 04D.2 persistent mapped upload, fence-retired
    std::map<std::array<VkImageView,4>,VkFramebuffer> framebuffer_cache;
    std::map<std::tuple<VkFormat,VkFormat,VkSampleCountFlagBits>,VkRenderPass> passes;
    std::map<std::string,VkPipeline> pipelines;
    std::map<std::shared_ptr<const ShaderPair>,std::array<VkShaderModule,2>,std::owner_less<std::shared_ptr<const ShaderPair>>> modules;
    std::map<std::string,VkSampler> samplers;
    std::set<std::string> warnings;
    struct Read { std::shared_ptr<std::promise<std::vector<uint32_t>>> promise; Buffer* buffer; ImagePtr image; VkDeviceSize stencil_offset; bool guest_size; };
    std::vector<Read> reads;
    struct Offset { VkDeviceSize vertex=0,index=0,vc=0,dc=0; };
    std::vector<Offset> offsets;
    Buffer* staged=nullptr;
    // 04D.2 persistent GPU upload / descriptor resources; see docs/portable/08-vulkan-performance.md
    explicit Impl(SDL_Window* w):window(w){}
    ~Impl() {
        if(device) vkDeviceWaitIdle(device);
        postfx.reset(); ReleaseFrame(true); stage_storage.reset(); front.reset(); white.reset(); white_cube.reset(); textures.clear(); dynamic_cubes.clear(); colors.clear(); depths.clear();
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
        internal_percent=quality::render_percent.load(std::memory_order_relaxed);
        std::printf("PGR1 Vulkan internal resolution: %s (%u%% of SDL output when enabled)\n",
            internal_percent ? "experimental screen-matched" : "guest/native-Xbox",internal_percent);
        Uint32 count=0; const char* const* extensions=nullptr;
        if(window) { extensions=SDL_Vulkan_GetInstanceExtensions(&count); if(!extensions) throw std::runtime_error(SDL_GetError()); }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName="PGR1 real runtime native Vulkan";
        auto enumerate_version=reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr,"vkEnumerateInstanceVersion"));
        uint32_t loader_version=VK_API_VERSION_1_0;
        if(enumerate_version)Check(enumerate_version(&loader_version),"instance version");
        if(loader_version>=VK_API_VERSION_1_2)instance_api=VK_API_VERSION_1_2;
        app.apiVersion=instance_api;
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
        quality::aniso_supported_max.store(features.samplerAnisotropy?unsigned(properties.limits.maxSamplerAnisotropy):0u,std::memory_order_release);
        if(quality::aniso.load(std::memory_order_relaxed)>quality::aniso_supported_max.load())
            quality::aniso.store(quality::aniso_supported_max.load(),std::memory_order_relaxed);
        features.shaderClipDistance=available.shaderClipDistance;
        features.largePoints=available.largePoints;
        features.occlusionQueryPrecise=available.occlusionQueryPrecise;
        if(instance_api>=VK_API_VERSION_1_2 && properties.apiVersion>=VK_API_VERSION_1_2) {
            VkPhysicalDeviceDepthStencilResolveProperties resolve{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES};
            VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&resolve;
            vkGetPhysicalDeviceProperties2(gpu,&props);
            depth_resolve=(resolve.supportedDepthResolveModes&VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) &&
                          (resolve.supportedStencilResolveModes&VK_RESOLVE_MODE_SAMPLE_ZERO_BIT);
        }
        if(depth_resolve) {
            msaa_supported=properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts &
                           properties.limits.framebufferStencilSampleCounts & (VK_SAMPLE_COUNT_1_BIT|VK_SAMPLE_COUNT_2_BIT|VK_SAMPLE_COUNT_4_BIT);
            for(auto format:{VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R5G6B5_UNORM_PACK16,DepthStorage(0x2a),VK_FORMAT_D16_UNORM}) {
                const bool depth=format!=VK_FORMAT_B8G8R8A8_UNORM && format!=VK_FORMAT_R5G6B5_UNORM_PACK16;
                VkImageFormatProperties image{};
                const auto usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|
                    (depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
                if(vkGetPhysicalDeviceImageFormatProperties(gpu,format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usage,0,&image)==VK_SUCCESS)
                    msaa_supported &= image.sampleCounts;
                else msaa_supported=VK_SAMPLE_COUNT_1_BIT;
            }
        }
        quality::msaa_supported_mask.store(msaa_supported,std::memory_order_release);
        const auto requested_msaa=quality::msaa.load(std::memory_order_relaxed);
        if(requested_msaa>2 || !(msaa_supported&(1u<<requested_msaa)))quality::msaa.store(0,std::memory_order_relaxed);
        std::printf("Vulkan MSAA primary display targets: 2x=%u 4x=%u; depth/stencil SAMPLE_ZERO resolve=%u\n",
            !!(msaa_supported&VK_SAMPLE_COUNT_2_BIT),!!(msaa_supported&VK_SAMPLE_COUNT_4_BIT),depth_resolve);
        float priority=1; VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qc.queueFamilyIndex=family; qc.queueCount=1; qc.pQueuePriorities=&priority;
        const char* swap_extension=VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dc.queueCreateInfoCount=1; dc.pQueueCreateInfos=&qc; dc.pEnabledFeatures=&features;
        if(surface) { dc.enabledExtensionCount=1; dc.ppEnabledExtensionNames=&swap_extension; }
        Check(vkCreateDevice(gpu,&dc,nullptr,&device),"vkCreateDevice"); vkGetDeviceQueue(device,family,0,&queue);
        std::printf("PGR1 native Vulkan GAME backend: %s vendor=0x%x api=%u.%u.%u; instance=%u.%u, extension=%s\n",properties.deviceName,properties.vendorID,
            VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),VK_VERSION_PATCH(properties.apiVersion),VK_VERSION_MAJOR(instance_api),VK_VERSION_MINOR(instance_api),surface?swap_extension:"none (offscreen test)");
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
    std::unique_ptr<Buffer> MakeBuffer(VkDeviceSize size,VkBufferUsageFlags usage) {
        auto b=std::make_unique<Buffer>(device); b->size=std::max<VkDeviceSize>(size,16);
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size=b->size; ci.usage=usage; ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        Check(vkCreateBuffer(device,&ci,nullptr,&b->buffer),"vkCreateBuffer");
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device,b->buffer,&requirements);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=requirements.size;
        alloc.memoryTypeIndex=MemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        Check(vkAllocateMemory(device,&alloc,nullptr,&b->memory),"host buffer vkAllocateMemory");
        Check(vkBindBufferMemory(device,b->buffer,b->memory,0),"bind buffer");
        Check(vkMapMemory(device,b->memory,0,VK_WHOLE_SIZE,0,&b->mapped),"map host buffer");
        return b;
    }
    Buffer* NewBuffer(VkDeviceSize size,VkBufferUsageFlags usage,const void* data=nullptr) {
        auto b=MakeBuffer(size,usage);
        if(data && size)std::memcpy(b->mapped,data,size);
        auto* result=b.get(); buffers.push_back(std::move(b)); return result;
    }
    void Barrier() {
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);
    }
    std::pair<unsigned,unsigned> PhysicalExtent(const Target& target) const {
        // Identical to the OpenGL backend's screen-target criterion: small
        // targets and reflection cube faces are never expanded.
        const unsigned percent=quality::render_percent.load(std::memory_order_relaxed);
        if(!window || !percent || !screen_w || !screen_h ||
           (target.height!=480 && target.height!=576) || target.width<target.height)
            return {target.width,target.height};
        double content_w=screen_w,content_h=screen_h;
        const double aspect=output_aspect>0?output_aspect:double(screen_w)/screen_h;
        if(content_w/content_h>aspect) content_w=content_h*aspect;
        else content_h=content_w/aspect;
        // Round to physical pixels. Keep at least the original guest footprint.
        // Large or ultrawide targets are limited by the device's max image size.
        const double factor=double(percent)/100.0;
        const double base_h=target.height==576?576.0:480.0;
        const double want_w=double(target.width)*content_w/640.0*factor;
        const double want_h=double(target.height)*content_h/base_h*factor;
        const unsigned limit=properties.limits.maxImageDimension2D;
        return {std::clamp(unsigned(std::lround(want_w)),1u,limit),
                std::clamp(unsigned(std::lround(want_h)),1u,limit)};
    }
    static Rect PhysicalRect(Rect r,const Image& image) {
        const auto x=[&](int n) { return int(std::lround(double(n)*image.w/image.guest_w)); };
        const auto y=[&](int n) { return int(std::lround(double(n)*image.h/image.guest_h)); };
        const int left=x(r.x),top=y(r.y);
        return {left,top,x(r.x+r.w)-left,y(r.y+r.h)-top};
    }
    ImagePtr NewImage(unsigned w,unsigned h,VkFormat format,unsigned levels=1,unsigned layers=1,VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT) {
        if(!w || !h || w>properties.limits.maxImageDimension2D || h>properties.limits.maxImageDimension2D) throw std::runtime_error("Vulkan image extent out of device limits");
        auto image=std::make_shared<Image>(device); image->w=w; image->h=h; image->guest_w=w; image->guest_h=h; image->format=format; image->levels=levels; image->layers=layers; image->samples=samples;
        const bool depth=format==VK_FORMAT_D16_UNORM || format==VK_FORMAT_D24_UNORM_S8_UINT || format==VK_FORMAT_D32_SFLOAT_S8_UINT;
        image->aspect=depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
        if(format==VK_FORMAT_D24_UNORM_S8_UINT || format==VK_FORMAT_D32_SFLOAT_S8_UINT)image->aspect|=VK_IMAGE_ASPECT_STENCIL_BIT;
        VkFormatProperties fp{}; vkGetPhysicalDeviceFormatProperties(gpu,format,&fp);
        const auto required=depth?VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if((fp.optimalTilingFeatures&required)!=required)throw std::runtime_error("Vulkan required attachment/sample format unsupported: "+std::to_string(format));
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ci.imageType=VK_IMAGE_TYPE_2D; ci.format=format; ci.extent={w,h,1}; ci.mipLevels=levels; ci.arrayLayers=layers;
        ci.samples=samples; ci.tiling=VK_IMAGE_TILING_OPTIMAL; ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
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
    static bool DisplayTarget(const Target& target) {
        return (target.height==480 || target.height==576) && target.width>=target.height;
    }
    VkSampleCountFlagBits RequestedSamples(const Target& target) const {
        const auto value=quality::msaa.load(std::memory_order_relaxed);
        const auto samples=value<=2?VkSampleCountFlagBits(1u<<value):VK_SAMPLE_COUNT_1_BIT;
        return DisplayTarget(target) && (msaa_supported&samples)?samples:VK_SAMPLE_COUNT_1_BIT;
    }
    static VkSampleCountFlagBits Samples(const ImagePtr& image) {
        return image->multisample?image->multisample->samples:VK_SAMPLE_COUNT_1_BIT;
    }
    void Multisample(const ImagePtr& image,VkSampleCountFlagBits samples) {
        if(Samples(image)==samples)return;
        if(image->multisample)retained.push_back(image->multisample);
        if(image->seed)retired_postfx.push_back(std::move(image->seed));
        image->multisample.reset();
        if(samples!=VK_SAMPLE_COUNT_1_BIT) {
            image->multisample=NewImage(image->w,image->h,image->format,1,1,samples);
            image->needs_seed=(image->aspect&VK_IMAGE_ASPECT_COLOR_BIT)!=0;
        }
    }
    void SeedColor(const ImagePtr& image) {
        if(!image->multisample || !image->needs_seed)return;
        if(!image->seed)image->seed=std::make_unique<PostFx>(device,image->format,VkExtent2D{image->w,image->h},
            std::vector<VkImage>{image->multisample->image},PGR1_VK_POSTFX_VERT_SPV,PGR1_VK_POSTFX_FRAG_SPV,
            Samples(image),VK_IMAGE_LAYOUT_GENERAL);
        image->seed->Record(cmd,image->image,image->sample,image->w,image->h,0,0,false,0);
        image->needs_seed=false;
    }
    // A setting change waits for a complete depth/stencil clear. The title
    // commonly clears these alone (flags 0x03); retained color can be seeded on
    // the GPU. No retained depth/stencil is discarded mid-multipass frame.
    void PrepareClear(const Command& c) {
        if(!DisplayTarget(c.target) || (c.target.depth && !(c.flags&1)) ||
           (!c.target.depth && (c.flags&0xf0)!=0xf0) ||
           (c.target.depth && DepthFormat(c.target.depth_format)==VK_FORMAT_D24_UNORM_S8_UINT && !(c.flags&2)))return;
        bool whole=c.rects.empty();
        for(auto r:c.rects)whole |= r.x<=0 && r.y<=0 && r.x+r.w>=int(c.target.width) && r.y+r.h>=int(c.target.height);
        if(!whole)return;
        auto color=TargetImage(c.target);const auto samples=RequestedSamples(c.target);
        Multisample(color,samples);
        if((c.flags&0xf0)==0xf0)color->needs_seed=false; // a complete color clear replaces every sample
        if(c.target.depth)Multisample(TargetImage(c.target,true),samples);
        const unsigned applied=samples==VK_SAMPLE_COUNT_4_BIT?2u:samples==VK_SAMPLE_COUNT_2_BIT?1u:0u;
        quality::msaa_applied.store(applied,std::memory_order_release);
    }
    ImagePtr TargetImage(const Target& target,bool depth=false) {
        auto& table=depth?depths:colors; const auto address=depth?target.depth:target.color;
        if(!address)throw std::runtime_error("missing Vulkan target guest address");
        auto& image=table[address]; const auto format=depth?DepthStorage(target.depth_format):ColorFormat(target.format);
        const auto [physical_w,physical_h]=PhysicalExtent(target);
        if(!image || image->w!=physical_w || image->h!=physical_h || image->format!=format ||
           image->guest_w!=target.width || image->guest_h!=target.height) {
            if(image)retained.push_back(image);
            // Opaque guest formats are lowered by the SDK's kTexOpaque shader
            // flag. Keep the storage/view neutral when Xbox reinterprets ARGB.
            image=NewImage(physical_w,physical_h,format);
            image->guest_w=target.width; image->guest_h=target.height;
            const auto color=colors.find(target.color);
            Multisample(image,depth && color!=colors.end() && color->second?Samples(color->second):RequestedSamples(target));
            // Reused Xbox addresses can refer to newly allocated GPU images.
            // A new source invalidates the corresponding reflection face.
            if(!depth) color_versions[address]=++color_serial;
        }
        return image;
    }
    VkRenderPass Pass(VkFormat color,VkFormat depth,VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT) {
        auto key=std::make_tuple(color,depth,samples); auto it=passes.find(key); if(it!=passes.end())return it->second;
        const bool multisample=samples!=VK_SAMPLE_COUNT_1_BIT,has_depth=depth!=VK_FORMAT_UNDEFINED;
        const uint32_t resolved_color=has_depth?2:1,resolved_depth=3;
        std::array<VkAttachmentDescription2,4> a{};
        for(auto& attachment:a) {
            attachment.sType=VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
            attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
        }
        a[0].format=color;a[0].samples=samples;
        if(has_depth) { a[1]=a[0];a[1].format=depth;a[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;a[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE; }
        if(multisample) {
            a[resolved_color]=a[0];a[resolved_color].samples=VK_SAMPLE_COUNT_1_BIT;a[resolved_color].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            if(has_depth) { a[resolved_depth]=a[1];a[resolved_depth].samples=VK_SAMPLE_COUNT_1_BIT;a[resolved_depth].loadOp=a[resolved_depth].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; }
        }
        VkAttachmentReference2 c{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2},d=c,r=c,z=c;
        c.attachment=0;c.layout=VK_IMAGE_LAYOUT_GENERAL;c.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        d.attachment=1;d.layout=VK_IMAGE_LAYOUT_GENERAL;d.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
        r=c;r.attachment=resolved_color;z=d;z.attachment=resolved_depth;
        VkSubpassDescriptionDepthStencilResolve resolve{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE};
        resolve.depthResolveMode=VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
        resolve.stencilResolveMode=(depth==VK_FORMAT_D24_UNORM_S8_UINT || depth==VK_FORMAT_D32_SFLOAT_S8_UINT)?VK_RESOLVE_MODE_SAMPLE_ZERO_BIT:VK_RESOLVE_MODE_NONE;
        resolve.pDepthStencilResolveAttachment=&z;
        VkSubpassDescription2 sub{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount=1;sub.pColorAttachments=&c;if(has_depth)sub.pDepthStencilAttachment=&d;
        if(multisample) { sub.pResolveAttachments=&r;if(has_depth)sub.pNext=&resolve; }
        VkSubpassDependency2 dependencies[2]{};
        for(auto& dependency:dependencies)dependency.sType=VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
        dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL; dependencies[0].dstSubpass=0;
        dependencies[0].srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; dependencies[0].dstStageMask=VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        dependencies[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT; dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass=0; dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL; dependencies[1].srcStageMask=VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT; dependencies[1].dstStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; dependencies[1].dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        VkRenderPass pass=VK_NULL_HANDLE;
        const uint32_t count=(has_depth?2:1)*(multisample?2:1);
        if(instance_api>=VK_API_VERSION_1_2 && properties.apiVersion>=VK_API_VERSION_1_2) {
            VkRenderPassCreateInfo2 info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};info.attachmentCount=count;info.pAttachments=a.data();
            info.subpassCount=1;info.pSubpasses=&sub;info.dependencyCount=2;info.pDependencies=dependencies;
            Check(vkCreateRenderPass2(device,&info,nullptr,&pass),"render pass with multisample resolve");
        } else {
            // Preserve the original Vulkan 1.0 single-sample backend.
            std::array<VkAttachmentDescription,2> old{};
            for(unsigned i=0;i<count;i++)old[i]={a[i].flags,a[i].format,a[i].samples,a[i].loadOp,a[i].storeOp,a[i].stencilLoadOp,a[i].stencilStoreOp,a[i].initialLayout,a[i].finalLayout};
            VkAttachmentReference color_ref{0,VK_IMAGE_LAYOUT_GENERAL},depth_ref{1,VK_IMAGE_LAYOUT_GENERAL};
            VkSubpassDescription old_sub{};old_sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;old_sub.colorAttachmentCount=1;old_sub.pColorAttachments=&color_ref;
            if(has_depth)old_sub.pDepthStencilAttachment=&depth_ref;
            VkSubpassDependency old_dependencies[2]{};
            for(unsigned i=0;i<2;i++)old_dependencies[i]={dependencies[i].srcSubpass,dependencies[i].dstSubpass,dependencies[i].srcStageMask,dependencies[i].dstStageMask,dependencies[i].srcAccessMask,dependencies[i].dstAccessMask,0};
            VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};info.attachmentCount=count;info.pAttachments=old.data();info.subpassCount=1;info.pSubpasses=&old_sub;info.dependencyCount=2;info.pDependencies=old_dependencies;
            Check(vkCreateRenderPass(device,&info,nullptr,&pass),"render pass");
        }
        passes[key]=pass;return pass;
    }
    VkRenderPass BeginTarget(const Command& c) {
        auto color=TargetImage(c.target);ImagePtr depth;if(c.target.depth)depth=TargetImage(c.target,true);
        auto samples=Samples(color);
        if(depth && Samples(depth)!=samples) {
            // A depth surface can be shared by retained display buffers. Its
            // clear owns the transition; re-seed color to that sample count.
            Multisample(color,Samples(depth));samples=Samples(color);
        }
        SeedColor(color);
        VkRenderPass pass=Pass(color->format,depth?depth->format:VK_FORMAT_UNDEFINED,samples);
        std::array<VkImageView,4> attachments{};
        unsigned count=0;attachments[count++]=color->multisample?color->multisample->view:color->view;
        if(depth)attachments[count++]=depth->multisample?depth->multisample->view:depth->view;
        if(samples!=VK_SAMPLE_COUNT_1_BIT) { attachments[count++]=color->view;if(depth)attachments[count++]=depth->view; }
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=pass;fb.attachmentCount=count;fb.pAttachments=attachments.data();fb.width=color->w;fb.height=color->h;fb.layers=1;
        auto found=framebuffer_cache.find(attachments);VkFramebuffer framebuffer=VK_NULL_HANDLE;
        if(found!=framebuffer_cache.end()) { framebuffer=found->second;++stats.framebuffer_hits; }
        else { Check(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer),"framebuffer");framebuffer_cache[attachments]=framebuffer;++stats.framebuffer_creates; }
        Barrier();VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=framebuffer;begin.renderArea.extent={color->w,color->h};
        vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);return pass;
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
        const bool alpha_coverage=quality::alpha_coverage.load(std::memory_order_relaxed);Key(key,alpha_coverage);
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
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=Samples(TargetImage(c.target));ms.alphaToCoverageEnable=alpha_coverage && ms.rasterizationSamples!=VK_SAMPLE_COUNT_1_BIT;
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
        const unsigned forced_aniso=quality::aniso.load(std::memory_order_relaxed);
        std::string key; for(auto v:{t.wrap_u,t.wrap_v,t.wrap_w,t.min_filter,t.mag_filter,t.mip_filter,t.max_mip,t.max_anisotropy,t.border_color,levels})Key(key,v); Key(key,t.lod_bias); Key(key,forced_aniso);
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
        ci.anisotropyEnable=features.samplerAnisotropy && (forced_aniso>1 || t.min_filter==3 || t.mag_filter==3); ci.maxAnisotropy=std::clamp(float(forced_aniso>1?forced_aniso:t.max_anisotropy),1.0f,properties.limits.maxSamplerAnisotropy);
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
                // Xbox reflection faces can have multiple mip levels. After
                // replacing mip0 with a GPU render target, regenerate its
                // coarser levels or the car samples stale highlights.
                if(image->levels>1 && quality::reflection_mips.load(std::memory_order_relaxed)) {
                    VkFormatProperties fp{}; vkGetPhysicalDeviceFormatProperties(gpu,image->format,&fp);
                    const VkFormatFeatureFlags bits=fp.optimalTilingFeatures;
                    if((bits&VK_FORMAT_FEATURE_BLIT_SRC_BIT) && (bits&VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
                        const VkFilter filter=(bits&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                            ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
                        for(unsigned mip=1;mip<image->levels;mip++) {
                            Barrier();
                            VkImageBlit b{};
                            b.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip-1,face,1};
                            b.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,face,1};
                            b.srcOffsets[1]={int(std::max(1u,image->w>>(mip-1))),int(std::max(1u,image->h>>(mip-1))),1};
                            b.dstOffsets[1]={int(std::max(1u,image->w>>mip)),int(std::max(1u,image->h>>mip)),1};
                            vkCmdBlitImage(cmd,image->image,VK_IMAGE_LAYOUT_GENERAL,image->image,VK_IMAGE_LAYOUT_GENERAL,1,&b,filter);
                        }
                        Barrier();
                    }
                }
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
        // 04D.2: two-pass offset planning, then ONE copy of vertex/index/constant
        // data directly into persistently mapped HOST_COHERENT memory. The next
        // submission cannot overwrite this buffer before Wait() observes its
        // GPU fence (Render calls Wait/ReleaseFrame before Stage).
        offsets.resize(frame.commands.size());
        VkDeviceSize required=0;
        const VkDeviceSize ualign=std::max<VkDeviceSize>(1,properties.limits.minUniformBufferOffsetAlignment);
        auto allocate=[&](size_t bytes,VkDeviceSize align) {
            const VkDeviceSize padded=std::max<VkDeviceSize>(VkDeviceSize(bytes),16);
            if(required>std::numeric_limits<VkDeviceSize>::max()-(align-1))
                throw std::runtime_error("staging offset overflow");
            const VkDeviceSize start=((required+align-1)/align)*align;
            if(start>std::numeric_limits<VkDeviceSize>::max()-padded)
                throw std::runtime_error("staging size overflow");
            required=start+padded; return start;
        };
        for(size_t i=0;i<frame.commands.size();i++) {
            const auto& c=frame.commands[i]; if(c.kind!=Command::Kind::Draw)continue;
            auto& o=offsets[i];
            o.vertex=allocate(c.vertices.size()*sizeof(Vertex),16);
            o.index=allocate(c.indices.size()*sizeof(uint32_t),4);
            o.vc=allocate(sizeof(c.vertex_constants),ualign);
            o.dc=allocate(c.draw_constants.size(),ualign);
        }
        const VkDeviceSize needed=std::max<VkDeviceSize>(required,16);
        if(!stage_storage || stage_storage->size<needed) {
            // Exponential growth prevents reallocations during menu/car changes.
            const VkDeviceSize previous=stage_storage?stage_storage->size:0;
            const VkDeviceSize doubled=previous<=std::numeric_limits<VkDeviceSize>::max()/2?previous*2:previous;
            const VkDeviceSize capacity=std::max(needed,std::max<VkDeviceSize>(doubled,64*1024));
            stage_storage=MakeBuffer(capacity,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            ++stats.staging_allocations;
        } else ++stats.staging_reuses;
        staged=stage_storage.get();
        auto* mapped=static_cast<uint8_t*>(staged->mapped);
        for(size_t i=0;i<frame.commands.size();i++) {
            const auto& c=frame.commands[i]; if(c.kind!=Command::Kind::Draw)continue;
            const auto& o=offsets[i];
            if(!c.vertices.empty())std::memcpy(mapped+o.vertex,c.vertices.data(),c.vertices.size()*sizeof(Vertex));
            if(!c.indices.empty())std::memcpy(mapped+o.index,c.indices.data(),c.indices.size()*sizeof(uint32_t));
            std::memcpy(mapped+o.vc,c.vertex_constants.data(),sizeof(c.vertex_constants));
            if(!c.draw_constants.empty())std::memcpy(mapped+o.dc,c.draw_constants.data(),c.draw_constants.size());
        }
        if(frame.commands.size()>size_t(std::numeric_limits<uint32_t>::max()/4))
            throw std::runtime_error("too many Vulkan draw descriptors");
        const uint32_t count=uint32_t(std::max(size_t(1),frame.commands.size()));
        if(!descriptor_pool || count>descriptor_capacity) {
            if(descriptor_pool)vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
            descriptor_pool=VK_NULL_HANDLE;
            const uint32_t max_capacity=UINT32_MAX/4;
            const uint32_t grown=descriptor_capacity<=max_capacity/2?descriptor_capacity*2:max_capacity;
            descriptor_capacity=std::max(count,grown);
            VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,descriptor_capacity*2},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,descriptor_capacity*4},{VK_DESCRIPTOR_TYPE_SAMPLER,descriptor_capacity*4}};
            VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; ci.maxSets=descriptor_capacity; ci.poolSizeCount=3; ci.pPoolSizes=sizes;
            Check(vkCreateDescriptorPool(device,&ci,nullptr,&descriptor_pool),"draw descriptor pool");
            ++stats.descriptor_creates;
        } else {
            // Legal: previous use finished before Stage(), and pool was created
            // without FREE_DESCRIPTOR_SET. Reclaim all per-draw sets at once.
            Check(vkResetDescriptorPool(device,descriptor_pool,0),"reset draw descriptor pool");
            ++stats.descriptor_resets;
        }
        const uint32_t query_count=std::count_if(frame.commands.begin(),frame.commands.end(),[](const auto& c){return bool(c.visibility);});
        if(query_count) {
            VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; query.queryType=VK_QUERY_TYPE_OCCLUSION; query.queryCount=query_count;
            Check(vkCreateQueryPool(device,&query,nullptr,&occlusion_pool),"precise occlusion pool");
            vkCmdResetQueryPool(cmd,occlusion_pool,0,query_count);
        }
    }
    void Readback(const ImagePtr& image,std::shared_ptr<std::promise<std::vector<uint32_t>>> promise,bool guest_size=true) {
        VkDeviceSize bytes=VkDeviceSize(image->w)*image->h*(image->format==VK_FORMAT_D16_UNORM || image->format==VK_FORMAT_R5G6B5_UNORM_PACK16?2:4);
        VkDeviceSize stencil_offset=(bytes+3)&~VkDeviceSize(3);
        bool stencil=bool(image->aspect&VK_IMAGE_ASPECT_STENCIL_BIT);
        auto* buffer=NewBuffer(stencil?stencil_offset+VkDeviceSize(image->w)*image->h:bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        Barrier(); VkBufferImageCopy copies[2]{}; copies[0].imageSubresource={image->aspect&~VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1}; copies[0].imageExtent={image->w,image->h,1};
        copies[1]=copies[0]; copies[1].bufferOffset=stencil_offset; copies[1].imageSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;
        vkCmdCopyImageToBuffer(cmd,image->image,VK_IMAGE_LAYOUT_GENERAL,buffer->buffer,stencil?2:1,copies);
        reads.push_back({std::move(promise),buffer,image,stencil_offset,guest_size});
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
            if(r.guest_size && (r.image->guest_w!=r.image->w || r.image->guest_h!=r.image->h)) {
                std::vector<uint32_t> guest(size_t(r.image->guest_w)*r.image->guest_h);
                for(unsigned y=0;y<r.image->guest_h;y++) {
                    const auto py=std::min(r.image->h-1,unsigned((uint64_t(y)*r.image->h+r.image->h/2)/r.image->guest_h));
                    for(unsigned x=0;x<r.image->guest_w;x++) {
                        const auto px=std::min(r.image->w-1,unsigned((uint64_t(x)*r.image->w+r.image->w/2)/r.image->guest_w));
                        guest[size_t(y)*r.image->guest_w+x]=pixels[size_t(py)*r.image->w+px];
                    }
                }
                pixels.swap(guest);
            }
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
            // Host MSAA increases passing sample counts; expose the equivalent
            // covered-pixel area to the unchanged Xbox visibility consumers.
            for(const auto& result:counted)result.promise->set_value((values[result.index]+result.samples/2)/result.samples);
            counted.clear();
        }
        if(pending_completion) { pending_completion->set_value(); pending_completion.reset(); }
    }
    void ReleaseFrame(bool final=false) {
        // Called after the submission fence. Keep framebuffers for live target
        // image views; retire stale framebuffer objects BEFORE the old images.
        std::set<VkImageView> live;
        if(!final) {
            for(const auto& [_,image]:colors)if(image) { live.insert(image->view);if(image->multisample)live.insert(image->multisample->view); }
            for(const auto& [_,image]:depths)if(image) { live.insert(image->view);if(image->multisample)live.insert(image->multisample->view); }
        }
        for(auto it=framebuffer_cache.begin();it!=framebuffer_cache.end();) {
            if(final || std::any_of(it->first.begin(),it->first.end(),[&](auto view){return view!=VK_NULL_HANDLE && !live.contains(view);})) {
                vkDestroyFramebuffer(device,it->second,nullptr);
                it=framebuffer_cache.erase(it);
            } else ++it;
        }
        retired_postfx.clear();buffers.clear(); retained.clear(); reads.clear();
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
        postfx.reset();
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
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; ci.surface=surface; ci.minImageCount=image_count; ci.imageFormat=selected.format; ci.imageColorSpace=selected.colorSpace; ci.imageExtent=chosen; ci.imageArrayLayers=1; ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const bool postfx_supported=bool(caps.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
            bool(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
        if(postfx_supported)ci.imageUsage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE; ci.preTransform=caps.currentTransform; ci.compositeAlpha=alpha; ci.presentMode=VK_PRESENT_MODE_FIFO_KHR;
        const bool want_vsync=quality::vsync.load(std::memory_order_relaxed);
        if(!want_vsync) {
            uint32_t modes_count=0;
            Check(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu,surface,&modes_count,nullptr),"present modes count");
            std::vector<VkPresentModeKHR> available(modes_count);
            Check(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu,surface,&modes_count,available.data()),"present modes");
            const char* preference=std::getenv("PGR1_VK_PRESENT_MODE");
            const auto preferred=preference && std::strcmp(preference,"immediate")==0?VK_PRESENT_MODE_IMMEDIATE_KHR:VK_PRESENT_MODE_MAILBOX_KHR;
            const auto alternate=preferred==VK_PRESENT_MODE_MAILBOX_KHR?VK_PRESENT_MODE_IMMEDIATE_KHR:VK_PRESENT_MODE_MAILBOX_KHR;
            if(std::find(available.begin(),available.end(),preferred)!=available.end())ci.presentMode=preferred;
            else if(std::find(available.begin(),available.end(),alternate)!=available.end())ci.presentMode=alternate;
            else {
                quality::vsync.store(true,std::memory_order_relaxed);
                std::fputs("Vulkan: unpaced present modes unavailable; VSync remains on (FIFO)\n",stderr);
            }
        }
        applied_vsync=ci.presentMode==VK_PRESENT_MODE_FIFO_KHR;
        quality::postfx_supported.store(postfx_supported,std::memory_order_release);
        if(!postfx_supported) {
            quality::fxaa.store(false,std::memory_order_relaxed);quality::sharpen_percent.store(0,std::memory_order_relaxed);
            quality::sharp_scaling.store(false,std::memory_order_relaxed);
        }
        ci.clipped=VK_TRUE; ci.oldSwapchain=swapchain;
        VkSwapchainKHR replacement=VK_NULL_HANDLE; Check(vkCreateSwapchainKHR(device,&ci,nullptr,&replacement),"recreate swapchain");
        if(swapchain)vkDestroySwapchainKHR(device,swapchain,nullptr); swapchain=replacement; extent=chosen; swap_format=selected.format;
        for(auto s:finished)vkDestroySemaphore(device,s,nullptr); finished.clear();
        Check(vkGetSwapchainImagesKHR(device,swapchain,&count,nullptr),"swapchain image count"); swap_images.resize(count); Check(vkGetSwapchainImagesKHR(device,swapchain,&count,swap_images.data()),"swapchain images");
        VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; for(unsigned i=0;i<count;i++){ VkSemaphore s=VK_NULL_HANDLE; Check(vkCreateSemaphore(device,&sc,nullptr,&s),"per-image present semaphore"); finished.push_back(s); }
        if(postfx_supported) {
            postfx=std::make_unique<PostFx>(device,swap_format,extent,swap_images,
                PGR1_VK_POSTFX_VERT_SPV,PGR1_VK_POSTFX_FRAG_SPV);
        } else if(warnings.insert("postfx swapchain").second)
            std::fputs("Vulkan: swapchain lacks COLOR_ATTACHMENT; FXAA unavailable, blit fallback\n",stderr);
        resize=false; std::printf("Vulkan game swapchain %ux%u images=%u mode=%s\n",extent.width,extent.height,count,ci.presentMode==VK_PRESENT_MODE_MAILBOX_KHR?"MAILBOX":ci.presentMode==VK_PRESENT_MODE_IMMEDIATE_KHR?"IMMEDIATE":"FIFO");
    }
    void Begin() {
        Check(vkResetCommandBuffer(cmd,0),"reset commands"); VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; Check(vkBeginCommandBuffer(cmd,&begin),"begin commands");
        if(queries){ vkCmdResetQueryPool(cmd,queries,0,2); vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,0); }
    }
    void Submit(bool present,unsigned image_index,uint64_t frame) {
        if(queries)vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,1);
        Check(vkEndCommandBuffer(cmd),"end commands"); Check(vkResetFences(device,1,&fence),"reset fence");
        VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO}; info.commandBufferCount=1; info.pCommandBuffers=&cmd;
        if(present){info.waitSemaphoreCount=1; info.pWaitSemaphores=&acquired; info.pWaitDstStageMask=&stage; info.signalSemaphoreCount=1; info.pSignalSemaphores=&finished.at(image_index);}
        Check(vkQueueSubmit(queue,1,&info,fence),"submit native game commands"); pending=true; query_frame=frame;
        if(present) {
            VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount=1; pi.pWaitSemaphores=&finished.at(image_index); pi.swapchainCount=1; pi.pSwapchains=&swapchain; pi.pImageIndices=&image_index;
            auto start=Clock::now(); VkResult r=vkQueuePresentKHR(queue,&pi); stats.present_ms=Millis(start);
            if(r==VK_ERROR_OUT_OF_DATE_KHR || r==VK_SUBOPTIMAL_KHR)resize=true; else Check(r,"present native game image");
            stats.presented=r==VK_SUCCESS || r==VK_SUBOPTIMAL_KHR;
            if(stats.presented)present_counter.Presented(std::chrono::duration<double>(Clock::now().time_since_epoch()).count());
        }
        if(!reads.empty() || !counted.empty())Wait();
    }
    void RecordPresent(unsigned image_index) {
        const bool fxaa=quality::fxaa.load(std::memory_order_relaxed);
        const unsigned sharpen=quality::sharpen_percent.load(std::memory_order_relaxed);
        const bool sharp_scaling=quality::sharp_scaling.load(std::memory_order_relaxed);
        if(postfx && (fxaa || sharpen || sharp_scaling || quality::show_fps.load(std::memory_order_relaxed))) {
            postfx->Record(cmd,front->image,front->sample,front->w,front->h,
                output_aspect,std::min(100u,sharpen+(sharp_scaling?25u:0u)),fxaa,image_index,
                quality::show_fps.load(std::memory_order_relaxed)?present_counter.Text():nullptr);
            return;
        }
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
        VkFormatProperties front_features{}; vkGetPhysicalDeviceFormatProperties(gpu,front->format,&front_features);
        const auto filter=(front_features.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
            ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        vkCmdBlitImage(cmd,front->image,VK_IMAGE_LAYOUT_GENERAL,barrier.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,filter);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=0;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }
    void Reject(const std::string& why,uint64_t frame) {
        ++stats.rejected; if(warnings.insert(why).second)std::fprintf(stderr,"Vulkan frame %llu rejected draw: %s; affected geometry absent (counted in metrics)\n",(unsigned long long)frame,why.c_str());
    }
    bool Render(Submission& frame) {
        stats={}; auto total=Clock::now(); Wait(); ReleaseFrame();
        if(window) {
            if(quality::vsync.load(std::memory_order_relaxed)!=applied_vsync)resize=true;
            int w=0,h=0; SDL_GetWindowSizeInPixels(window,&w,&h);
            if(w>0 && h>0) { screen_w=unsigned(w); screen_h=unsigned(h); }
        }
        Begin(); Stage(frame);
        auto execute=Clock::now();
        for(size_t i=0;i<frame.commands.size();i++) {
            auto& c=frame.commands[i];
            if(c.kind==Command::Kind::Readback) { auto image=TargetImage(c.target,c.read_depth); Readback(image,c.readback); continue; }
            if(c.kind==Command::Kind::Copy) {
                auto source=colors.find(c.source); if(source==colors.end())throw std::runtime_error("Copy source was never rendered");
                auto target=TargetImage(c.target);
                if(c.rects.size()!=c.destination_rects.size())throw std::runtime_error("invalid owned CopyRects payload");
                for(size_t j=0;j<c.rects.size();j++)
                    Blit(source->second,target,PhysicalRect(c.rects[j],*source->second),
                         PhysicalRect(c.destination_rects[j],*target));
                target->needs_seed=bool(target->multisample);
                color_versions[c.target.color]=++color_serial; continue;
            }
            if(c.kind==Command::Kind::Clear) {
                if((c.flags&0xf0) && (c.flags&0xf0)!=0xf0)throw std::runtime_error("partial color-channel Clear unsupported; aborting rather than corrupting retained target");
                PrepareClear(c);BeginTarget(c); std::vector<VkClearAttachment> clears;
                if(c.flags&0xf0) { VkClearAttachment a{}; a.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; a.clearValue.color={{Channel(c.color,16),Channel(c.color,8),Channel(c.color,0),Channel(c.color,24)}}; clears.push_back(a); }
                if((c.flags&3) && c.target.depth) { VkClearAttachment a{}; a.aspectMask=(c.flags&1?VK_IMAGE_ASPECT_DEPTH_BIT:0)|((c.flags&2) && DepthFormat(c.target.depth_format)==VK_FORMAT_D24_UNORM_S8_UINT?VK_IMAGE_ASPECT_STENCIL_BIT:0); a.clearValue.depthStencil={c.z,c.stencil}; if(a.aspectMask)clears.push_back(a); }
                const auto color_image=colors.at(c.target.color);
                std::vector<VkClearRect> rects; for(auto r:c.rects) {
                    auto rect=Clip(PhysicalRect(r,*color_image),color_image->w,color_image->h);
                    if(rect.extent.width && rect.extent.height)rects.push_back({rect,0,1});
                }
                if(c.rects.empty())rects.push_back({{{0,0},{color_image->w,color_image->h}},0,1});
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
                auto color=TargetImage(c.target); VkFormat depth=VK_FORMAT_UNDEFINED;
                if(c.target.depth) { auto z=TargetImage(c.target,true);depth=z->format;if(Samples(z)!=Samples(color))Multisample(color,Samples(z)); }
                pipeline=PipelineFor(c,Pass(color->format,depth,Samples(color))); set=Bindings(c,offsets[i]);
            } catch(const std::exception& e) {
                Reject(e.what(),frame.frame);
                if(c.visibility)c.visibility->set_exception(std::current_exception());
                continue;
            }
            BeginTarget(c); vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
            const auto image_extent=colors.at(c.target.color);
            VkViewport viewport{0,0,float(image_extent->w),float(image_extent->h),0,1}; vkCmdSetViewport(cmd,0,1,&viewport);
            const auto& p=c.pipeline; float blend[4]={Channel(p.blend_color,16),Channel(p.blend_color,8),Channel(p.blend_color,0),Channel(p.blend_color,24)}; vkCmdSetBlendConstants(cmd,blend);
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);
            vkCmdBindVertexBuffers(cmd,0,1,&staged->buffer,&offsets[i].vertex);
            if(!c.indices.empty())vkCmdBindIndexBuffer(cmd,staged->buffer,offsets[i].index,VK_INDEX_TYPE_UINT32);
            uint32_t query=uint32_t(counted.size());
            if(c.visibility)vkCmdBeginQuery(cmd,occlusion_pool,query,VK_QUERY_CONTROL_PRECISE_BIT);
            auto draw=[&](Rect r) {
                int x=std::max(r.x,p.viewport.x),y=std::max(r.y,p.viewport.y); int right=std::min(r.x+r.w,p.viewport.x+p.viewport.w),bottom=std::min(r.y+r.h,p.viewport.y+p.viewport.h);
                auto scale=[&](int v){return int(std::lround(p.clip_pivot+(v-p.clip_pivot)*p.clip_scale));};
                const auto guest_rect=Rect{scale(x),y,std::max(0,scale(right)-scale(x)),std::max(0,bottom-y)};
                const auto scissor=Clip(PhysicalRect(guest_rect,*image_extent),image_extent->w,image_extent->h); vkCmdSetScissor(cmd,0,1,&scissor);
                if(c.indices.empty()) { vkCmdDraw(cmd,c.vertices.size(),1,0,0); ++stats.nonindexed; }
                else { vkCmdDrawIndexed(cmd,c.indices.size(),1,0,0,0); ++stats.indexed; }
            };
            if(p.scissors.empty())draw(p.viewport); else for(auto r:p.scissors)draw(r);
            if(c.visibility) { vkCmdEndQuery(cmd,occlusion_pool,query); counted.push_back({c.visibility,query,unsigned(Samples(image_extent))}); }
            vkCmdEndRenderPass(cmd); color_versions[c.target.color]=++color_serial; ++stats.accepted;
        }
        Barrier(); stats.execute_ms=Millis(execute);
        bool present=false; unsigned image_index=0;
        if(frame.present) {
            auto it=colors.find(frame.front); if(it==colors.end())throw std::runtime_error("Present surface has no captured Vulkan commands"); front=it->second;
            // Newly allocated primary targets can already use the requested
            // samples before a later complete clear. Report the presented
            // surface, rather than inferring applied state from clear flags.
            const auto samples=Samples(front);
            quality::msaa_applied.store(samples==VK_SAMPLE_COUNT_4_BIT?2u:samples==VK_SAMPLE_COUNT_2_BIT?1u:0u,std::memory_order_release);
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
        stats.presented_fps=present_counter.Fps();stats.presented_frame_ms=present_counter.FrameMs();
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
Renderer::DisplayPixels Renderer::ReadDisplay() {
    auto& g=*impl_;g.Wait();if(!g.front)return {};
    g.ReleaseFrame();g.Begin();
    const VkExtent2D output=g.extent.width?g.extent:VkExtent2D{g.front->w,g.front->h};
    auto image=g.NewImage(output.width,output.height,VK_FORMAT_B8G8R8A8_UNORM);
    PostFx post(g.device,image->format,output,std::vector<VkImage>{image->image},
        PGR1_VK_POSTFX_VERT_SPV,PGR1_VK_POSTFX_FRAG_SPV,VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_LAYOUT_GENERAL);
    const auto sharpen=quality::sharpen_percent.load(std::memory_order_relaxed);
    post.Record(g.cmd,g.front->image,g.front->sample,g.front->w,g.front->h,g.output_aspect,
        std::min(100u,sharpen+(quality::sharp_scaling.load(std::memory_order_relaxed)?25u:0u)),
        quality::fxaa.load(std::memory_order_relaxed),0,
        quality::show_fps.load(std::memory_order_relaxed)?g.present_counter.Text():nullptr);
    auto promise=std::make_shared<std::promise<std::vector<uint32_t>>>();auto future=promise->get_future();
    g.Readback(image,promise,false);g.Submit(false,0,0);
    return {output.width,output.height,future.get()};
}
bool Renderer::Capture(const char* path) {
    try {
        auto image=ReadDisplay();if(image.pixels.empty())return false;
        std::ofstream out(path,std::ios::binary);out<<"P6\n"<<image.width<<" "<<image.height<<"\n255\n";
        for(auto p:image.pixels) { const char rgb[]={char(p>>16),char(p>>8),char(p)};out.write(rgb,3); }
        return bool(out);
    } catch(const std::exception& e) { std::fprintf(stderr,"Vulkan capture: %s\n",e.what());return false; }
}
void Renderer::Compare(const Submission& frame) {
    if(frame.reference_pixels.empty())return;
    try {
        auto& g=*impl_; g.Wait();
        if(!g.front || g.front->guest_w!=frame.reference_width || g.front->guest_h!=frame.reference_height)
            throw std::runtime_error("reference/front dimensions differ");
        g.ReleaseFrame(); g.Begin();
        auto promise=std::make_shared<std::promise<std::vector<uint32_t>>>(); auto future=promise->get_future();
        g.Readback(g.front,promise); g.Submit(false,0,0); auto actual=future.get();
        if(actual.size()!=frame.reference_pixels.size())throw std::runtime_error("reference/front payload differs");
        uint64_t error=0,different=0; unsigned maximum=0;
        auto save=[&](const char* path,const std::vector<uint32_t>& pixels) {
            std::ofstream out(path,std::ios::binary); out<<"P6\n"<<frame.reference_width<<" "<<frame.reference_height<<"\n255\n";
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
