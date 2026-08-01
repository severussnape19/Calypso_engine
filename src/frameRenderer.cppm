module;

#include "vulkan/vulkan.hpp"
#include <GLFW/glfw3.h>
#include <cstdint>
#include <limits>
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
    explicit FrameRenderer(VulkanContext const& context, Swapchain const& swapchain, Pipeline const& pipeline, GLFWwindow* window)
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
private:
    auto createCommandBuffer() -> void {
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo
            .setCommandPool(context_.getCommandpool())
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1u);
        commandBuffer_ = std::move(vk::raii::CommandBuffers(context_.getLogicalDevice(), allocInfo).front());
        std::println("[LOG] Created command buffer!");
    }

    auto recordFrameBuffer(u32 imageIndex) -> void {
        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        // Starts recording the command buffer
        commandBuffer_.begin(beginInfo);

        // undefined -> colorAttachmentOptimal
        // GPU needs the image to te in colorAttachmentOptimal layout to render into it
        transition_image_layout(
            imageIndex,
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
        commandBuffer_.beginRendering(renderingInfo);

        // Dynamic variables
        vk::Viewport viewport {
            0.f, 0.f,
            static_cast<f32>(swapchain_.getExtent().width),
            static_cast<f32>(swapchain_.getExtent().height),
            0.f, 1.f
        };
        commandBuffer_.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, swapchain_.getExtent()};
        commandBuffer_.setScissor(0, scissor);
        commandBuffer_.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline_.getPipeline());

        commandBuffer_.draw(3, 1, 0, 0);
        // End draw commands
        commandBuffer_.endRendering();

        // Transistion image to present
        // colorAttachmentOptimal -> presentSrcKHR
        transition_image_layout(
            imageIndex,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlags2{vk::AccessFlagBits2::eColorAttachmentWrite},
            vk::AccessFlags2{},
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eColorAttachmentOutput},
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eBottomOfPipe});
        // End recording commands
        commandBuffer_.end();
    }

    auto createSyncObjects() -> void {
        vk::SemaphoreCreateInfo semaphoreInfo{};
        vk::FenceCreateInfo fenceInfo{};
        fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);

        usize sz = swapchain_.getImages().size();
        // Creates a semaphore per swachain image.
        // Each of these will be signaled by 'acquireNextImage' at runtime when a swapchain image is ready to render into
        for (usize i = 0; i < sz; i++) {
            imageAvailableSemaphores_.emplace_back(context_.getLogicalDevice(), semaphoreInfo);
        }

        // Also creates a semaphore per swapchain image
        // each will be singlaed by 'queueSubmit' at runtime when rendering is done.
        // the presenter will then know it is safe to show the image
        for (usize i = 0; i < sz; i++) {
            renderFinishedSemaphores_.emplace_back(context_.getLogicalDevice(), semaphoreInfo);
        }

        // CPU blocks on this at the start of each frame to ensure the previous frame's GPU work is done before recording into command buffer
        inflightfence_ = vk::raii::Fence(context_.getLogicalDevice(), fenceInfo);
    }

    auto drawFrame() -> void {
        // CPU blocks here until GPU signals 'inflightfence_'.
        // 'inflightFence_' was submitted with previous frame''s 'queueSubmit2' - GPU signals it when it finishes executing that command buffer.
        // We only proceed after the signal which indicates that we can re-record the command buffers now as they are completely used by the GPU
        auto result = context_.getLogicalDevice().waitForFences(*inflightfence_, vk::True, std::numeric_limits<uint64_t>::max());
        context_.getLogicalDevice().resetFences(*inflightfence_);

        // Returns the index of a free image from the image pool
        // the 'imageAvailableSemaphores_[currentFrame_]' semaphore gets signaled by the swapchain when the image is actually ready
        auto [acquireResult, imageIndex] = swapchain_.getSwapchain().acquireNextImage(
            std::numeric_limits<uint64_t>::max(),
            *imageAvailableSemaphores_[currentFrame_],
            nullptr
        );

        // Re-record fresh command buffers for the current frame.
        commandBuffer_.reset();
        recordFrameBuffer(imageIndex);

        // --- submit ---
        // WAIT
        vk::SemaphoreSubmitInfo waitSemaphoreInfo{};
        waitSemaphoreInfo
            .setSemaphore(*imageAvailableSemaphores_[currentFrame_])
            .setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput);

        // After exec, tell GPU that the present image is ready
        // Also tells the next frame's waitForFences' that is is safe to proceed
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

        context_.getQueue().submit2(submitInfo, *inflightfence_);
        // ---

        // Tells the swapchain to present the Image at imageIdx.
        vk::PresentInfoKHR presentInfo{};
        presentInfo
            .setWaitSemaphores(*renderFinishedSemaphores_[imageIndex]) // waits on this
            .setSwapchains(*swapchain_.getSwapchain())
            .setImageIndices(imageIndex);
       result = context_.getQueue().presentKHR(presentInfo);

       currentFrame_ = (currentFrame_ + 1) % swapchain_.getImageViews().size();
    }

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
            .setImage(swapchain_.getImages()[imageIndex])
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

private:
    VulkanContext const& context_;
    Swapchain     const& swapchain_;
    Pipeline      const& pipeline_;
    GLFWwindow*   window_ = nullptr;

    vk::raii::CommandBuffer commandBuffer_ = nullptr;

    std::vector<vk::raii::Semaphore> imageAvailableSemaphores_{};
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores_{};
    vk::raii::Fence                  inflightfence_ = nullptr;
    u32 currentFrame_{};
};
