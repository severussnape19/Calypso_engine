module;

#include "vulkan/vulkan.hpp"
#include <array>
#include <vulkan/vulkan_raii.hpp>
#include <print>

export module Mesh;

import types;
import math;
import Context;

export struct Vertex {
    Vec2f pos;
    Color color;

    static auto getBindingDescription() -> vk::VertexInputBindingDescription {
        return {0, sizeof(Vertex) , vk::VertexInputRate::eVertex};
    }

    static auto getAttributeDescriptions() -> std::array<vk::VertexInputAttributeDescription, 2> {
        vk::VertexInputAttributeDescription des1{
            0,
            0,
            vk::Format::eR32G32Sfloat,
            offsetof(Vertex, pos) // 0 bytes
        };
        vk::VertexInputAttributeDescription des2{
            1,
            0,
            vk::Format::eR32G32B32Sfloat,
            offsetof(Vertex, color) // 8
        };
        return {des1, des2};
    }
};

std::vector<Vertex> const vertices {
    {.pos = {-0.5f, -0.5f}, .color = {1.f, 0.f, 0.f}},
    {.pos = { 0.5f, -0.5f}, .color = {0.f, 1.f, 0.f}},
    {.pos = { 0.5f,  0.5f}, .color = {0.f, 0.f, 1.f}},
    {.pos = {-0.5f,  0.5f}, .color = {1.f, 1.f, 1.f}},
};

std::vector<u16> const indices = {
    0, 1, 2, 2, 3, 0
};

export class Mesh {
public:
    Mesh(VulkanContext const& context, std::vector<Vertex> const& vertices, std::vector<u16> const& indices)
        : context_(context)
        , vertices_(vertices)
        , indices_(indices)
        , index_count(static_cast<u32>(indices_.size()))
    {
        this->createVertexBuffer(vertices_);
        this->createIndexBuffer(indices_);
    }

    Mesh(Mesh const&) = delete;
    auto operator=(Mesh const&) noexcept -> Mesh& = delete;

    Mesh(Mesh&&) noexcept = delete;
    //auto operator=(Mesh&&) noexcept -> Mesh& = default;

    auto draw(vk::raii::CommandBuffer const& cmdBuf) const -> void {
        cmdBuf.bindVertexBuffers(0, *vertexBuffer_, {0});
        cmdBuf.bindIndexBuffer(*indexBuffer_, 0, vk::IndexType::eUint16);
        cmdBuf.drawIndexed(index_count, 1u, 0u, 0u, 0u);
    }
private:
    auto createBuffer(vk::DeviceSize buffer_size, vk::BufferUsageFlags usage_bits, vk::MemoryPropertyFlags propertyFlags) -> std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> {
        // create buffer
        vk::BufferCreateInfo createInfo{};
        createInfo
            .setSharingMode(vk::SharingMode::eExclusive)
            .setSize(buffer_size)
            .setUsage(usage_bits);

        vk::raii::Buffer buffer(context_.getLogicalDevice(), createInfo);

        // get memory reqs
        vk::MemoryRequirements memReqs = buffer.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo
            .setAllocationSize(memReqs.size)
            .setMemoryTypeIndex(findMemoryType(memReqs.memoryTypeBits, propertyFlags));

        vk::raii::DeviceMemory memory(context_.getLogicalDevice(), allocInfo);
        // bind buffer to resource
        buffer.bindMemory(*memory, 0);

        return {std::move(buffer), std::move(memory)};
    }

    auto copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size) -> void {
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo
            .setCommandPool(context_.getCommandpool())
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1u);

        vk::raii::CommandBuffer command_copy_buffer = std::move(context_.getLogicalDevice().allocateCommandBuffers(allocInfo).front());
        command_copy_buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        command_copy_buffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy(0, 0, size));
        command_copy_buffer.end();

        vk::SubmitInfo submitInfo{};
        submitInfo
            .setCommandBufferCount(1u)
            .setPCommandBuffers(&*command_copy_buffer);

        context_.getGraphicsQueue().submit(submitInfo, nullptr);
        context_.getGraphicsQueue().waitIdle();
    }

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

    [[nodiscard]] auto findMemoryType(u32 typeBits, vk::MemoryPropertyFlags props) -> u32 {
        vk::PhysicalDeviceMemoryProperties memProps = context_.getPhysicalDevice().getMemoryProperties();

        for (u32 i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
                return i;
            }
        }
        throw std::runtime_error("[ERR] Failed to find a suitable memory type!");
    }
private:
    VulkanContext const& context_;

    std::vector<Vertex> const& vertices_{};
    std::vector<u16>    const& indices_{};

    u32 index_count{};

    vk::raii::Buffer              vertexBuffer_       = nullptr;
    vk::raii::DeviceMemory        vertexBufferMemory_ = nullptr;
    vk::raii::Buffer              indexBuffer_        = nullptr;
    vk::raii::DeviceMemory        indexBufferMemory_  = nullptr;
};
