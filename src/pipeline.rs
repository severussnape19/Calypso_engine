use std::{error::Error, ffi::{CString, c_char}, fs::File, path::{self, Path}};

use ash::khr::swapchain;
use glm::ext::half_pi;

use crate::{log, pipeline, swapchain::Swapchain, vulkan_context::VulkanContext, warn};

#[repr(C)]
pub struct Vertex {
    pub pos:   glm::Vec2,
    pub color: glm::Vec3,
}

impl Vertex {
    pub fn get_binding_description() -> ash::vk::VertexInputBindingDescription {
        ash::vk::VertexInputBindingDescription {
            binding: 0u32,
            stride: size_of::<Vertex>() as u32,
            input_rate: ash::vk::VertexInputRate::VERTEX,
        }
    }

    pub fn get_attribute_descriptions() -> [ash::vk::VertexInputAttributeDescription; 2] {
        let description_1 = ash::vk::VertexInputAttributeDescription::default()
            .location(0u32)
            .binding(0u32)
            .format(ash::vk::Format::R32G32_SFLOAT)
            .offset(std::mem::offset_of!(Vertex, pos) as u32);

        let description_2 = ash::vk::VertexInputAttributeDescription::default()
            .location(1u32)
            .binding(0u32)
            .format(ash::vk::Format::R32G32B32_SFLOAT)
            .offset(std::mem::offset_of!(Vertex, color) as u32);

        [description_1, description_2]
    }
}

pub struct Pipeline {
    pub shader_module: ash::vk::ShaderModule,
    pub layout: ash::vk::PipelineLayout,
    pub handle: ash::vk::Pipeline,
    pub descriptor_set_layout: ash::vk::DescriptorSetLayout,
}

impl Pipeline {
    pub unsafe fn destroy_resources(&mut self, ctx: &VulkanContext) {
        unsafe {
            ctx.device.destroy_descriptor_set_layout(self.descriptor_set_layout, None);
            ctx.device.destroy_shader_module(self.shader_module, None);
            ctx.device.destroy_pipeline_layout(self.layout, None);
            ctx.device.destroy_pipeline(self.handle, None);
            warn!(WARN, "Pipeline objects destroyed!");
        }
    }

    fn load_shader(ctx: &VulkanContext, path: &std::path::Path) -> Result<ash::vk::ShaderModule, Box<dyn Error>> {
        let mut file = std::io::BufReader::new(
            std::fs::File::open(path)?
        );
        let code = ash::util::read_spv(&mut file)?;
        let create_info = ash::vk::ShaderModuleCreateInfo::default()
            .code(&code);
        let shader_module = unsafe { ctx.device.create_shader_module(&create_info, None)? };

        log!(INFO, "Shader module created!");
        Ok(shader_module)
    }

    pub fn new(ctx: &VulkanContext, swapchain: &Swapchain) -> Result<Self, Box<dyn Error>> {
        let descriptor_set_layout = Self::create_descriptor_set_layout(ctx)?;
        let (shader_module, pipeline_layout, pipeline) =
            Self::create_graphics_pipeline(ctx, swapchain, &descriptor_set_layout)?;

        Ok (Self {
            shader_module,
            layout: pipeline_layout,
            handle: pipeline,
            descriptor_set_layout
        })
    }

    fn create_descriptor_set_layout(ctx: &VulkanContext) -> Result<ash::vk::DescriptorSetLayout, Box<dyn Error>> {
        let ubo_layout_binding = ash::vk::DescriptorSetLayoutBinding::default()
            .binding(0)
            .descriptor_type(ash::vk::DescriptorType::UNIFORM_BUFFER)
            .descriptor_count(1)
            .stage_flags(ash::vk::ShaderStageFlags::VERTEX);
        let bindings = [ubo_layout_binding];

        let create_info = ash::vk::DescriptorSetLayoutCreateInfo::default()
            .bindings(&bindings);

        log!(INFO, "Descriptor set layout created!");
        Ok(unsafe { ctx.device.create_descriptor_set_layout(&create_info, None)? })
    }

    fn create_graphics_pipeline(
        ctx: &VulkanContext,
        swapchain: &Swapchain,
        descriptor_set_layout: &ash::vk::DescriptorSetLayout,
    ) -> Result<(ash::vk::ShaderModule, ash::vk::PipelineLayout, ash::vk::Pipeline), Box<dyn Error>> {

        let path = Path::new("./shaders/slang.spv");
        let shader_module= Self::load_shader(ctx, path)?;

        let vertex_stage_name = CString::new("vertMain")?;
        let frag_stage_name   = CString::new("fragMain")?;

        let vertex_shader_stage = ash::vk::PipelineShaderStageCreateInfo::default()
            .stage(ash::vk::ShaderStageFlags::VERTEX)
            .module(shader_module)
            .name(&vertex_stage_name);

        let fragment_shader_stage = ash::vk::PipelineShaderStageCreateInfo::default()
            .stage(ash::vk::ShaderStageFlags::FRAGMENT)
            .module(shader_module)
            .name(&frag_stage_name);

        let shader_stages = [vertex_shader_stage, fragment_shader_stage];

        let input_assembly_state = ash::vk::PipelineInputAssemblyStateCreateInfo::default()
            .topology(ash::vk::PrimitiveTopology::TRIANGLE_LIST);

        let tessellation_stage = ash::vk::PipelineTessellationStateCreateInfo::default();

        let viewport_state = ash::vk::PipelineViewportStateCreateInfo::default()
            .viewport_count(1u32)
            .scissor_count(1u32);

        // Dynamic states
        let dynamic_states = [ash::vk::DynamicState::VIEWPORT, ash::vk::DynamicState::SCISSOR];
        let pipeline_dynamic_state = ash::vk::PipelineDynamicStateCreateInfo::default()
            .dynamic_states(&dynamic_states);
        //pipeline_dynamic_state.dynamic_state_count = 2u32;

        let rasterization_stage = ash::vk::PipelineRasterizationStateCreateInfo::default()
            .depth_clamp_enable(false)
            .rasterizer_discard_enable(false)
            .polygon_mode(ash::vk::PolygonMode::FILL)
            .cull_mode(ash::vk::CullModeFlags::BACK)
            .front_face(ash::vk::FrontFace::CLOCKWISE)
            .depth_bias_enable(false)
            .line_width(1.0f32);

        let multisample_stage   = ash::vk::PipelineMultisampleStateCreateInfo::default()
            .rasterization_samples(ash::vk::SampleCountFlags::TYPE_1)
            .sample_shading_enable(false);

        let color_blend_attachment = ash::vk::PipelineColorBlendAttachmentState::default()
            .blend_enable(false)
            .color_write_mask(ash::vk::ColorComponentFlags::RGBA);

        let attachments = [color_blend_attachment];
        let color_blend_state = ash::vk::PipelineColorBlendStateCreateInfo::default()
            .attachments(&attachments)
            .logic_op(ash::vk::LogicOp::COPY)
            .logic_op_enable(false);

        let depth_state = ash::vk::PipelineDepthStencilStateCreateInfo::default()
            .depth_test_enable(true)
            .depth_write_enable(true)
            .depth_compare_op(ash::vk::CompareOp::LESS)
            .depth_bounds_test_enable(false)
            .stencil_test_enable(false);

        let descriptor_set_layouts = [*descriptor_set_layout];
        let pipeline_layout_create_info = ash::vk::PipelineLayoutCreateInfo::default()
            .set_layouts(&descriptor_set_layouts);

        let pipeline_layout = unsafe {
            ctx.device.create_pipeline_layout(&pipeline_layout_create_info, None)?
        };

        let binding_descriptions  = [Vertex::get_binding_description()];
        let attribute_description = Vertex::get_attribute_descriptions();

        let vertex_input = ash::vk::PipelineVertexInputStateCreateInfo::default()
            .vertex_binding_descriptions(&binding_descriptions)
            .vertex_attribute_descriptions(&attribute_description);

        let surface_formats = [swapchain.config.surface_format.format];
        let mut render_create_info = ash::vk::PipelineRenderingCreateInfo::default()
            .color_attachment_formats(&surface_formats)
            .depth_attachment_format(ash::vk::Format::D32_SFLOAT);

        let create_info = ash::vk::GraphicsPipelineCreateInfo::default()
            .input_assembly_state(&input_assembly_state)
            .stages(&shader_stages)
            .input_assembly_state(&input_assembly_state)
            .vertex_input_state(&vertex_input)
            .viewport_state(&viewport_state)
            .rasterization_state(&rasterization_stage)
            .multisample_state(&multisample_stage)
            .color_blend_state(&color_blend_state)
            .depth_stencil_state(&depth_state)
            .layout(pipeline_layout)
            .dynamic_state(&pipeline_dynamic_state)
            .render_pass(ash::vk::RenderPass::null())
            .push_next(&mut render_create_info);

        let graphics_pipeline = unsafe { ctx.device.create_graphics_pipelines(
            ash::vk::PipelineCache::null(),
            &[create_info],
            None)
            .map_err(|(partials, err)| {
                for pipeline in partials { // if partially created, destroy all and return
                    ctx.device.destroy_pipeline(pipeline, None);
                }
                err
            })?
        };

        log!(INFO, "Pipeline and layout created!");

        Ok((shader_module, pipeline_layout, graphics_pipeline[0]))
    }
}
