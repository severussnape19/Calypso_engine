#include <cstdlib>
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <print>

import types;
import Context;
import swapchain;
import pipeline;
import frameRenderer;
import mesh;

auto main(i32 argc, char* argv[]) -> i32 {
    try {
        if (!glfwInit()) {
            throw std::runtime_error("[ERR] Could not create a window!");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        GLFWwindow* window = glfwCreateWindow(800, 600, "vulkan", nullptr, nullptr);

        if (!window) {
            glfwTerminate();
            throw std::runtime_error("[ERR] Could not create a window!");
        }

        VulkanContext context(true);
        Swapchain swapchain(context, window);
        Mesh mesh(context);
        Pipeline pipeline(context, swapchain, mesh);
        FrameRenderer app(context, swapchain, pipeline, window, mesh);
        glfwSetWindowUserPointer(window, &app);
        glfwSetFramebufferSizeCallback(window, *app.framebufferResizeCallback);
        app.run();

        glfwDestroyWindow(window);
        glfwTerminate();
    } catch (std::exception const& e) {
        std::println(stderr, "[ERR] Error: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
