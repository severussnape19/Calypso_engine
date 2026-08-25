module;

#include "vulkan/vulkan.hpp"
#include <array>
#include <vulkan/vulkan_raii.hpp>
export module mesh;

import types;
import math;
import Context;
import buffer;

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

std::vector<Vertex> const default_vertices {
{.pos = {-0.5f, -0.5f}, .color = {1.f, 0.f, 0.f}},
    {.pos = { 0.5f, -0.5f}, .color = {0.f, 1.f, 0.f}},
    {.pos = { 0.5f,  0.5f}, .color = {0.f, 0.f, 1.f}},
    {.pos = {-0.5f,  0.5f}, .color = {1.f, 1.f, 1.f}},
};

std::vector<u16> const default_indices = {
    0, 1, 2, 2, 3, 0
};

export class Mesh {
public:
    explicit Mesh(
            VulkanContext const& context,
            std::vector<Vertex> const& vertices = default_vertices,
            std::vector<u16>    const& indices  = default_indices)
        : context_(context)
        , vertex_buffer_(
                context_,
                vk::DeviceSize(sizeof(vertices[0]) * vertices.size()),
                vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
                )
        , index_buffer_(
                context_,
                vk::DeviceSize(sizeof(indices[0] * indices.size())),
                vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
                )
        , index_count(static_cast<u32>(default_indices.size()))
    {
        DeviceBuffer vertex_staging_buffer(
            context_,
            vertex_buffer_.get_size(),
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        vertex_staging_buffer.write(vertices.data(), vertex_staging_buffer.get_size());
        DeviceBuffer::copyBuffer(context_, vertex_staging_buffer, vertex_buffer_, vertex_buffer_.get_size());

        DeviceBuffer index_staging_buffer(
            context_,
            vertex_buffer_.get_size(),
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        index_staging_buffer.write(indices.data(), index_staging_buffer.get_size());
        DeviceBuffer::copyBuffer(context_, index_staging_buffer, index_buffer_, index_staging_buffer.get_size());
    }

    Mesh(Mesh const&) = delete;
    auto operator=(Mesh const&) noexcept -> Mesh& = delete;

    Mesh(Mesh&&) noexcept = delete;
    //auto operator=(Mesh&&) noexcept -> Mesh& = default;

    auto draw(vk::raii::CommandBuffer const& cmdBuf) const -> void {
        cmdBuf.bindVertexBuffers(0, vertex_buffer_.get_buffer(), {0});
        cmdBuf.bindIndexBuffer(index_buffer_.get_buffer(), 0, vk::IndexType::eUint16);
        cmdBuf.drawIndexed(index_count, 1u, 0u, 0u, 0u);
    }

    auto get_binding_descriptions() -> const std::tuple<vk::VertexInputBindingDescription, std::array<vk::VertexInputAttributeDescription, 2>> {
        return { Vertex::getBindingDescription(), Vertex::getAttributeDescriptions() };
    }

private:
    VulkanContext const& context_;
    DeviceBuffer vertex_buffer_;
    DeviceBuffer index_buffer_;

    std::vector<Vertex> vertices_{};
    std::vector<u16> indices_{};

    u32 index_count{};
};
