module;

#include "vulkan/vulkan.hpp"
#include <cstring>
#include <vulkan/vulkan_raii.hpp>
#include <print>

export module buffer;

import types;
import Context;

export class DeviceBuffer {
public:
    DeviceBuffer(VulkanContext const& context, vk::DeviceSize const buffer_size,
           vk::BufferUsageFlags const usage_bits, vk::MemoryPropertyFlags const property_flags)
        : context_(context)
        , buffer_size(buffer_size)
        , usage_bits(usage_bits)
        , memory_property_flags(property_flags)
    {
        vk::BufferCreateInfo createInfo{};
        createInfo
            .setSharingMode(vk::SharingMode::eExclusive)
            .setSize(buffer_size)
            .setUsage(usage_bits);

        buffer_ = vk::raii::Buffer(context_.getLogicalDevice(), createInfo);

        // get memory reqs
        vk::MemoryRequirements memReqs = buffer_.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo
            .setAllocationSize(memReqs.size)
            .setMemoryTypeIndex(findMemoryType(memReqs.memoryTypeBits, memory_property_flags));

        vk::raii::DeviceMemory memory(context_.getLogicalDevice(), allocInfo);
        // bind buffer to resource
        buffer_.bindMemory(*memory, 0);
    }

    static auto copyBuffer(VulkanContext const& context_, DeviceBuffer const& srcBuffer, DeviceBuffer const& dstBuffer, vk::DeviceSize size) -> void {
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo
            .setCommandPool(context_.getCommandpool())
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1u);

        vk::raii::CommandBuffer command_copy_buffer = std::move(context_.getLogicalDevice().allocateCommandBuffers(allocInfo).front());
        command_copy_buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        command_copy_buffer.copyBuffer(srcBuffer.get_buffer(), dstBuffer.get_buffer(), vk::BufferCopy(0, 0, size));
        command_copy_buffer.end();

        vk::SubmitInfo submitInfo{};
        submitInfo
            .setCommandBufferCount(1u)
            .setPCommandBuffers(&*command_copy_buffer);

        context_.getGraphicsQueue().submit(submitInfo, nullptr);
        context_.getGraphicsQueue().waitIdle();
    }

    auto write(void const* data, vk::DeviceSize const buffer_size, vk::DeviceSize offset = 0) -> void {
        void* mapped = this->buffer_memory_.mapMemory(offset, buffer_size);
        memcpy(mapped, data, static_cast<usize>(buffer_size));
        buffer_memory_.unmapMemory();
    }

    [[nodiscard]] auto get_size()   const noexcept -> vk::DeviceSize { return buffer_size; }
    [[nodiscard]] auto get_buffer() const noexcept -> vk::Buffer const& { return buffer_; }
/*
    auto createVertexBuffer(std::vector<Vertex> const& vertices__) -> void {
        vk::DeviceSize buffer_size = sizeof(vertices__[0]) * vertices__.size();

        auto [staging_vertex_buffer, staging_buffer_memory] = createBuffer(
            buffer_size,
            vk::BufferUsageFlagBits::eTransferSrc, // memory used as transfer source location
            vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible
        );

        void* data = staging_buffer_memory.mapMemory(0u, buffer_size);
        memcpy(data, vertices__.data(), buffer_size);
        staging_buffer_memory.unmapMemory();

        std::tie(vertexBuffer_, vertexBufferMemory_) = createBuffer(
            buffer_size,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, // memory used as transfer destination location
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        copyBuffer(staging_vertex_buffer, vertexBuffer_, static_cast<usize>(buffer_size));
        std::println("[LOG] Vertex buffer created and copied!");
    }

    auto createIndexBuffer(std::vector<u16> const& indices__) -> void {
        vk::DeviceSize size = sizeof(indices__[0]) * indices__.size();

        auto [staging_index_buffer, staging_index_memory] = createBuffer(
            size,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible
        );

        void* data = staging_index_memory.mapMemory(0, size);
        memcpy(data, indices__.data(), static_cast<usize>(size));
        staging_index_memory.unmapMemory();

        std::tie(indexBuffer_, indexBufferMemory_) = createBuffer(
            size,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        copyBuffer(staging_index_buffer, indexBuffer_, size);
        std::println("[LOG] index buffer created and copied!");
    }
*/
private:
    [[nodiscard]] auto findMemoryType(u32 typeBits, vk::MemoryPropertyFlags props) -> u32 {
        vk::PhysicalDeviceMemoryProperties memProps = context_.getPhysicalDevice().getMemoryProperties();

        for (u32 i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
                return i;
            }
        }
        throw std::runtime_error("[ERR] Failed to find a suitable memory type!");
    }

    VulkanContext const& context_;

    vk::BufferUsageFlags    const usage_bits{};
    vk::MemoryPropertyFlags const memory_property_flags{};
    vk::DeviceSize          const buffer_size{};

    vk::raii::Buffer buffer_= nullptr;
    vk::raii::DeviceMemory buffer_memory_ = nullptr;
};
