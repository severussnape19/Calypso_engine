module;

#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <cassert>
#include <limits>
#include <print>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

export module swapchain;

import Context;
import types;

export class Swapchain {
public:
    explicit Swapchain(VulkanContext const& context, GLFWwindow* window)
        : context_(context)
        , window_(window) {
            createSurface();
            createSwapchain();
            createImageViews();
            createDepthImage();
            createDepthImageView();
        }

    Swapchain(Swapchain const& o) = delete;
    auto operator=(Swapchain const& o) -> Swapchain& = delete;

    [[nodiscard]] auto getExtent()         const noexcept -> vk::Extent2D                            { return swapchainExtent_; }
    [[nodiscard]] auto getSurfaceFormat()  const noexcept -> vk::SurfaceFormatKHR                    { return surfaceFormat_; }
    [[nodiscard]] auto getSwapchain()      const noexcept -> vk::raii::SwapchainKHR const&           { return swapchain_; }
    [[nodiscard]] auto getImages()         const noexcept -> std::vector<vk::Image> const&           { return swapchainImages_; }
    [[nodiscard]] auto getImages()         noexcept -> std::vector<vk::Image>&                       { return swapchainImages_; }
    [[nodiscard]] auto getImageViews()     const noexcept -> std::vector<vk::raii::ImageView> const& { return swapchainImageViews_; }
    [[nodiscard]] auto getDepthImageView() const noexcept -> vk::raii::ImageView const&              { return depthImageView_; }
    [[nodiscard]] auto getDepthImage()     const noexcept -> vk::raii::Image const&                  { return depthImage_; }

    [[nodiscard]] auto findMemoryType(u32 typeBits, vk::MemoryPropertyFlags props) -> u32 {
        vk::PhysicalDeviceMemoryProperties memProps = context_.getPhysicalDevice().getMemoryProperties();

        for (u32 i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
                return i;
            }
        }
        throw std::runtime_error("[ERR] Failed to find a suitable memory type!");
    }

    auto cleanupSwapchain() -> void {
        depthImageView_ = nullptr;
        depthImageMemory_ = nullptr;
        depthImage_ = nullptr;
        swapchainImageViews_.clear();
        swapchain_ = nullptr;
    }

    auto recreateSwapchain() -> void {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(window_, &width, &height);
            glfwWaitEvents();
        }
        /* We would have the necessity to recreate the swapchain for example if there has been a change
           in the window size i.e window resize, we should catch that event and recreate the swap chain*/
        context_.getLogicalDevice().waitIdle();

        vk::raii::SwapchainKHR oldSwapchain = std::move(swapchain_);

        cleanupSwapchain(); // clear all the image views and cleanup the old swapchain
        createSwapchain(&oldSwapchain);
        createImageViews(); // we create views on the images that the swapchain creates
        createDepthImage();
        createDepthImageView();
    }

private:
    auto createSurface() -> void {
        VkSurfaceKHR _surface = VK_NULL_HANDLE;
        VkResult     result   = glfwCreateWindowSurface(*context_.getInstance(), window_, nullptr, &_surface);
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
            // Brev CHECK IF WIDTH / HEIGHT IS INFINITY NOT EXTENT YOU DUMB SHIT
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

    auto createSwapchain(vk::raii::SwapchainKHR const* oldSwapchain = nullptr) -> void {
        // Physical device must have surfacce support
        assert(context_.getPhysicalDevice().getSurfaceSupportKHR(context_.getGraphicsQueueIndex(), *surface_) && "[ERR] Physical Device does not have surface support!");

        vk::SurfaceCapabilitiesKHR surfaceCapabilities = context_.getPhysicalDevice().getSurfaceCapabilitiesKHR(*surface_);

        auto [minImageCount, presentModes] = getCapabilities();
        vk::SwapchainCreateInfoKHR swapchainCreateInfo{};
        swapchainCreateInfo
            .setPresentMode(presentModes)
            .setSurface(*surface_)
            .setMinImageCount(minImageCount)
            .setImageFormat(surfaceFormat_.format)
            .setImageColorSpace(surfaceFormat_.colorSpace)
            .setImageExtent({
                swapchainExtent_.width,
                swapchainExtent_.height
            })
            .setImageArrayLayers(1u)
            .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
            .setImageSharingMode(vk::SharingMode::eExclusive) // assuming presentQueueIndex is the same as graphics
            .setPreTransform(surfaceCapabilities.currentTransform)
            .setClipped(vk::True)
            .setOldSwapchain((oldSwapchain && **oldSwapchain) ? **oldSwapchain : nullptr)
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque);
            // queueIndex and ptr are to be used when sharing mode is concurrent

        swapchain_       = vk::raii::SwapchainKHR(context_.getLogicalDevice(), swapchainCreateInfo);
        swapchainImages_ = swapchain_.getImages();
        std::println(stderr, "[LOG] Created swapchain!");
    }

    auto createImageViews() -> void {
        vk::ImageViewCreateInfo imageViewCreateInfo{};
        imageViewCreateInfo
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(surfaceFormat_.format)
            .setComponents(vk::ComponentMapping({})) // Identity / default swizzle components
            .setSubresourceRange({
                vk::ImageAspectFlagBits::eColor,
                0,
                1,
                0,
                1
            });

        swapchainImageViews_.reserve(swapchainImages_.size());
        for (auto& image : swapchainImages_) {
            imageViewCreateInfo.setImage(image);
            swapchainImageViews_.emplace_back(std::move(vk::raii::ImageView(context_.getLogicalDevice(), imageViewCreateInfo)));
        }
        std::println(stderr, "[LOG] Created swapchain image views");
    }

    auto createDepthImage() -> void {
        vk::ImageCreateInfo createInfo{};
        createInfo
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eD32Sfloat)
            .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
            .setArrayLayers(1u)
            .setMipLevels(1u)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setExtent({swapchainExtent_.width, swapchainExtent_.height, 1u}); // vulkan expects depth to be greater than 1

        depthImage_ = vk::raii::Image(context_.getLogicalDevice(), createInfo);

        // create memory for the depth buffer
        vk::MemoryRequirements memReqs = depthImage_.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo
            .setAllocationSize(memReqs.size)
            .setMemoryTypeIndex(findMemoryType(
                memReqs.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            )); // since depth buffer is gonna reside in the device

        depthImageMemory_ = vk::raii::DeviceMemory(context_.getLogicalDevice(), allocInfo);
        depthImage_.bindMemory(depthImageMemory_, 0);
        std::println(stderr, "[LOG] Created Depth Image!");
    }

    auto createDepthImageView() -> void {
        vk::ImageViewCreateInfo createInfo{};
        createInfo
            .setImage(depthImage_)
            .setFormat(vk::Format::eD32Sfloat)
            .setViewType(vk::ImageViewType::e2D)
            .setSubresourceRange({
                vk::ImageAspectFlagBits::eDepth,
                0,
                1,
                0,
                1
            });
        depthImageView_ = vk::raii::ImageView(context_.getLogicalDevice(), createInfo);
        std::println(stderr, "[LOG] Created Depth image view!");
    }
private:
    VulkanContext const& context_;
    GLFWwindow* window_ = nullptr;

    // the swapchain referes to the vulkan's view of the window
    vk::raii::SurfaceKHR   surface_   = nullptr;
    vk::raii::SwapchainKHR swapchain_ = nullptr;
    std::vector<vk::Image> swapchainImages_{};
    std::vector<vk::raii::ImageView> swapchainImageViews_{};

    vk::raii::Image depthImage_ = nullptr;
    vk::raii::DeviceMemory depthImageMemory_ = nullptr;
    vk::raii::ImageView depthImageView_ = nullptr;

    vk::Extent2D         swapchainExtent_{};
    vk::SurfaceFormatKHR surfaceFormat_{};
};
