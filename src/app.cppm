module;

#include <fstream>
#include <ios>
#include <limits>
#include <string_view>
#include <vector>
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
            drawFrame();
        }
        device_.waitIdle();
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
        createCommandPool();
        createCommandBuffer();
        createSyncObjects();
    }

    auto create_instance() -> void {
        vk::ApplicationInfo appInfo {
            "Vulkan app",
            VK_MAKE_VERSION(0, 1, 0),
            "nyx",
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

        queueIndex_ = queueFamilyIndex;

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
        featureChain.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = VK_TRUE;
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
        deviceCreateInfo.pNext = &featureChain.template get<vk::PhysicalDeviceFeatures2>();

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

    auto createDepthImage() -> void {
        vk::ImageCreateInfo depthImageInfo{};
        depthImageInfo
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eD32Sfloat)
            .setExtent({swapchainExtent.width, swapchainExtent.height, 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal) // eOptimal used for only GPU images. Lets the GPU use whatever internal layout is fastest for its texture units
            .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);
        
        depthImage_ = vk::raii::Image(device_, depthImageInfo);

        vk::MemoryRequirements memReqs = depthImage_.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo
            .setAllocationSize(memReqs.size)
            .setMemoryTypeIndex(findMemoryType(
                memReqs.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            ));

        depthMemory_ = vk::raii::DeviceMemory(device_, allocInfo);
        depthImage_.bindMemory(depthMemory_, 0);
    }

    auto createDepthImageView() -> void {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo
            .setImage(depthImage_)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eD32Sfloat)
            .setSubresourceRange({
                vk::ImageAspectFlagBits::eDepth,
                0, 1, 0, 1
            });
        depthImageView_ = vk::raii::ImageView(device_, viewInfo);
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

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{vertShaderStageInfo, fragShaderStageInfo};
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{}; // format of the input vertices

        vk::PipelineInputAssemblyStateCreateInfo inputAsmSatecreateInfo{};
        inputAsmSatecreateInfo
            .setTopology(vk::PrimitiveTopology::eTriangleList);

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState
            .setViewportCount(1u)
            .setScissorCount(1u);

        // So that we can change the values dynamically later
        std::vector<vk::DynamicState> dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState
            .setDynamicStateCount(static_cast<u32>(dynamicStates.size()))
            .setPDynamicStates(dynamicStates.data());

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer
            .setDepthClampEnable(vk::False)
            .setRasterizerDiscardEnable(vk::False)
            .setPolygonMode(vk::PolygonMode::eFill) // determines how fragments are generated for geometry
            .setCullMode(vk::CullModeFlagBits::eBack) // backward culling
            .setFrontFace(vk::FrontFace::eClockwise) // which vertex ordering to be considered front facing
            .setDepthBiasClamp(vk::False)
            .setLineWidth(1.0f);

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling
            .setRasterizationSamples(vk::SampleCountFlagBits::e1)
            .setSampleShadingEnable(vk::False);

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment
            .setBlendEnable(vk::False)
            .setColorWriteMask(
                vk::ColorComponentFlagBits::eR |
                vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB |
                vk::ColorComponentFlagBits::eA
            );

        vk::PipelineColorBlendStateCreateInfo colorBlending{
            {},
            vk::False,
            vk::LogicOp::eCopy,
            1u,
            &colorBlendAttachment,
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo
            .setSetLayoutCount(0u)
            .setPushConstantRangeCount(0u);
        pipelineLayout_ = vk::raii::PipelineLayout(device_, pipelineLayoutInfo);

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil
            .setDepthTestEnable(vk::True)
            .setDepthWriteEnable(vk::True)
            .setDepthCompareOp(vk::CompareOp::eLess)
            .setDepthBoundsTestEnable(vk::False)
            .setStencilTestEnable(vk::False);

        vk::GraphicsPipelineCreateInfo graphicsPipelineInfo{};
        graphicsPipelineInfo
            .setStageCount(2u)
            .setPStages(shaderStages.data())
            .setPVertexInputState(&vertexInputInfo)
            .setPInputAssemblyState(&inputAsmSatecreateInfo)
            .setPViewportState(&viewportState)
            .setPRasterizationState(&rasterizer)
            .setPMultisampleState(&multisampling)
            .setPColorBlendState(&colorBlending)
            .setPDepthStencilState(&depthStencil)
            .setPDynamicState(&dynamicState)
            .setLayout(pipelineLayout_)
            .setRenderPass(nullptr);

        vk::PipelineRenderingCreateInfo pipelineRenderingInfo{
            {},
            1u,
            &swapchainSurfaceFormat.format
        };

        vk::StructureChain<vk::GraphicsPipelineCreateInfo ,vk::PipelineRenderingCreateInfo>
            pipelineCreateInfoChain = {graphicsPipelineInfo, pipelineRenderingInfo};

        graphicsPipeline_ = vk::raii::Pipeline(
            device_,
            nullptr,
            pipelineCreateInfoChain.template get<vk::GraphicsPipelineCreateInfo>());

        std::println(stderr, "[LOG] graphics pipeline object created!");
    }

    auto createCommandPool() -> void {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
            .setQueueFamilyIndex(queueIndex_);
        commandPool_ = vk::raii::CommandPool(device_, poolInfo);
    }

    auto createCommandBuffer() -> void {
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo
            .setCommandPool(commandPool_)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1u);
        commandBuffer_ = std::move(vk::raii::CommandBuffers(device_, allocInfo).front());
    }

    // We tell the GPU what to do per frame
    auto recordCommandBuffer(u32 imageIndex) -> void {
        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit); // sumbit once and then re-record
        commandBuffer_.begin(beginInfo);

        transition_image_layout(
            imageIndex, 
            vk::ImageLayout::eUndefined, 
            vk::ImageLayout::eColorAttachmentOptimal, 
            vk::AccessFlags2{}, 
            vk::AccessFlags2{vk::AccessFlagBits2::eColorAttachmentWrite}, 
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eTopOfPipe}, 
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eColorAttachmentOutput});

        vk::RenderingAttachmentInfo colorAttachmentInfo{};
        colorAttachmentInfo
            .setImageView(*swapchainImageViews[imageIndex])
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear) // clear image after use
            .setClearValue(vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.f, 0.f, 0.f, 1.f}}});

        vk::RenderingAttachmentInfo depthAttachmentInfo{};

        vk::RenderingInfo renderingInfo{};
        renderingInfo
            .setRenderArea(vk::Rect2D{{0, 0}, swapchainExtent})
            .setLayerCount(1u)
            .setColorAttachmentCount(1u)
            .setPColorAttachments(&colorAttachmentInfo);
       
        // BEGIN
        commandBuffer_.beginRendering(renderingInfo);

        // Since the pipeline object does not bake the below values in and are dynamic, we should specify them here
        vk::Viewport viewport{
            0.f, 1.f,
            static_cast<f32>(swapchainExtent.width),
            static_cast<f32>(swapchainExtent.height),
            0.f, 1.f
        };
        commandBuffer_.setViewport(0, viewport);

        vk::Rect2D scissor{{0, 0}, swapchainExtent};
        commandBuffer_.setScissor(0, scissor);
        commandBuffer_.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline_);

        commandBuffer_.draw(3, 1, 0, 0);
        commandBuffer_.endRendering();

        transition_image_layout(
            imageIndex, 
            vk::ImageLayout::eColorAttachmentOptimal, 
            vk::ImageLayout::ePresentSrcKHR, 
            vk::AccessFlags2{vk::AccessFlagBits2::eColorAttachmentWrite},
            vk::AccessFlags2{}, 
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eColorAttachmentOutput}, 
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eBottomOfPipe});
        
        commandBuffer_.end();
    }

    
    auto createSyncObjects() -> void {
        vk::SemaphoreCreateInfo semaphoreInfo{};
        vk::FenceCreateInfo fenceInfo{};
        fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
        
        for (usize i = 0; i < swapchainImages.size(); ++i) {
            imageAvailableSemaphores_.emplace_back(device_, semaphoreInfo);
        }

        for (usize i = 0; i < swapchainImages.size(); ++i) {
            renderFinishedSemaphores_.emplace_back(device_, semaphoreInfo);
        }

        inflightFence_ = vk::raii::Fence(device_, fenceInfo);
    }

    auto drawFrame() -> void {
        // blocks the CPU until the GPU signals the fence.
        auto result = device_.waitForFences(*inflightFence_, vk::True, std::numeric_limits<u32>::max());
        device_.resetFences(*inflightFence_);

        // Acquire swapchain image
        auto [acquireResult, imageIndex] = swapchain_.acquireNextImage(
            std::numeric_limits<u32>::max(),
            *imageAvailableSemaphores_[currentFrame_],
            nullptr
        );

        // Reset and record command buffer
        commandBuffer_.reset();
        recordCommandBuffer(imageIndex);

        // Submit
        vk::SemaphoreSubmitInfo waitSemaphoreInfo{};
        waitSemaphoreInfo
            .setSemaphore(*imageAvailableSemaphores_[currentFrame_])
            .setStageMask(vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eColorAttachmentOutput});

        vk::SemaphoreSubmitInfo signalSemaphoreInfo{};
        signalSemaphoreInfo
            .setSemaphore(*renderFinishedSemaphores_[imageIndex]);
        vk::CommandBufferSubmitInfo cmdSubmitInfo{};
        cmdSubmitInfo.setCommandBuffer(*commandBuffer_);

        vk::SubmitInfo2 submitInfo{};
        submitInfo
            .setWaitSemaphoreInfos(waitSemaphoreInfo)
            .setCommandBufferInfos(cmdSubmitInfo)
            .setSignalSemaphoreInfos(signalSemaphoreInfo);

        queue_.submit2(submitInfo, *inflightFence_);

        vk::PresentInfoKHR presentInfo{};
        presentInfo
            .setWaitSemaphores(*renderFinishedSemaphores_[imageIndex])
            .setSwapchains(*swapchain_)
            .setImageIndices(imageIndex);
        result = queue_.presentKHR(presentInfo);

        currentFrame_ = (currentFrame_ + 1) % swapchainImages.size();
    }

private:
    auto findMemoryType(u32 typeBits, vk::MemoryPropertyFlags properties) -> u32 {
        vk::PhysicalDeviceMemoryProperties memProps = physicalDevice_.getMemoryProperties();

        for (u32 i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeBits & (1 << i)) && 
                (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("Failed to find find suitable memory type");
    }
    
    // We use this function to transition the image before and after rendering
    auto transition_image_layout(
        u32 imageIndex,
        vk::ImageLayout         old_layout,      vk::ImageLayout         new_layout,
        vk::AccessFlags2        src_access_mask, vk::AccessFlags2        dst_access_mask,
        vk::PipelineStageFlags2 src_stage_mask,  vk::PipelineStageFlags2 dst_stage_mask
    ) -> void {
        /* before we start rendering to an image, we need to transition its layout to one that is suitable for rendeing*/
        vk::ImageMemoryBarrier2 barrier{};
        barrier
            .setSrcStageMask(src_stage_mask)
            .setDstStageMask(dst_stage_mask)
            .setSrcAccessMask(src_access_mask)
            .setDstAccessMask(dst_access_mask)
            .setOldLayout(old_layout)
            .setNewLayout(new_layout)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(swapchainImages[imageIndex])
            .setSubresourceRange({
                vk::ImageAspectFlagBits::eColor,
                0,
                1,
                0,
                1
            });

        vk::DependencyInfo dependencyInfo{};
        dependencyInfo
            .setDependencyFlags({})
            .setImageMemoryBarrierCount(1u)
            .setPImageMemoryBarriers(&barrier);

        commandBuffer_.pipelineBarrier2(dependencyInfo);
    }

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
    u32 queueIndex_{};
    vk::raii::Device device_{nullptr};

    vk::raii::SwapchainKHR swapchain_{nullptr};
    std::vector<vk::Image> swapchainImages{};
    vk::SurfaceFormatKHR swapchainSurfaceFormat{};
    vk::Extent2D swapchainExtent{};

    std::vector<vk::raii::ImageView> swapchainImageViews{};
    vk::raii::Image depthImage_{nullptr};
    vk::raii::DeviceMemory depthMemory_{nullptr};
    vk::raii::ImageView depthImageView_{nullptr};

    vk::raii::PipelineLayout pipelineLayout_{nullptr};
    vk::raii::Pipeline graphicsPipeline_{nullptr};

    // Manages the memory that is used to store the buffers.
    // Command buffers are allocated from here.
    vk::raii::CommandPool commandPool_{nullptr};
    vk::raii::CommandBuffer commandBuffer_{nullptr};

    std::vector<vk::raii::Semaphore> imageAvailableSemaphores_{};
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores_{};
    vk::raii::Fence     inflightFence_{nullptr};
    u32 currentFrame_{};
};