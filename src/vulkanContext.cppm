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

export module device;
import types;

export class VulkanContext {
public:
    VulkanContext(bool enableValidation)
    : enableValidation_(enableValidation) {
        createInstance();
    }

    ~VulkanContext() = default;

    VulkanContext(VulkanContext const&) = delete;
    auto operator=(VulkanContext const&) = delete;

    [[nodiscard]] auto getInstance() const noexcept -> vk::raii::Instance const& { return instance_; }
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
        std::vector<vk::ExtensionProperties> availableExtensions = context_.enumerateInstanceExtensionProperties();

        if (!std::ranges::all_of(extensions_, [&availableExtensions](std::string_view required) -> bool {
            return std::ranges::any_of(availableExtensions, [required](std::string_view available) -> bool {
                return required == available;
            }, &vk::ExtensionProperties::extensionName);
        })) {
            throw std::runtime_error("[ERR] Could not find all the required instance extensions");
        }

        std::vector<vk::LayerProperties> availableLayers = context_.enumerateInstanceLayerProperties();

        if (!std::ranges::all_of(layers_, [&availableLayers](std::string_view required) {
            return std::ranges::any_of(availableLayers, [required](std::string_view available) {
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
            return qfp.queueFlags == vk::QueueFlagBits::eCompute;
        });

        bool hasGraphicsBit = std::ranges::any_of(queueFamilyProperties, [](auto const& qfp) {
            return qfp.queueFlags == vk::QueueFlagBits::eGraphics;
        });

        auto features = physicalDevice.template getFeatures2<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan12Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>(); // allows me to change culling, depth and topology on the fly rather than having me create new pipeline

        bool supportsRequiredFeatures =
            features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
            features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

        if (version1_3_support && isDescrete && hasComputeBit && hasGraphicsBit && supportsRequiredFeatures) {
            return true;
        }
        return false;
    }

private:
    vk::raii::Context context_{};
    vk::raii::Instance       instance_       = nullptr;
    vk::raii::PhysicalDevice physicalDevice_ = nullptr;
    vk::raii::Queue          queue_          = nullptr;
    vk::raii::CommandPool    commandPool_    = nullptr;
    u32 queueIndex_ = 0;

    std::vector<char const*> layers_{"VK_LAYER_KHRONOS_validation"};
    std::vector<char const*> extensions_{};
    bool enableValidation_{};
};
