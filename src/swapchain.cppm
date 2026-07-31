module;

#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <cassert>
#include <limits>
#include <print>
#include <stdexcept>
#include <tuple>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

export module swapchain;

import VulkanContext;
import types;

export class Swapchain {
public:
    Swapchain(VulkanContext const& context, GLFWwindow* window)
        : context_(context)
        , window_(window) {

        createSurface();
        createSwapchain();
    }
private:
    auto createSurface() -> void {
        VkSurfaceKHR _surface = VK_NULL_HANDLE;
        VkResult result = glfwCreateWindowSurface(*context_.getInstance(), window_, nullptr, &_surface);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("[ERR] Could not create surface!");
        }
        surface_ = vk::raii::SurfaceKHR(context_.getInstance(), _surface);
        std::println(stderr, "[LOG] Create window surface!");
    }

    auto getCapabilities() -> std::tuple<u32, vk::PresentModeKHR> {
        vk::SurfaceCapabilitiesKHR surfaceCapabilities = context_.getPhysicalDevice().getSurfaceCapabilitiesKHR(*surface_);

        // Check for image counts
        u32 minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
        u32 maxImageCount = surfaceCapabilities.maxImageCount;

        if ((0 < maxImageCount) && (minImageCount > maxImageCount)) {
            minImageCount = maxImageCount;
        }

        // choose surface format
        std::vector<vk::SurfaceFormatKHR> formats = context_.getPhysicalDevice().getSurfaceFormatsKHR(*surface_);
        auto formatItr = std::ranges::find_if(formats, [&](auto const& fmt) {
            // writing to framebuffer converts values written automatically to non linear sRGB
            // channel order. BGRA
            return fmt.format == vk::Format::eB8G8R8A8Srgb
            // tells the display pipeline how to interpret the colors being presented
            && fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
        surfaceFormat_ = (formatItr == formats.end()) ? formats[0] : *formatItr;

        //Extent
        // most os force swapchain to match the exact pixel size of the OS window.
        // If we do not follow this, this will result in validation error or swapchain creation failure
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
            // Brev CHECK IF WIDTH IS INFINITY NOT EXTENT YOU DUMB SHIT
            swapchainExtent_ = surfaceCapabilities.currentExtent;
        } else {
            // maxOS / moltenVK set extent to u32 max implying that the window manager does not care about the resolution we pick. So we choose our own.
            int width{}, height{};
            glfwGetFramebufferSize(window_, &width, &height);
            swapchainExtent_.width  = std::clamp<u32>(width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
            swapchainExtent_.height = std::clamp<u32>(height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
        }

        // Present modes
        std::vector<vk::PresentModeKHR> presentModes = context_.getPhysicalDevice().getSurfacePresentModesKHR(*surface_);
        // eFifo is required to be supported
        assert(std::ranges::any_of(presentModes, [](auto const& mode) {
            return mode == vk::PresentModeKHR::eFifo;
        }));

        auto presentMode = std::ranges::any_of(presentModes, [](auto const& mode) {
            return mode == vk::PresentModeKHR::eMailbox;
        }) ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;

        return {minImageCount, presentMode};
    }

    auto createSwapchain() -> void {
        // Physical device must have surfacce support
        assert(context_.getPhysicalDevice().getSurfaceSupportKHR(context_.getQueueIndex(), *surface_) && "[ERR] Physical Device does not have surface support!");

        vk::SurfaceCapabilitiesKHR surfaceCapabilities = context_.getPhysicalDevice().getSurfaceCapabilitiesKHR(*surface_);

        auto [minImageCount, presentModes] = getCapabilities();
        vk::SwapchainCreateInfoKHR swapchainCreateInfo{};
        swapchainCreateInfo
            .setSurface(*surface_)
            .setMinImageCount(minImageCount)
            .setImageFormat(surfaceFormat_.format)
            .setImageColorSpace(surfaceFormat_.colorSpace)
            .setImageExtent(swapchainExtent_)
            .setImageArrayLayers(1u)
            .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
            .setImageSharingMode(vk::SharingMode::eExclusive)
            .setPreTransform(surfaceCapabilities.currentTransform)
            .setClipped(vk::True)
            .setOldSwapchain(nullptr)
            // controls how alpha composition is handled by the window system
            // With eQpaque, the alpha channel is ignored and treated as though it contains constants 1.0
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque);
            // queueIndex and ptr are to be used when sharing more is concurrent

        std::println("Extent: {} | {}", swapchainExtent_.width, swapchainExtent_.height);
        swapchain_ = vk::raii::SwapchainKHR(context_.getLogicalDevice(), swapchainCreateInfo);
        swapchainImages_ = swapchain_.getImages();
        std::println(stderr, "[LOG] Created swapchain!");
    }
private:
    VulkanContext const& context_;
    GLFWwindow* window_ = nullptr;

    // the swapchain referes to the vulkan's view of the window
    vk::raii::SurfaceKHR surface_ = nullptr;
    vk::raii::SwapchainKHR swapchain_ = nullptr;
    std::vector<vk::Image> swapchainImages_{};

    vk::Extent2D swapchainExtent_{};
    vk::SurfaceFormatKHR surfaceFormat_{};
};
