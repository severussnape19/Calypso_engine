module;

#include <fstream>
#include <ios>
#include <limits>
#include <string_view>
#define GLFW_INCLUDE_VULKAN
#include <algorithm>
#include <exception>
#include <print>
#include <vulkan/vk_platform.h>
#include "vulkan/vulkan.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

export module app;

import types;

export class App {
public:
    auto run() noexcept -> void {
        try {
            init_window();
            init_vulkan();
            main_loop();
        } catch (const std::exception& e) {
            std::println(stderr, "[ERR] Fatal error: {}", e.what());
        }
        cleanUp();
    }
private:
    auto init_window() -> void {
        if (!glfwInit()) {
            throw std::runtime_error("[ERR] Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);

        window_ = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan renderer", nullptr, nullptr);
        if (!window_) {
            throw std::runtime_error("[ERR] Could not create a window!");
        }
        glfwShowWindow(window_);
        glfwFocusWindow(window_);
    }

    auto main_loop() -> void {
        while (!glfwWindowShouldClose(window_)) {
            glfwPollEvents();
        }
    }

    auto cleanUp() -> void {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    auto init_vulkan() -> void {
        create_instance();
        createDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createGraphicsPipeline();
    }

    auto create_instance() -> void {
        vk::ApplicationInfo appInfo {
            "Vulkan app",
            VK_MAKE_VERSION(0, 1, 0),
            "cyclops",
            VK_MAKE_VERSION(0, 1, 0),
            VK_API_VERSION_1_3,
        };

        u32 ext_count{};
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&ext_count);
        std::vector<const char*> instanceExtensions(glfwExtensions, glfwExtensions + ext_count);
        instanceExtensions.push_back(vk::EXTDebugUtilsExtensionName);
        std::vector<vk::ExtensionProperties> availableExtensions = context_.enumerateInstanceExtensionProperties();

        if (!check(instanceExtensions, availableExtensions, &vk::ExtensionProperties::extensionName)) {
            throw std::runtime_error("[ERR] Could not find one or more required extensions!");
        }

        std::vector<vk::LayerProperties> availableLayers = context_.enumerateInstanceLayerProperties();
        if (!check(layers, availableLayers, &vk::LayerProperties::layerName)) {
            throw std::runtime_error("[ERR] Could not find one or more required layers!");
        }

        vk::InstanceCreateInfo createInfo {
            {},
            &appInfo,
            enable_validation_ ? static_cast<u32>(layers.size()) : 0,
            enable_validation_ ? layers.data() : nullptr,
            static_cast<u32>(instanceExtensions.size()),
            instanceExtensions.data(),
        };
        instance_ = std::move(vk::raii::Instance(context_, createInfo));
        std::println(stderr, "[LOG] Instance successfully created!");
    }

    static VKAPI_ATTR auto VKAPI_CALL debugCallback(
            vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
            vk::DebugUtilsMessageTypeFlagsEXT type,
            const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData
    ) -> vk::Bool32 {
        std::println(stderr, "Validation layer: msg: {}", pCallbackData->pMessage);
        return vk::False;
    }

    auto createDebugMessenger() -> void {

        if (!enable_validation_) return;

        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
        );

        vk::DebugUtilsMessageTypeFlagsEXT typeFlags(
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
        );

        vk::DebugUtilsMessengerCreateInfoEXT createInfo {
            {},
            severityFlags,
            typeFlags,
            &debugCallback,
        };
        debugMessenger_ = vk::raii::DebugUtilsMessengerEXT(instance_, createInfo);
        std::println(stderr, "[LOG] Debug Messenger created successfully created!");
    }

    auto createSurface() -> void {
        VkSurfaceKHR surface{};
        if (glfwCreateWindowSurface(*instance_, window_, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("[ERR] Could not create window surface!");
        }

        surface_ = vk::raii::SurfaceKHR(instance_, surface);
        std::println(stderr, "[LOG] Created window surface!");
    }

    auto is_device_suitable(vk::raii::PhysicalDevice const& physical_dev) -> bool {

        auto supportsVulkan1_3 = physical_dev.getProperties().apiVersion >= vk::ApiVersion13;
        auto isDiscrete = physical_dev.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu;

        auto queueFamilies = physical_dev.getQueueFamilyProperties();
        bool supportsGraphics =
            std::ranges::any_of(queueFamilies, [](auto const& qfp) -> bool {
                    return static_cast<bool>(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
            });

        auto availableExtensions = physical_dev.enumerateDeviceExtensionProperties();
        bool supportsRequiredExtensions = check(deviceExtensions, availableExtensions, &vk::ExtensionProperties::extensionName);

       auto features =
           physical_dev.template getFeatures2<vk::PhysicalDeviceFeatures2,
                                              vk::PhysicalDeviceVulkan11Features,
                                              vk::PhysicalDeviceVulkan13Features,
                                              vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
       bool supportsRequiredFeatures =
           features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
           features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
           features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        if (supportsVulkan1_3 && isDiscrete && supportsGraphics && supportsRequiredExtensions && supportsRequiredFeatures) {
            return true;
        }
        return false;
    }

    auto pickPhysicalDevice() -> void {
        vk::raii::PhysicalDevices physicalDevices(instance_);
        if (physicalDevices.empty()) {
            throw std::runtime_error("[ERR] No vulkan-compatible GPU found!");
        }

        auto const dev =
            std::ranges::find_if(physicalDevices, [&](auto const& physical_dev) {
                    return is_device_suitable(physical_dev);
            });

        if (dev == physicalDevices.end()) {
            throw std::runtime_error("[ERR] Could not find a GPU with required specs");
        }
        physicalDevice_ = *dev;
        std::println(stderr, "[LOG] Picked Physical Device!");
    }

    auto createLogicalDevice() -> void {

        std::vector <vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice_.getQueueFamilyProperties();

        u32 queueFamilyIndex = ~0; // 0xFFFFFFFF
        for (u32 qfIdx = 0; qfIdx < queueFamilyProperties.size(); ++qfIdx) {
            if (queueFamilyProperties[qfIdx].queueFlags & vk::QueueFlagBits::eGraphics &&
                queueFamilyProperties[qfIdx].queueFlags & vk::QueueFlagBits::eCompute  &&
                physicalDevice_.getSurfaceSupportKHR(qfIdx, *surface_))
            {
                queueFamilyIndex = qfIdx;
                break;
            }
        }

        if (queueFamilyIndex == ~0) {
            throw std::runtime_error("[ERR] Could not find any GPU with required properties / extensions");
        }

        f32 queuePriority = 0.5f;

        vk::DeviceQueueCreateInfo deviceQueueCreateInfo {
            {},
            queueFamilyIndex,
            1,
            &queuePriority,
        };

        vk::StructureChain<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain {};

        featureChain.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = VK_TRUE;
        featureChain.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = VK_TRUE;
        featureChain.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState = VK_TRUE;

        vk::DeviceCreateInfo deviceCreateInfo {
            {},
            1,
            &deviceQueueCreateInfo,
            {},
            {},
            static_cast<u32>(deviceExtensions.size()),
            deviceExtensions.data(),
        };
        deviceCreateInfo.pNext = &featureChain;

        device_ = vk::raii::Device(physicalDevice_, deviceCreateInfo);
        queue_  = vk::raii::Queue(device_, queueFamilyIndex, 0);
        std::println("[LOG] Created logical device and queue!");
    }

    auto choose_swapchain_surface_format(std::vector<vk::SurfaceFormatKHR> const& formats) -> vk::SurfaceFormatKHR
    {
        assert(!formats.empty());
        const auto formatIt = std::ranges::find_if(
            formats, [](const auto& fmt) {
                return fmt.format == vk::Format::eB8G8R8A8Srgb &&
                       fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
            }
        );
        return (formatIt == formats.end()) ? formats[0] : *formatIt;
    }

    auto choose_swapchain_presentation_mode(std::vector<vk::PresentModeKHR> const & availablePrsentModes) -> vk::PresentModeKHR
    {
        assert(std::ranges::any_of(availablePrsentModes, [](auto const & presentMode) -> bool
                {
                    return presentMode == vk::PresentModeKHR::eFifo;
                }
        )); // Since eFifo is guaranteed to be available

        return
            std::ranges::any_of(availablePrsentModes, [](auto const & presentMode) -> bool
            {
                return vk::PresentModeKHR::eMailbox == presentMode;
            }
            ) ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;
    }

    auto choose_swapchain_extent(vk::SurfaceCapabilitiesKHR const & capabilities) -> vk::Extent2D
    {
        if (capabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
            return capabilities.currentExtent;
        }

        int width{};
        int height{};
        glfwGetFramebufferSize(window_, &width, &height);

        return vk::Extent2D {
            std::clamp<u32>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<u32>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }

    auto choose_swapchain_min_image_count(vk::SurfaceCapabilitiesKHR const & surfaceCapabilities) -> u32
    {
        // Min image count 3 for triple buffering. 2 for double buffering.
        auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
        if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
            minImageCount = surfaceCapabilities.maxImageCount;
        }
        return minImageCount;
    }

    auto createSwapchain() -> void {

        vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice_.getSurfaceCapabilitiesKHR(*surface_);
        swapchainExtent = choose_swapchain_extent(surfaceCapabilities);
        u32 minImageCount = choose_swapchain_min_image_count(surfaceCapabilities);

        std::vector<vk::SurfaceFormatKHR> availableFormats = physicalDevice_.getSurfaceFormatsKHR(*surface_);
        swapchainSurfaceFormat = choose_swapchain_surface_format(availableFormats);

        std::vector<vk::PresentModeKHR> availablePrsentModes = physicalDevice_.getSurfacePresentModesKHR(*surface_);

        vk::SwapchainCreateInfoKHR createInfo{};
        createInfo
            .setSurface(*surface_)
            .setMinImageCount(minImageCount) // buffering
            .setImageFormat(swapchainSurfaceFormat.format)
            .setImageColorSpace(swapchainSurfaceFormat.colorSpace)
            .setImageExtent(swapchainExtent) // Dimensions of images in swap chain in pixels
            .setImageArrayLayers(1)
            .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
            .setImageSharingMode(vk::SharingMode::eExclusive) // how the images are shared across the queues
            .setPreTransform(surfaceCapabilities.currentTransform) // transformation prior presentation
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque) // uhh
            .setPresentMode(choose_swapchain_presentation_mode(availablePrsentModes)) // eMail or eFifo
            .setClipped(vk::True)
            .setOldSwapchain(nullptr);

        swapchain_ = vk::raii::SwapchainKHR(device_, createInfo);
        swapchainImages = swapchain_.getImages();
        std::println("[LOG] Created swapchain!");
    }

    auto createImageViews() -> void {
        vk::ImageViewCreateInfo viewCreateInfo{};
        viewCreateInfo
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(swapchainSurfaceFormat.format)
            .setSubresourceRange({
                vk::ImageAspectFlagBits::eColor,
                0,
                1,
                0,
                1}
            )// purpose of the image and which part of the image should be accessed
            .setComponents({
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
            }); // Used to swizzle around the color channels

        for (auto& image : swapchainImages) {
            viewCreateInfo.setImage(image);
            swapchainImageViews.emplace_back(device_, viewCreateInfo);
        }

        std::println(stderr, "[LOG] Image views created!");
    }

    auto createGraphicsPipeline() -> void {
        std::vector<char> shaderCode = load_shader("../shaders/slang.spv");
        vk::raii::ShaderModule shaderModule = createShaderModule(shaderCode);

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(shaderModule)
            .setPName("vertMain"); // vertex shader entrypoint

        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(shaderModule)
            .setPName("fragMain"); // fragment shader entrypoint

        vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    }

private:
    static auto load_shader(std::string const& filename) -> std::vector<char> {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("[ERR] Could not open the file!");
        }
        usize size = file.tellg();
        std::vector<char> buffer(size);

        file.seekg(0, std::ios::beg);
        file.read(buffer.data(), static_cast<std::streamsize>(size));

        file.close();
        return buffer;
    }

    [[nodiscard]]
    auto createShaderModule(std::vector<char> const& code) const -> vk::raii::ShaderModule {

        vk::ShaderModuleCreateInfo createInfo{};
        createInfo
            .setCodeSize(code.size() * sizeof(char))
            .setPCode(reinterpret_cast<const uint32_t*>(code.data()));
        vk::raii::ShaderModule shaderModule{device_, createInfo};

        return shaderModule;
    }

    auto check(auto required, auto available, auto projection) -> bool {
        return std::ranges::all_of(required, [&](std::string_view req) {
            return std::ranges::any_of(available, [req](std::string_view avl) {
                return req == avl;
            }, projection);
        });
    };

private:
    GLFWwindow* window_ { nullptr };

    static constexpr i32 WIDTH = 800;
    static constexpr i32 HEIGHT = 600;
#ifdef NDEBUG
    const bool enable_validation_ = false;
#else
    const bool enable_validation_ = true;
#endif

    std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};
    std::vector<const char*> extensions{};
    std::vector<const char*> deviceExtensions = {vk::KHRSwapchainExtensionName};

    vk::raii::Context context_{};
    vk::raii::Instance instance_{nullptr};
    vk::raii::DebugUtilsMessengerEXT debugMessenger_{nullptr};
    vk::raii::SurfaceKHR surface_{nullptr};

    vk::raii::PhysicalDevice physicalDevice_{nullptr};
    vk::raii::Queue queue_{nullptr};
    vk::raii::Device device_{nullptr};

    vk::raii::SwapchainKHR swapchain_{nullptr};
    std::vector<vk::Image> swapchainImages{};
    vk::SurfaceFormatKHR swapchainSurfaceFormat{};
    vk::Extent2D swapchainExtent{};

    std::vector<vk::raii::ImageView> swapchainImageViews{};
};
