module;

#include "vulkan/vulkan.hpp"
#include <GLFW/glfw3.h>
#include <limits>
#include <stdexcept>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <print>
export module frameRenderer;

import types;
import context;
import swapchain;
import pipeline;

export class FrameRenderer {
public:
    explicit FrameRenderer(VulkanContext const& context, Swapchain & swapchain, Pipeline const& pipeline, GLFWwindow* window)
        : context_(context)
        , swapchain_(swapchain)
        , pipeline_(pipeline)
        , window_(window)
    {
        createCommandBuffer();
        createSyncObjects();
    }

    auto run() -> void {
        while (!glfwWindowShouldClose(window_)) {
            glfwPollEvents();
            drawFrame();
        }
        context_.getLogicalDevice().waitIdle();
    }

    static auto framebufferResizeCallback(GLFWwindow* window, int width, int height) -> void {
        auto app = reinterpret_cast<FrameRenderer*>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }
private:
    auto createCommandBuffer() -> void {
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo
            .setCommandPool(context_.getCommandpool())
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(static_cast<u32>(swapchain_.getImages().size()));
        commandBuffer_ = std::move(vk::raii::CommandBuffers(context_.getLogicalDevice(), allocInfo));
        std::println("[LOG] Created command buffer!");
    }

    auto recordCommandBuffer(u32 curIdx, u32 imageIndex) -> void {
        auto& cmdBuf = commandBuffer_[curIdx];
        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        // Starts recording the command buffer
        commandBuffer_[curIdx].begin(beginInfo);

        // undefined -> colorAttachmentOptimal
        // GPU needs the image to te in colorAttachmentOptimal layout to render into it
        transition_image_layout(
            cmdBuf,
            swapchain_.getImages()[imageIndex],
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlags2{},
            vk::AccessFlags2{vk::AccessFlagBits2::eColorAttachmentWrite},
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eTopOfPipe},
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eColorAttachmentOutput}
        );

        vk::RenderingAttachmentInfo colorAttachmentInfo{};
        colorAttachmentInfo
            .setImageView(*swapchain_.getImageViews()[imageIndex])
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear) // clear image after use
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.f, 0.f, 0.f, 1.f}}});

        vk::RenderingAttachmentInfo depthAttachmentInfo{};
        depthAttachmentInfo
            .setImageView(swapchain_.getDepthImageView())
            .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eDontCare) // we dont need depth after the frame ends
            .setClearValue(vk::ClearValue{vk::ClearDepthStencilValue{1.f, 0}});

        vk::RenderingInfo renderingInfo{};
        renderingInfo
            .setRenderArea(vk::Rect2D{{0, 0}, swapchain_.getExtent()})
            .setLayerCount(1u)
            .setColorAttachmentCount(1u)
            .setPColorAttachments(&colorAttachmentInfo)
            .setPDepthAttachment(&depthAttachmentInfo);

        // Draw commands from here
        commandBuffer_[curIdx].beginRendering(renderingInfo);

        // Dynamic variables
        vk::Viewport viewport {
            0.f, 0.f,
            static_cast<f32>(swapchain_.getExtent().width),
            static_cast<f32>(swapchain_.getExtent().height),
            0.f, 1.f
        };
        commandBuffer_[curIdx].setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, swapchain_.getExtent()};
        commandBuffer_[curIdx].setScissor(0, scissor);
        commandBuffer_[curIdx].bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.getPipeline());

        commandBuffer_[curIdx].draw(3, 1, 0, 0);
        // End draw commands
        commandBuffer_[curIdx].endRendering();

        // Transistion image to present
        // colorAttachmentOptimal -> presentSrcKHR
        transition_image_layout(
            cmdBuf,
            swapchain_.getImages()[imageIndex],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlags2{vk::AccessFlagBits2::eColorAttachmentWrite},
            vk::AccessFlags2{},
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eColorAttachmentOutput},
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eBottomOfPipe});
        // End recording commands
        commandBuffer_[curIdx].end();
    }

    auto createSyncObjects() -> void {
        vk::SemaphoreCreateInfo semaphoreInfo{};
        vk::FenceCreateInfo fenceInfo{};
        fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);

        usize sz = swapchain_.getImages().size();
        // Creates a semaphore per swachain image.
        // Each of these will be signaled by 'acquireNextImage' at runtime when a swapchain image is ready to render into
        for (usize i = 0; i < sz; i++) {
            presentCompleteSemaphores_.emplace_back(context_.getLogicalDevice(), semaphoreInfo);
        }

        // Also creates a semaphore per swapchain image
        // each will be singlaed by 'queueSubmit' at runtime when rendering is done.
        // the presenter will then know it is safe to show the image
        for (usize i = 0; i < sz; i++) {
            renderFinishedSemaphores_.emplace_back(context_.getLogicalDevice(), semaphoreInfo);
            inflightFences_.emplace_back(context_.getLogicalDevice(), fenceInfo);
        }

        // CPU blocks on this at the start of each frame to ensure the previous frame's GPU work is done before recording into command buffer
        //inflightFences_ = vk::raii::Fence(context_.getLogicalDevice(), fenceInfo);
    }

    auto drawFrame() -> void {
        // CPU blocks here until GPU signals 'inflightfence_'.
        // 'inflightFence_' was submitted with previous frame''s 'queueSubmit2' - GPU signals it when it finishes executing that command buffer.
        // We only proceed after the signal which indicates that we can re-record the command buffers now as they are completely used by the GPU
        auto result = context_.getLogicalDevice().waitForFences(*inflightFences_[currentFrame_], vk::True, std::numeric_limits<u64>::max());
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("[ERR] Failed to wait for fence");
        }
        context_.getLogicalDevice().resetFences(*inflightFences_[currentFrame_]);

        // Returns the index of a free image from the image pool
        // the 'imageAvailableSemaphores_[currentFrame_]' semaphore gets signaled by the swapchain when the image is actually ready
        auto [acquireResult, imageIndex] = swapchain_.getSwapchain().acquireNextImage(
            std::numeric_limits<u64>::max(),
            *presentCompleteSemaphores_[currentFrame_],
            nullptr
        );

        if (acquireResult == vk::Result::eErrorOutOfDateKHR) {
            swapchain_.recreateSwapchain();
            return;
        }

        if (acquireResult != vk::Result::eSuccess && acquireResult != vk::Result::eSuboptimalKHR) {
            assert(acquireResult == vk::Result::eTimeout || acquireResult == vk::Result::eNotReady);
            throw std::runtime_error("[ERR] Failed to acquire swapchain result");
        }

        // only reset after we know we are going to submit work to the GPU
        context_.getLogicalDevice().resetFences(*inflightFences_[currentFrame_]);
        // Re-record fresh command buffers for the current frame.
        commandBuffer_[currentFrame_].reset();
        recordCommandBuffer(currentFrame_, imageIndex);

        // --- submit ---
        // WAIT
        vk::SemaphoreSubmitInfo waitSemaphoreInfo{};
        waitSemaphoreInfo
            .setSemaphore(*presentCompleteSemaphores_[currentFrame_])
            .setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput);

        // After exec, tell GPU that the present image is ready
        // Also tells the next frame's waitForFences' that is is safe to proceed
        vk::SemaphoreSubmitInfo signalSemaphoreInfo{};
        signalSemaphoreInfo
            .setSemaphore(*renderFinishedSemaphores_[imageIndex]);

        // set command buffers
        vk::CommandBufferSubmitInfo cmdSubmitInfo{};
        cmdSubmitInfo.setCommandBuffer(*commandBuffer_[currentFrame_]);

        vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        vk::SubmitInfo submitInfo{};
        submitInfo
            // Wait semaphores
            .setWaitSemaphoreCount(1u)
            .setPWaitSemaphores(&*presentCompleteSemaphores_[currentFrame_])
            .setPWaitDstStageMask(&waitDestinationStageMask)
            // Commandbufs
            .setCommandBufferCount(1u)
            .setPCommandBuffers(&*commandBuffer_[currentFrame_])
            // Signal Semaphores
            .setSignalSemaphoreCount(1u)
            .setPSignalSemaphores(&*renderFinishedSemaphores_[imageIndex])
            ;

        context_.getQueue().submit(submitInfo, *inflightFences_[currentFrame_]);
        // ---

        // Present frame
        // Tells the swapchain to present the Image at imageIdx.
        vk::PresentInfoKHR presentInfo{};
        presentInfo
            // wait semaphores
            .setWaitSemaphoreCount(1u)
            .setWaitSemaphores(*renderFinishedSemaphores_[imageIndex]) // waits on this
            .setSwapchainCount(1u)
            .setSwapchains(*swapchain_.getSwapchain())
            .setPImageIndices(&imageIndex);

        vk::Result presentResult{};
        try{
            presentResult = context_.getQueue().presentKHR(presentInfo);
        } catch (vk::OutOfDateKHRError const&) {
            presentResult = vk::Result::eErrorOutOfDateKHR;
        }

        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR) {
            swapchain_.recreateSwapchain();
        }
       currentFrame_ = (currentFrame_ + 1) % swapchain_.getImageViews().size();
    }

    auto transition_image_layout(
        vk::raii::CommandBuffer const& cmdBuf,
        vk::Image image,
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
            .setImage(image)
            .setSubresourceRange({
                vk::ImageAspectFlagBits::eColor,
                0,
                1,
                0,
                1
            });

        vk::DependencyInfo dependencyInfo{};
        dependencyInfo.setImageMemoryBarriers(barrier);

        cmdBuf.pipelineBarrier2(dependencyInfo);
    }

private:
    VulkanContext const& context_;
    Swapchain&           swapchain_;
    Pipeline      const& pipeline_;
    GLFWwindow*   window_ = nullptr;

    std::vector<vk::raii::CommandBuffer> commandBuffer_{};

//    vk::raii::Semaphore presentCompleteSemaphore_ = nullptr;
//    vk::raii::Semaphore renderFinishedSemaphore_  = nullptr;
    std::vector<vk::raii::Semaphore> presentCompleteSemaphores_{};
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores_{};
    std::vector<vk::raii::Fence>                  inflightFences_{};
    u32 currentFrame_{};
    bool framebufferResized = false;
};
