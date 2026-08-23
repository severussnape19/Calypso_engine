module;

#include "vulkan/vulkan.hpp"
#include <fstream>
#include <print>
#include <iostream>
#include <stdexcept>
#include <vulkan/vulkan_raii.hpp>
export module pipeline;

import types;
import Context;
import swapchain;
import math;


/* owns shader modules, Descriptor set layouts, pipeline layout and ofc the object itself */
export class Pipeline {
public:
    explicit Pipeline(VulkanContext const& context, Swapchain const& swapchain) noexcept
        : context_(context)
        , swapchain_(swapchain)
    {
        createGraphicsPipeline();
    }

    Pipeline(Pipeline const& o) = delete;
    auto operator=(Pipeline const& o) -> Pipeline& = delete;

    [[nodiscard]] auto getPipeline()            const noexcept -> vk::raii::Pipeline            const& { return graphicsPipeline_;     }
    [[nodiscard]] auto getPipelineLayout()      const noexcept -> vk::raii::PipelineLayout      const& { return pipelineLayout_;       }
    [[nodiscard]] auto getDescriptorSetLayout() const noexcept -> vk::raii::DescriptorSetLayout const& { return  descriptorSetLayout_; }
private:
    static auto load_shader(std::string const& path) -> std::vector<char> {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("[ERR] Could not read shader at path: " + path);
        }
        usize size = file.tellg();
        std::vector<char> buffer(size);

        file.seekg(0, std::ios::beg);
        file.read(buffer.data(), static_cast<std::streamsize>(size));

        file.close();
        return buffer;
    }

    [[nodiscard]] auto createShaderModule(std::vector<char> const& shader_code) -> vk::raii::ShaderModule {
        vk::ShaderModuleCreateInfo shaderCreateInfo{};
        shaderCreateInfo
            .setCodeSize(shader_code.size() * sizeof(char))
            .setPCode(reinterpret_cast<u32 const*>(shader_code.data()));
        vk::raii::ShaderModule module(context_.getLogicalDevice(), shaderCreateInfo);

        return module;
    }

    auto createGraphicsPipeline() -> void {
        std::vector<char> shader_code = load_shader("../shaders/slang.spv");
        vk::raii::ShaderModule shader_module = createShaderModule(shader_code);

        vk::PipelineShaderStageCreateInfo vertexShaderStage{};
        vertexShaderStage
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(shader_module)
            .setPName("vertMain");

        vk::PipelineShaderStageCreateInfo fragmentShaderStage{};
        fragmentShaderStage
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(shader_module)
            .setPName("fragMain");

        auto bindingDescription   = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo
            .setVertexBindingDescriptionCount(1u)
            .setPVertexBindingDescriptions(&bindingDescription)
            .setVertexAttributeDescriptionCount(static_cast<u32>(attributeDescriptions.size()))
            .setPVertexAttributeDescriptions(attributeDescriptions.data());

        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{vertexShaderStage, fragmentShaderStage};

        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{};
        inputAssemblyState.setTopology(vk::PrimitiveTopology::eTriangleList);

        vk::PipelineTessellationStateCreateInfo tesselationState{}; // No need rn

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState
            .setViewportCount(1u)
            .setScissorCount(1u);

        // We can dynamically change viewport and scissor states without having to create a new pipeline
        std::array<vk::DynamicState, 2> dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState
            .setDynamicStateCount(static_cast<u32>(dynamicStates.size()))
            .setPDynamicStates(dynamicStates.data());

        vk::PipelineRasterizationStateCreateInfo rasterizationState{};
        rasterizationState
            .setDepthClampEnable(vk::False)
            .setRasterizerDiscardEnable(vk::False)
            .setPolygonMode(vk::PolygonMode::eFill)
            .setCullMode(vk::CullModeFlagBits::eBack)
            .setFrontFace(vk::FrontFace::eClockwise)
            .setDepthBiasClamp(vk::False)
            .setLineWidth(1.0f);

        vk::PipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState
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

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending
            .setAttachmentCount(1u)
            .setPAttachments(&colorBlendAttachment)
            .setLogicOpEnable(vk::False)
            .setLogicOp(vk::LogicOp::eCopy);

        vk::PipelineDepthStencilStateCreateInfo depthStateCreateInfo{};
        depthStateCreateInfo
            .setDepthTestEnable(vk::True)
            .setDepthWriteEnable(vk::True)
            .setDepthCompareOp(vk::CompareOp::eLess)
            .setDepthBoundsTestEnable(vk::False)
            .setStencilTestEnable(vk::False);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo
            .setSetLayoutCount(0u)
            .setPushConstantRangeCount(0u);
        pipelineLayout_ = vk::raii::PipelineLayout(context_.getLogicalDevice(), pipelineLayoutInfo);

        vk::GraphicsPipelineCreateInfo pipelineCreateInfo{};
        pipelineCreateInfo
            .setStageCount(2u)
            .setPStages(shaderStages.data())
            .setPInputAssemblyState(&inputAssemblyState)
            .setPVertexInputState(&vertexInputInfo)
            .setPViewportState(&viewportState)
            .setPRasterizationState(&rasterizationState)
            .setPMultisampleState(&multisampleState)
            .setPColorBlendState(&colorBlending)
            .setPDepthStencilState(&depthStateCreateInfo)
            .setLayout(pipelineLayout_)
            .setPDynamicState(&dynamicState)
            .setRenderPass(nullptr);

        vk::Format surfaceFormat = swapchain_.getSurfaceFormat().format;
        vk::PipelineRenderingCreateInfo pipelineRenderingInfo{
            {},
            1u,
            &surfaceFormat,
            vk::Format::eD32Sfloat
        };

        vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
            pipelineCreateInfoChain = {pipelineCreateInfo, pipelineRenderingInfo};

        graphicsPipeline_ = vk::raii::Pipeline(
                context_.getLogicalDevice(),
                nullptr,
                pipelineCreateInfoChain.template get<vk::GraphicsPipelineCreateInfo>());

        std::println(stderr, "[LOG] Graphics pipeline created!");
    }

    auto createDescriptorSetLayout() -> void {
        /* A descriptor set is a set of resources that are bound into the pipeline as a group */
        vk::DescriptorSetLayoutCreateInfo createInfo{};
        createInfo
            .setBindingCount(0u)
            .setPBindings({});
        descriptorSetLayout_ = vk::raii::DescriptorSetLayout(context_.getLogicalDevice(), createInfo);
    }
private:
    VulkanContext const& context_;
    Swapchain     const& swapchain_;

    vk::raii::PipelineLayout      pipelineLayout_      = nullptr;
    vk::raii::Pipeline            graphicsPipeline_    = nullptr;
    vk::raii::DescriptorSetLayout descriptorSetLayout_ = nullptr;
};
