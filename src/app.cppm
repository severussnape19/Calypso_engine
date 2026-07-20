module;

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
        pickPhysicalDevice();
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

    auto pickPhysicalDevice() -> void {

        vk::raii::PhysicalDevices physicalDevices(instance_);
        if (physicalDevices.empty()) {
            throw std::runtime_error("[ERR] No vulkan-compatible GPU found!");
        }
        auto dev = [&]() -> vk::raii::PhysicalDevice {
            for (auto dev : physicalDevices) {
                if (is_device_suitable(dev)) {
                    return dev;
                }
            }
            throw std::runtime_error("[ERR] Could not find a GPU with required specs");
        };
        physicalDevice_ = std::move(dev());
    }

    auto is_device_suitable(vk::raii::PhysicalDevice const& physical_dev) -> bool {

        auto supportsVulkan1_3 = physical_dev.getProperties().apiVersion >= vk::ApiVersion13;
        auto isDiscrete = physical_dev.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
        auto hasGeometryShader = physical_dev.getFeatures().geometryShader;

        auto queueFamilies = physical_dev.getQueueFamilyProperties();
        bool supportsGraphics =
            std::ranges::any_of(queueFamilies, [](auto const& qfp) -> bool {
                    return static_cast<bool>(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
            });

        auto availableExtensions = physical_dev.enumerateDeviceExtensionProperties();
        bool supportsRequiredExtensions = check(deviceExtensions, availableExtensions, &vk::ExtensionProperties::extensionName);

        if (supportsVulkan1_3 && isDiscrete && hasGeometryShader && supportsGraphics && supportsRequiredExtensions) {
            return true;
        }
        return false;
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
    std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    std::vector<const char*> deviceExtensions = {vk::KHRSwapchainExtensionName};

    vk::raii::Context context_{};
    vk::raii::Instance instance_{nullptr};
    vk::raii::DebugUtilsMessengerEXT debugMessenger_{nullptr};
    vk::raii::PhysicalDevice physicalDevice_{nullptr};
};
