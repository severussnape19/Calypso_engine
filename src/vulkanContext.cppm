module;

#include "vulkan/vulkan.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <print>
#include <stdexcept>
#include <string_view>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

export module VulkanContext;
import types;

/* This class owns all the mostly stable / static objects i.e things that exist once for the lifetime of the app.
 * Instance, physical device, logical device, debugMessenger creation, queue picking and command pool creation */
export class VulkanContext {
public:
    VulkanContext(bool enableValidation)
    : enableValidation_(enableValidation)
    , layers_(enableValidation_ ?
            std::vector<char const*>{"VK_LAYER_KHRONOS_validation"} : std::vector<char const*>{})
    {
        createInstance();
        createDebugMessenger();
        pickPhysicalDevice();
        createLogicalDevice();
        createCommandPool();
    }

    ~VulkanContext() = default;

    VulkanContext(VulkanContext const&) = delete;
    auto operator=(VulkanContext const&) = delete;

    [[nodiscard]] auto getInstance() const noexcept -> vk::raii::Instance const& { return instance_; }
    [[nodiscard]] auto getPhysicalDevice() const noexcept -> vk::raii::PhysicalDevice const& { return physicalDevice_; }
    [[nodiscard]] auto getLogicalDevice() const noexcept -> vk::raii::Device const& { return device_; }
    [[nodiscard]] auto getQueue() const noexcept -> vk::raii::Queue const& { return queue_; }
    [[nodiscard]] auto getCommandpool() const noexcept -> vk::raii::CommandPool const& { return commandPool_; }
    [[nodiscard]] auto getQueueIndex() const noexcept -> u32 { return queueIndex_; }
private:
    auto createInstance() -> void {
        // The vulkan context serves as the initial bootstrapping object that manages lifetimes of the dynamic loader
        // The vulkan instance aggregates all vulkan capable devices together each device then exposing one or more queues
        vk::ApplicationInfo appInfo{};
        appInfo
            .setPApplicationName("Vulkan")
            .setApplicationVersion(vk::makeVersion(0, 1, 0))
            .setPEngineName("Posseidon")
            .setEngineVersion(vk::makeVersion(0, 1, 0))
            .setApiVersion(VK_API_VERSION_1_3);

        u32 ext_count = 0;
        auto glfwInstanceExtensions = glfwGetRequiredInstanceExtensions(&ext_count);
        extensions_.assign(glfwInstanceExtensions, glfwInstanceExtensions + ext_count);
        extensions_.push_back(vk::EXTDebugUtilsExtensionName);
        extensions_.push_back("VK_KHR_surface");
        std::vector<vk::ExtensionProperties> availableExtensions = context_.enumerateInstanceExtensionProperties();

        if (!std::ranges::all_of(extensions_, [&availableExtensions](std::string_view required) -> bool {
            return std::ranges::any_of(availableExtensions, [required](std::string_view available) -> bool {
                return required == available;
            }, &vk::ExtensionProperties::extensionName);
        })) {
            throw std::runtime_error("[ERR] Could not find all the required instance extensions");
        }

        std::vector<vk::LayerProperties> availableLayers = context_.enumerateInstanceLayerProperties();

        if (!std::ranges::all_of(layers_, [&availableLayers](std::string_view required) -> bool {
            return std::ranges::any_of(availableLayers, [required](std::string_view available) -> bool {
                return required == available;
            }, &vk::LayerProperties::layerName);
        })) {
            throw std::runtime_error("[ERR] Could not find all the required instance layers");
        }

        vk::InstanceCreateInfo createInfo{};
        createInfo
            .setPApplicationInfo(&appInfo)
            .setEnabledLayerCount(enableValidation_ ? static_cast<u32>(layers_.size()) : 0)
            .setPpEnabledLayerNames(enableValidation_ ? layers_.data() : nullptr)
            .setEnabledExtensionCount(static_cast<u32>(extensions_.size()))
            .setPpEnabledExtensionNames(extensions_.data());

        instance_ = vk::raii::Instance(context_, createInfo);
        std::println(stderr, "[LOG] Instance created!");
    }

    static VKAPI_ATTR auto VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
        vk::DebugUtilsMessageTypeFlagsEXT             messageTypes,
        const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*                                       pUserData
    ) -> vk::Bool32 {
        if (messageSeverity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning ||
            messageSeverity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError   ||
            messageSeverity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo    ||
            messageTypes    == vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation ||
            messageTypes    == vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance ||
            messageTypes    == vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral ) {

            std::println(stderr, "[VULKAN] {}", pCallbackData->pMessage);
        }
        return vk::False; // controls weather the vulkan call that triggered it got aborted or not
    }

    auto createDebugMessenger() -> void {
        if (!enableValidation_) return;

        vk::Flags<vk::DebugUtilsMessageSeverityFlagBitsEXT> severityFlags =
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;

        vk::Flags<vk::DebugUtilsMessageTypeFlagBitsEXT> messageType =
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation;

        vk::DebugUtilsMessengerCreateInfoEXT createInfo {};
        createInfo
            .setFlags({})
            .setMessageSeverity(severityFlags)
            .setMessageType(messageType)
            .setPfnUserCallback(&debugCallback);

        debugMessenger_ = vk::raii::DebugUtilsMessengerEXT(instance_, createInfo);
        std::println(stderr, "[LOG] DebugMessenger created!");
    }

    auto pickPhysicalDevice() -> void {
        // Select the physical device based on required capabilities to make a logical device out of it
        vk::raii::PhysicalDevices physicalDevices(instance_);
        if (physicalDevices.empty()) {
            throw std::runtime_error("[ERR] could not find vulkan capable devices!");
        }

        auto const phyDev = std::ranges::find_if(physicalDevices, [&](auto const& device) {
            return isDeviceSuitable(device);
        });
        if (phyDev == physicalDevices.end()) {
            throw std::runtime_error("[ERR] Could not find suitable device!");
        }
        physicalDevice_ = *phyDev;
        std::println(stderr, "[LOG] Physical device found!");
    }

    auto isDeviceSuitable(vk::raii::PhysicalDevice physicalDevice) -> bool {
        auto physicalDeviceProperties = physicalDevice.getProperties2().properties;
        bool version1_3_support = physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_3;
        bool isDescrete = physicalDeviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;

        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        bool hasComputeBit = std::ranges::any_of(queueFamilyProperties, [](auto const& qfp) {
            return !!(qfp.queueFlags & vk::QueueFlagBits::eCompute);
        });

        bool hasGraphicsBit = std::ranges::any_of(queueFamilyProperties, [](auto const& qfp) {
            return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
        });

        auto features = physicalDevice.template getFeatures2<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();

        // extendedDynamicState - allows me to change culling, depth and topology on the fly rather than having me create new pipeline through the frame buffer
        // shaderDrawParameters - for GPU-driven rendering / batching
        // dynamicRendering - Cuts out needing renderpass and framebuffers

        bool supportsRequiredFeatures =
            features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        if (version1_3_support && isDescrete && hasComputeBit && hasGraphicsBit && supportsRequiredFeatures) {
            return true;
        }
        return false;
    }

    auto createLogicalDevice() -> void {
        // Since I now have a physical device, I create a logical device out of it and pull out required queues
        auto queueFamilyProperties = physicalDevice_.getQueueFamilyProperties();

        for (u32 i = 0; i < queueFamilyProperties.size(); i++) {
            // Check for presentation capability in swapchain module
            if (queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute &&
                queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics)
            {
                queueIndex_ = i;
                break;
            }
        }

        if (queueIndex_ == ~0) {
            throw std::runtime_error("[ERR] Could not find queues with required props");
        }

        std::vector<f32> queuePriorities = {0.5f};
        vk::DeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo
            .setQueueFamilyIndex(queueIndex_)
            .setQueueCount(1u)
            .setPQueuePriorities(queuePriorities.data());

        vk::StructureChain<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain{};

        featureChain.template get<vk::PhysicalDeviceVulkan11Features>().setShaderDrawParameters(vk::True);
        featureChain.template get<vk::PhysicalDeviceVulkan13Features>().setDynamicRendering(vk::True);
        // Makes CPU side code much less error prone. gives pipelineBarrier2, submitInfo2, semaphoreInfo
        featureChain.template get<vk::PhysicalDeviceVulkan13Features>().setSynchronization2(vk::True);
        featureChain.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().setExtendedDynamicState(vk::True);

        vk::DeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo
            .setQueueCreateInfoCount(1u)
            .setPQueueCreateInfos(&queueCreateInfo)
            .setPNext(&featureChain.template get<vk::PhysicalDeviceFeatures2>())
            .setEnabledExtensionCount(static_cast<u32>(deviceExtensions_.size()))
            .setPpEnabledExtensionNames(deviceExtensions_.data());

        device_ = vk::raii::Device(physicalDevice_, deviceCreateInfo);
        queue_  = vk::raii::Queue(device_, queueIndex_, 0);
        std::println(stderr, "[LOG] Created physical device and queue!");
    }

    auto createCommandPool() -> void {
        vk::CommandPoolCreateInfo createInfo{};
        createInfo
            .setQueueFamilyIndex(queueIndex_)
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

        commandPool_ = vk::raii::CommandPool(device_, createInfo);
        std::println(stderr, "[LOG] Created command pool!");
    }

private:
    vk::raii::Context context_{};
    vk::raii::Instance               instance_       = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger_ = nullptr;
    vk::raii::PhysicalDevice         physicalDevice_ = nullptr;
    vk::raii::Device                 device_         = nullptr;
    vk::raii::Queue                  queue_          = nullptr;
    vk::raii::CommandPool            commandPool_    = nullptr;
    u32 queueIndex_ = ~0;

    bool enableValidation_{};
    std::vector<char const*> layers_{};
    std::vector<char const*> extensions_{};
    std::vector<char const*> deviceExtensions_{vk::KHRSwapchainExtensionName};
};
