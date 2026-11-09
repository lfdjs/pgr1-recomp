// PGR1 Portable 04A — native Vulkan WSI/device/swapchain proof of integration.
// This is NOT an Xbox draw backend. No CPU reference rasterizer, OpenGL, or Zink is used.
// SDL3 is used only for the window/surface/events; the GPU work is Vulkan 1.0.
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "vk_geometry.h"
#include "vk_guest_frame.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <cctype>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void Check(VkResult result, const char* action) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(action) + " failed, VkResult=" + std::to_string(int(result)));
    }
}

bool SupportsSwapchain(VkPhysicalDevice gpu) {
    std::uint32_t count = 0;
    Check(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr), "device extension count");
    std::vector<VkExtensionProperties> extensions(count);
    Check(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data()), "device extensions");
    return std::any_of(extensions.begin(), extensions.end(), [](const auto& e) {
        return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    });
}

class NativeVulkan {
public:
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = std::numeric_limits<std::uint32_t>::max();
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    // Each swapchain image retains its own render-finished semaphore. Reusing one
    // binary semaphore across successive presents is NOT portable: a present
    // wait can outlive the submission fence, especially on AMD/Mesa.
    std::vector<VkSemaphore> ready_by_image;
    VkFence fence = VK_NULL_HANDLE;
    bool resize = false;
    bool draw_fixture = false;
    std::unique_ptr<pgr::vk::Geometry> geometry;

    NativeVulkan() = default;
    NativeVulkan(const NativeVulkan&) = delete;
    NativeVulkan& operator=(const NativeVulkan&) = delete;
    ~NativeVulkan() {
        if (device) vkDeviceWaitIdle(device);
        geometry.reset();
        if (device && fence) vkDestroyFence(device, fence, nullptr);
        for(auto semaphore : ready_by_image) if(device && semaphore) vkDestroySemaphore(device,semaphore,nullptr);
        if (device && acquired) vkDestroySemaphore(device, acquired, nullptr);
        if (device && pool) vkDestroyCommandPool(device, pool, nullptr);
        if (device && swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        if (device) vkDestroyDevice(device, nullptr);
        if (instance && surface) SDL_Vulkan_DestroySurface(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        if (window) SDL_DestroyWindow(window);
    }

    void Init(bool test_geometry) {
        draw_fixture = test_geometry;
        window = SDL_CreateWindow(test_geometry ? "PGR1 Vulkan 04B - indexed GPU geometry test (NOT GAME)" :
                                  "PGR1 Vulkan 04A - native WSI test (NOT GAME)", 1100, 700,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
        Uint32 extension_count = 0;
        auto required = SDL_Vulkan_GetInstanceExtensions(&extension_count);
        if (!required || !extension_count) throw std::runtime_error(std::string("SDL Vulkan extensions: ") + SDL_GetError());
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "PGR1 portable Vulkan native WSI";
        application.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create.pApplicationInfo = &application;
        create.enabledExtensionCount = extension_count;
        create.ppEnabledExtensionNames = required;
        Check(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance");
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
            throw std::runtime_error(std::string("SDL Vulkan surface: ") + SDL_GetError());

        std::uint32_t n = 0;
        Check(vkEnumeratePhysicalDevices(instance, &n, nullptr), "GPU count");
        if (!n) throw std::runtime_error("No physical Vulkan device/ICD available");
        std::vector<VkPhysicalDevice> gpus(n);
        Check(vkEnumeratePhysicalDevices(instance, &n, gpus.data()), "GPU enumeration");
        // Prefer discrete GPUs, but a working graphics+present queue is mandatory.
        int best = -1;
        const char* preferred = std::getenv("PGR1_VK_DEVICE");
        bool preferred_found = false;
        for (auto candidate : gpus) {
            if (!SupportsSwapchain(candidate)) continue;
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(candidate, &props);
            if (preferred && *preferred) {
                std::string gpu_name = props.deviceName, match = preferred;
                for (char& c : gpu_name) c = char(std::tolower(static_cast<unsigned char>(c)));
                for (char& c : match) c = char(std::tolower(static_cast<unsigned char>(c)));
                if (gpu_name.find(match) == std::string::npos) continue;
                preferred_found = true;
            }
            std::uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
            for (std::uint32_t i = 0; i < count; ++i) {
                VkBool32 present = VK_FALSE;
                Check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "surface support");
                if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
                std::uint32_t formats = 0, modes = 0;
                Check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formats, nullptr), "formats count");
                Check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &modes, nullptr), "present modes count");
                if (!formats || !modes) continue;
                int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
                if (score <= best) continue;
                best = score;
                gpu = candidate;
                family = i;
            }
        }
        if (!gpu) {
            if (preferred && *preferred && !preferred_found)
                throw std::runtime_error(std::string("Vulkan GPU requested but not found: ") + preferred);
            throw std::runtime_error("No Vulkan GPU with graphics/present queue and swapchain support");
        }
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpu, &props);
        std::printf("NATIVE Vulkan device: %s, vendor=0x%x, device=0x%x, api=%u.%u.%u, queue=%u\n",
                    props.deviceName, props.vendorID, props.deviceID,
                    VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion), family);
        float priority = 1.0f;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dc.queueCreateInfoCount = 1;
        dc.pQueueCreateInfos = &q;
        dc.enabledExtensionCount = 1;
        dc.ppEnabledExtensionNames = extensions;
        Check(vkCreateDevice(gpu, &dc, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, family, 0, &queue);

        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pc.queueFamilyIndex = family;
        pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Check(vkCreateCommandPool(device, &pc, nullptr, &pool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = pool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        Check(vkAllocateCommandBuffers(device, &ca, &commands), "vkAllocateCommandBuffers");
        VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        Check(vkCreateSemaphore(device, &sc, nullptr, &acquired), "image acquired semaphore");
        // Present-ready semaphores are created per-image in Recreate().
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fc.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        Check(vkCreateFence(device, &fc, nullptr, &fence), "vkCreateFence");
        Recreate();
    }

    void Recreate() {
        // Synchronize before destroying old swapchain and its render pass/framebuffers.
        Check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle on resize");
        geometry.reset();
        for(auto semaphore:ready_by_image) vkDestroySemaphore(device,semaphore,nullptr);
        ready_by_image.clear();
        VkSurfaceCapabilitiesKHR caps{};
        Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps), "surface capabilities");
        const auto required_usage = draw_fixture ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (!(caps.supportedUsageFlags & required_usage))
            throw std::runtime_error(draw_fixture ? "Swapchain lacks COLOR_ATTACHMENT usage" :
                                                 "Swapchain lacks TRANSFER_DST usage");
        int window_width = 0, window_height = 0;
        SDL_GetWindowSizeInPixels(window, &window_width, &window_height);
        if (window_width <= 0 || window_height <= 0) { resize = true; return; }
        VkExtent2D chosen = caps.currentExtent;
        if (chosen.width == UINT32_MAX) {
            chosen.width = std::clamp(std::uint32_t(window_width), caps.minImageExtent.width, caps.maxImageExtent.width);
            chosen.height = std::clamp(std::uint32_t(window_height), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        std::uint32_t count = 0;
        Check(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr), "swapchain formats");
        std::vector<VkSurfaceFormatKHR> formats(count);
        Check(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data()), "swapchain formats data");
        if (!count) throw std::runtime_error("No swapchain formats");
        VkSurfaceFormatKHR selected = formats.front();
        if (formats.size() == 1 && selected.format == VK_FORMAT_UNDEFINED)
            selected.format = VK_FORMAT_B8G8R8A8_UNORM;
        for (const auto& candidate : formats) {
            if ((candidate.format == VK_FORMAT_B8G8R8A8_UNORM || candidate.format == VK_FORMAT_B8G8R8A8_SRGB) &&
                candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { selected = candidate; break; }
        }
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & alpha)) {
            for (auto bit : {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                             VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                             VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
                if (caps.supportedCompositeAlpha & bit) { alpha = bit; break; }
            }
        }
        std::uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
        VkSwapchainCreateInfoKHR si{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        si.surface = surface;
        si.minImageCount = image_count;
        si.imageFormat = selected.format;
        si.imageColorSpace = selected.colorSpace;
        si.imageExtent = chosen;
        si.imageArrayLayers = 1;
        si.imageUsage = required_usage;
        si.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        si.preTransform = caps.currentTransform;
        si.compositeAlpha = alpha;
        si.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        si.clipped = VK_TRUE;
        si.oldSwapchain = swapchain;
        VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
        Check(vkCreateSwapchainKHR(device, &si, nullptr, &new_swapchain), "vkCreateSwapchainKHR");
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = new_swapchain;
        format = selected.format;
        extent = chosen;
        std::uint32_t new_images = 0;
        Check(vkGetSwapchainImagesKHR(device, swapchain, &new_images, nullptr), "swapchain image count");
        images.resize(new_images);
        Check(vkGetSwapchainImagesKHR(device, swapchain, &new_images, images.data()), "swapchain images");
        VkSemaphoreCreateInfo create_semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for(std::size_t i=0;i<images.size();i++) {
            VkSemaphore sem=VK_NULL_HANDLE;
            Check(vkCreateSemaphore(device,&create_semaphore,nullptr,&sem),"present semaphore per image");
            ready_by_image.push_back(sem);
        }
        resize = false;
        if (draw_fixture) {
            geometry = std::make_unique<pgr::vk::Geometry>(gpu, device);
            geometry->Prepare(format,extent,images);
        }
        std::printf("Native Vulkan swapchain: %ux%u, %u images, VkFormat=%d, FIFO present\n",
                    extent.width, extent.height, new_images, int(format));
    }

    bool Frame(std::uint64_t index) {
        if (resize) { Recreate(); if (resize) return false; }
        Check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "wait previous GPU submission");
        std::uint32_t image_index = 0;
        VkResult acquire = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &image_index);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { Recreate(); return false; }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) Check(acquire, "vkAcquireNextImageKHR");
        Check(vkResetFences(device, 1, &fence), "reset fence");
        Check(vkResetCommandBuffer(commands, 0), "reset commands");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(commands, &begin), "begin GPU commands");
        if (draw_fixture) {
            auto sample=pgr::vk::Fixture(index,extent.width,extent.height);
            if(index==0) std::printf("Vulkan 04B indexed GPU test: %zu vertices, %zu indices, %zu vkCmdDrawIndexed batches\n",
                                     sample.vertices.size(),sample.indices.size(),sample.batches.size());
            geometry->Record(commands, image_index, sample);
        } else {
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;
        VkImageMemoryBarrier to_transfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // discard previous full-frame clear
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = images.at(image_index);
        to_transfer.subresourceRange = range;
        to_transfer.srcAccessMask = 0;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &to_transfer);
        // A GPU color-clear is intentionally all this test draws; it validates
        // the native Vulkan WSI path but does NOT render any PGR1 guest commands.
        const float wave = 0.5f + 0.5f * std::sin(float(index) * 0.03f);
        VkClearColorValue color{{0.05f + 0.18f * wave, 0.11f, 0.23f + 0.16f * wave, 1.0f}};
        vkCmdClearColorImage(commands, images.at(image_index), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
        VkImageMemoryBarrier to_present{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.image = images.at(image_index);
        to_present.subresourceRange = range;
        to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_present.dstAccessMask = 0;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &to_present);
        }
        Check(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
        VkPipelineStageFlags wait_stage = draw_fixture ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &ready_by_image.at(image_index);
        Check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &ready_by_image.at(image_index);
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image_index;
        VkResult result = vkQueuePresentKHR(queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR) {
            resize = true;
        } else Check(result, "vkQueuePresentKHR");
        return true;
    }
};
} // namespace

int main(int argc, char** argv) {
    unsigned frames = 180;
    bool geometry_test = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char* end = nullptr;
            unsigned long value = std::strtoul(argv[++i], &end, 10);
            if (!end || *end || !value || value > 100000) {
                std::fprintf(stderr, "--frames requires integer 1..100000\n"); return 2;
            }
            frames = unsigned(value);
        } else if (std::strcmp(argv[i], "--geometry-test") == 0) {
            geometry_test = true;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::puts("pgr1_vk_wsi [--frames 180] [--geometry-test]  # native Vulkan GPU fixture, NOT PGR1 game rendering"); return 0;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]); return 2;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init video: %s\n", SDL_GetError()); return 1;
    }
    int result = 0;
    try {
        NativeVulkan vk;
        vk.Init(geometry_test);
        bool running = true;
        unsigned completed = 0;
        auto start = std::chrono::steady_clock::now();
        while (running && completed < frames) {
            SDL_Event e{};
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) running = false;
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
                if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) vk.resize = true;
            }
            if (!running) break;
            if (vk.Frame(completed)) ++completed;
            SDL_Delay(16);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::printf("NATIVE Vulkan WSI OK: %u presented frames in %.3f s (%.1f presents/s)\n",
                    completed, seconds, seconds > 0 ? completed/seconds : 0);
        if(geometry_test) std::puts("NOTE: this is an indexed GPU fixture; it does NOT execute PGR1 Xbox Draw commands.");
        else std::puts("NOTE: this verifies Vulkan WSI only; no Xbox Draw, textures or NV2A shaders are executed.");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Native Vulkan WSI failed: %s\n", e.what());
        result = 1;
    }
    SDL_Quit();
    return result;
}
