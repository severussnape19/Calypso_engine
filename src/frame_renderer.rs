use core::error;
use std::{char::MAX, error::Error, ffi::c_void, ops::BitOr, sync::LazyLock, time::Instant};

use ash::{khr::swapchain, nv::descriptor_pool_overallocation, vk::{CommandBuffer, Semaphore}};
use glm::{ Vec2, Vec3, ext::{look_at, perspective, rotate} };

use crate::{buffer::DeviceBuffer, log, mesh::Mesh, pipeline::{self, Pipeline, Vertex}, swapchain::Swapchain, vulkan_context::VulkanContext, warn};

const MAX_FRAMES_IN_FLIGHT: u32 = 2;
static START_TIME: LazyLock<Instant> = LazyLock::new(Instant::now);

#[repr(C)]
pub struct UniformBufferObject {
    model: glm::Mat4,
    view:  glm::Mat4,
    proj:  glm::Mat4,
}

pub struct SyncObjects {
    pub present_complete_semaphores: Vec<ash::vk::Semaphore>,
    pub render_finish_semaphores: Vec<ash::vk::Semaphore>,
    pub inflight_fences: Vec<ash::vk::Fence>,
}

pub struct FrameRenderer {
    mesh: Mesh,
    vbo: DeviceBuffer,
    ibo: DeviceBuffer,
    descriptor_pool: ash::vk::DescriptorPool,
    descriptor_sets: Vec<ash::vk::DescriptorSet>,
    uniform_buffers: Vec<ash::vk::Buffer>,
    uniform_buffers_memory: Vec<ash::vk::DeviceMemory>,
    command_buffers: Vec<ash::vk::CommandBuffer>,
    uniform_buffers_mapped: Vec<*mut c_void>,
    sync_objects : SyncObjects,
    current_frame: usize,
    framebuffer_resized: bool,
}

impl FrameRenderer {
    pub fn new(ctx: &VulkanContext, swapchain: &Swapchain, pipeline: &Pipeline) -> Result<Self, Box<dyn Error>> {
        let descriptor_pool = Self::create_descriptor_pool(ctx)?;

        let mesh: Mesh = Mesh::data();

        let ibo: DeviceBuffer = DeviceBuffer::create_vertex_buffer(
            ctx,
            ash::vk::BufferUsageFlags::INDEX_BUFFER,
            ash::vk::MemoryPropertyFlags::DEVICE_LOCAL,
            &mesh.indices
        )?;

        let vbo: DeviceBuffer = DeviceBuffer::create_vertex_buffer(
            ctx,
            ash::vk::BufferUsageFlags::VERTEX_BUFFER,
            ash::vk::MemoryPropertyFlags::DEVICE_LOCAL,
            &mesh.vertices
        )?;

        let sync_objects = Self::create_synchronization_objects(ctx, swapchain)?;
        let current_frame = 0;

        let command_buffers = Self::create_command_buffer(ctx)?;

        let (uniform_buffers, uniform_buffers_memory, uniform_buffers_mapped) =
            Self::create_uniform_buffer(ctx)?;

        let descriptor_sets = Self::create_descriptor_sets(
            ctx,
            pipeline.descriptor_set_layout,
            descriptor_pool,
            &uniform_buffers
        )?;

        Ok( Self{
            mesh,
            vbo,
            ibo,
            descriptor_pool,
            descriptor_sets,
            uniform_buffers,
            uniform_buffers_memory,
            command_buffers,
            uniform_buffers_mapped,
            sync_objects,
            current_frame,
            framebuffer_resized: false,
        } )
    }

    pub unsafe fn destroy_resources(&mut self, ctx: &VulkanContext) {
        unsafe {
            let _ = ctx.device.device_wait_idle();
            ctx.device.free_command_buffers(ctx.command_pool, &self.command_buffers);

            for i in 0..self.sync_objects.present_complete_semaphores.len() {
                ctx.device.destroy_semaphore(self.sync_objects.present_complete_semaphores[i], None);
                ctx.device.destroy_semaphore(self.sync_objects.render_finish_semaphores[i], None);
                ctx.device.destroy_fence(self.sync_objects.inflight_fences[i], None);
            }
            warn!(WARN, "Render objects destroyed!");

            for i in 0..self.uniform_buffers.len() {
                ctx.device.unmap_memory(self.uniform_buffers_memory[i]);
                ctx.device.destroy_buffer(self.uniform_buffers[i], None);
                ctx.device.free_memory(self.uniform_buffers_memory[i], None);
            }
            ctx.device.destroy_descriptor_pool(self.descriptor_pool, None);
        }
    }

    fn create_descriptor_pool(ctx: &VulkanContext) -> Result<(ash::vk::DescriptorPool), Box<dyn Error>> {
        let pool_size = ash::vk::DescriptorPoolSize::default()
            .ty(ash::vk::DescriptorType::UNIFORM_BUFFER)
            .descriptor_count(MAX_FRAMES_IN_FLIGHT);

        let create_info = ash::vk::DescriptorPoolCreateInfo::default()
            .flags(ash::vk::DescriptorPoolCreateFlags::FREE_DESCRIPTOR_SET)
            .max_sets(MAX_FRAMES_IN_FLIGHT)
            .pool_sizes(std::slice::from_ref(&pool_size));
        Ok(unsafe { ctx.device.create_descriptor_pool(&create_info, None)? })
    }

    fn create_descriptor_sets(
        ctx: &VulkanContext,
        descriptor_set_layout: ash::vk::DescriptorSetLayout,
        descriptor_pool: ash::vk::DescriptorPool,
        uniform_buffers: &[ash::vk::Buffer]
    ) -> Result<Vec<ash::vk::DescriptorSet>, Box<dyn Error>> {
        // one descriptor set for each frame in flight
        let layouts: Vec<ash::vk::DescriptorSetLayout> = vec![descriptor_set_layout; MAX_FRAMES_IN_FLIGHT as usize];
        let alloc_info = ash::vk::DescriptorSetAllocateInfo::default()
            .descriptor_pool(descriptor_pool)
            .set_layouts(&layouts);

        let descriptor_sets = unsafe { ctx.device.allocate_descriptor_sets(&alloc_info)? };

        // Descriptor sets are allocated but not yet configured
        for i in 0..MAX_FRAMES_IN_FLIGHT as usize {
            let buffer_info = ash::vk::DescriptorBufferInfo::default()
                .buffer(uniform_buffers[i])
                .offset(0_u64)
                .range(std::mem::size_of::<UniformBufferObject>() as u64);

            let descriptors_write = ash::vk::WriteDescriptorSet::default()
                .dst_set(descriptor_sets[i])
                .dst_array_element(0_u32)
                .descriptor_count(1_u32)
                .descriptor_type(ash::vk::DescriptorType::UNIFORM_BUFFER)
                .buffer_info(std::slice::from_ref(&buffer_info));

            unsafe { ctx.device.update_descriptor_sets(
                std::slice::from_ref(&descriptors_write),
                &[]); }
        }
        Ok(descriptor_sets)
    }

    #[allow(clippy::type_complexity)]
    fn create_uniform_buffer(ctx: &VulkanContext
    ) -> Result<(Vec<ash::vk::Buffer>, Vec<ash::vk::DeviceMemory>, Vec<*mut c_void>), Box<dyn Error>> {
        let mut ubos: Vec<ash::vk::Buffer> = vec![];
        let mut ubos_mem: Vec<ash::vk::DeviceMemory> = vec![];
        let mut ubos_mapped: Vec<*mut c_void> = vec![];

        for i in 0..MAX_FRAMES_IN_FLIGHT {
            let size = std::mem::size_of::<UniformBufferObject>() as u64;
            let (buffer, buffer_mem ) = DeviceBuffer::create_buffer(
                ctx,
                size,
                ash::vk::BufferUsageFlags::UNIFORM_BUFFER,
                ash::vk::MemoryPropertyFlags::HOST_VISIBLE | ash::vk::MemoryPropertyFlags::HOST_COHERENT
            )?;

            ubos.push(buffer);
            ubos_mem.push(buffer_mem);

            let mapped_mem = unsafe {
                ctx.device.map_memory(
                    buffer_mem,
                    0,
                    size,
                    ash::vk::MemoryMapFlags::default())?
            };

            ubos_mapped.push(mapped_mem);
        }

        Ok((ubos, ubos_mem, ubos_mapped))
    }

    fn create_command_buffer(ctx: &VulkanContext) -> Result<Vec<ash::vk::CommandBuffer>, Box<dyn Error>> {
        let alloc_info = ash::vk::CommandBufferAllocateInfo::default()
            .command_pool(ctx.command_pool)
            .level(ash::vk::CommandBufferLevel::PRIMARY)
            .command_buffer_count(MAX_FRAMES_IN_FLIGHT);

        let command_buffers = unsafe { ctx.device.allocate_command_buffers(&alloc_info)? };
        println!("Command Buffers: {}", command_buffers.len());
        Ok(command_buffers)
    }

    #[allow(clippy::too_many_arguments)]
    fn transition_image_layout(
        device: &ash::Device,
        command_buffer: &ash::vk::CommandBuffer, image: ash::vk::Image,
        old_layout: ash::vk::ImageLayout, new_layout: ash::vk::ImageLayout,
        src_access_mask: ash::vk::AccessFlags2, dst_access_mask: ash::vk::AccessFlags2,
        src_stage_mask: ash::vk::PipelineStageFlags2, dst_stage_mask: ash::vk::PipelineStageFlags2,
        aspect_mask: ash::vk::ImageAspectFlags,
    ) {
        let barrier = ash::vk::ImageMemoryBarrier2::default()
            .src_stage_mask(src_stage_mask)
            .dst_stage_mask(dst_stage_mask)
            .src_access_mask(src_access_mask)
            .dst_access_mask(dst_access_mask)
            .old_layout(old_layout)
            .new_layout(new_layout)
            .src_queue_family_index(ash::vk::QUEUE_FAMILY_IGNORED)
            .dst_queue_family_index(ash::vk::QUEUE_FAMILY_IGNORED)
            .image(image)
            .subresource_range(ash::vk::ImageSubresourceRange {
                aspect_mask,
                base_mip_level: 0,
                level_count: 1,
                base_array_layer:0,
                layer_count: 1,
            });

        let mem_barriers = [barrier];
        let dependency_info = ash::vk::DependencyInfo::default().image_memory_barriers(&mem_barriers);
        unsafe { device.cmd_pipeline_barrier2(*command_buffer, &dependency_info) };
    }

    fn record_command_buffer(
        &self,
        device: &ash::Device,
        command_buffer: &ash::vk::CommandBuffer,
        swapchain: &Swapchain,
        pipeline: &Pipeline,
        current_idx: usize, image_index: usize
    ) -> Result<(), Box<dyn Error>> {

        let begin_info = ash::vk::CommandBufferBeginInfo::default()
            .flags(ash::vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT);

        // Start recording
        unsafe { device.begin_command_buffer(*command_buffer, &begin_info)? };

        // ---------------- Image and Depth Transitions
        // image transition barrier (undefined -> colorAttachmentOptimal)
        Self::transition_image_layout(
            device, command_buffer, swapchain.images[image_index],
            ash::vk::ImageLayout::UNDEFINED, ash::vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL,
            ash::vk::AccessFlags2::default(), ash::vk::AccessFlags2::COLOR_ATTACHMENT_WRITE,
            ash::vk::PipelineStageFlags2::TOP_OF_PIPE, ash::vk::PipelineStageFlags2::COLOR_ATTACHMENT_OUTPUT,
            ash::vk::ImageAspectFlags::COLOR);

        let color_attachment_info = ash::vk::RenderingAttachmentInfo::default()
            .image_view(swapchain.image_views[image_index])
            .image_layout(ash::vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL)
            .load_op(ash::vk::AttachmentLoadOp::CLEAR)
            .store_op(ash::vk::AttachmentStoreOp::STORE)
            .clear_value(ash::vk::ClearValue { color: ash::vk::ClearColorValue::default() });

        // Depth image transition
        Self::transition_image_layout(
            device, command_buffer, swapchain.depth_image,
            ash::vk::ImageLayout::UNDEFINED, ash::vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            ash::vk::AccessFlags2::default(), ash::vk::AccessFlags2::DEPTH_STENCIL_ATTACHMENT_WRITE,
            ash::vk::PipelineStageFlags2::TOP_OF_PIPE,
            ash::vk::PipelineStageFlags2::default().bitor(ash::vk::PipelineStageFlags2::EARLY_FRAGMENT_TESTS | ash::vk::PipelineStageFlags2::LATE_FRAGMENT_TESTS),
            ash::vk::ImageAspectFlags::DEPTH);

        let depth_attachment_info = ash::vk::RenderingAttachmentInfo::default()
            .image_view(swapchain.depth_image_view)
            .image_layout(ash::vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            .load_op(ash::vk::AttachmentLoadOp::CLEAR)
            .store_op(ash::vk::AttachmentStoreOp::STORE)
            .clear_value(ash::vk::ClearValue { depth_stencil: ash::vk::ClearDepthStencilValue::default().depth(1.0f32).stencil(0)});

        // ------------- Draw Commands
        let color_attachment_infos = [color_attachment_info];
        let render_info = ash::vk::RenderingInfo::default()
            .render_area(ash::vk::Rect2D::default()
                .offset(ash::vk::Offset2D::default().x(0).y(0))
                .extent(swapchain.config.extent))
            .layer_count(1)
            .color_attachments(&color_attachment_infos)
            .depth_attachment(&depth_attachment_info);

        unsafe { device.cmd_begin_rendering(*command_buffer, &render_info) };

        let viewport = ash::vk::Viewport::default()
            .x(0f32)
            .y(0f32)
            .width(swapchain.config.extent.width as f32)
            .height(swapchain.config.extent.height as f32)
            .min_depth(0f32)
            .max_depth(1f32);
        let viewports = [viewport];

        let scissor = ash::vk::Rect2D::default()
            .offset(ash::vk::Offset2D::default().x(0).y(0))
            .extent(swapchain.config.extent);
        let scissors = [scissor];

        // Bind dynamic variables
        unsafe { device.cmd_set_viewport(*command_buffer, 0, &viewports) };
        unsafe { device.cmd_set_scissor(*command_buffer, 0, &scissors); }

        unsafe { device.cmd_bind_pipeline(*command_buffer, ash::vk::PipelineBindPoint::GRAPHICS, pipeline.handle) };

        let vert_buf_offsets = [0u64];

        unsafe { device.cmd_bind_vertex_buffers(
            *command_buffer,
            0,
            std::slice::from_ref(&self.vbo.handle),
            &vert_buf_offsets
        ); }

        unsafe { device.cmd_bind_index_buffer(
            *command_buffer,
            self.ibo.handle,
            0u64,
            ash::vk::IndexType::UINT16) };

        let dynamic_offsets = [];

        unsafe { device.cmd_bind_descriptor_sets(
            *command_buffer,
            ash::vk::PipelineBindPoint::GRAPHICS,
            pipeline.layout, 0,
            std::slice::from_ref(&self.descriptor_sets[self.current_frame]),
            &dynamic_offsets)
        };

        unsafe { device.cmd_draw_indexed(
            *command_buffer,
            self.mesh.indices.len() as u32,
            1_u32,
            0,
            0,
            0)
        };

        unsafe { device.cmd_end_rendering(*command_buffer) };

        Self::transition_image_layout(
            device, command_buffer, swapchain.images[image_index],
            ash::vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL, ash::vk::ImageLayout::PRESENT_SRC_KHR,
            ash::vk::AccessFlags2::COLOR_ATTACHMENT_WRITE, ash::vk::AccessFlags2::default(),
            ash::vk::PipelineStageFlags2::COLOR_ATTACHMENT_OUTPUT, ash::vk::PipelineStageFlags2::BOTTOM_OF_PIPE,
            ash::vk::ImageAspectFlags::COLOR);

        unsafe { device.end_command_buffer(*command_buffer) };

        Ok(())
    }

    fn update_uniform_buffer(&self, swapchain: &Swapchain) {
        let time: f32 = START_TIME.elapsed().as_secs_f32();
        let identity = glm::mat4(
           1.0, 0.0, 0.0, 0.0,
           0.0, 1.0, 0.0, 0.0,
           0.0, 0.0, 1.0, 0.0,
           0.0, 0.0, 0.0, 1.0,
        );

        let mut ubo: UniformBufferObject = UniformBufferObject {
            model: rotate(&identity, time * 90.0_f32.to_radians(), glm::vec3(0.0, 0.0, 1.0)),
            view: look_at(glm::vec3(2.0, 2.0, 2.0), glm::vec3(0.0, 0.0, 0.0), glm::vec3(0.0, 0.0,1.0)),
            proj: perspective(45_f32.to_radians(), (swapchain.config.extent.width as f32 / swapchain.config.extent.height as f32), 0.1, 10.0),
        };

        ubo.proj[1][1] *= -1.0;
        unsafe {
            let dst = self.uniform_buffers_mapped[self.current_frame] as *mut UniformBufferObject;
            *dst = ubo;
        }
    }

    fn create_synchronization_objects(ctx: &VulkanContext, swapchain: &Swapchain) -> Result<SyncObjects, Box<dyn Error>> {
        let semaphore_info = ash::vk::SemaphoreCreateInfo::default();
        let fence_info = ash::vk::FenceCreateInfo::default().flags(ash::vk::FenceCreateFlags::SIGNALED);

        let num_objs = MAX_FRAMES_IN_FLIGHT as usize;

        let mut present_complete_semaphores: Vec<ash::vk::Semaphore> = Vec::with_capacity(num_objs);
        let mut render_finish_semaphores: Vec<ash::vk::Semaphore> = Vec::with_capacity(num_objs);
        let mut inflight_fences: Vec<ash::vk::Fence> = Vec::with_capacity(num_objs);

        for i in 0..num_objs {
            present_complete_semaphores.push(unsafe { ctx.device.create_semaphore(&semaphore_info, None)? });
            render_finish_semaphores.push(unsafe { ctx.device.create_semaphore(&semaphore_info, None)? });
            inflight_fences.push(unsafe { ctx.device.create_fence(&fence_info, None)? });
        }
        log!(INFO, "Sync objects created!");
        Ok(SyncObjects { present_complete_semaphores, render_finish_semaphores, inflight_fences })
    }

    pub fn draw_frame(&mut self, ctx: &VulkanContext, swapchain: &Swapchain, pipeline: &Pipeline) -> Result<(), Box<dyn Error>> {
        let current_fences = [self.sync_objects.inflight_fences[self.current_frame]];
        let current_wait_semaphores = [self.sync_objects.present_complete_semaphores[self.current_frame]];
        let current_signal_semaphores = [self.sync_objects.render_finish_semaphores[self.current_frame]];
        let current_command_buffers = [self.command_buffers[self.current_frame]];

        unsafe { ctx.device.wait_for_fences(
            &current_fences,
            true,
            u64::MAX)?
        };

        let (image_index, acquire_result) = match unsafe { swapchain.loader.acquire_next_image(
            swapchain.handle,
            u64::MAX,
            current_wait_semaphores[0],
            ash::vk::Fence::null())
        } {
            Ok(result ) => result,
            Err(ash::vk::Result::ERROR_OUT_OF_DATE_KHR) => {
                return Err(Box::new(ash::vk::Result::ERROR_OUT_OF_DATE_KHR));
            }
            Err(e) => return Err(Box::new(e)),
        };

        unsafe { ctx.device.reset_fences(&current_fences)? };
        unsafe { ctx.device.reset_command_buffer(current_command_buffers[0], ash::vk::CommandBufferResetFlags::RELEASE_RESOURCES)? };
        unsafe {
            self.record_command_buffer(
                &ctx.device,
                &self.command_buffers[self.current_frame],
                swapchain,
                pipeline,
                self.current_frame,
                image_index as usize)?
        };

        self.update_uniform_buffer(swapchain);

        // Image submission to queue
        let wait_semaphore_info = ash::vk::SemaphoreSubmitInfo::default()
            .semaphore(current_wait_semaphores[0])
            .stage_mask(ash::vk::PipelineStageFlags2::COLOR_ATTACHMENT_OUTPUT);

        let signal_semaphore_info = ash::vk::SemaphoreSubmitInfo::default()
            .semaphore(current_signal_semaphores[0])
            .stage_mask(ash::vk::PipelineStageFlags2::ALL_COMMANDS);

        let command_buffer_submit_info = ash::vk::CommandBufferSubmitInfo::default()
            .command_buffer(current_command_buffers[0]);

        let wait_semaphore_infos = [wait_semaphore_info];
        let command_buffer_submit_infos = [command_buffer_submit_info];
        let signal_semaphore_infos = [signal_semaphore_info];

        let submit_info = ash::vk::SubmitInfo2::default()
            .wait_semaphore_infos(&wait_semaphore_infos)
            .command_buffer_infos(&command_buffer_submit_infos)
            .signal_semaphore_infos(&signal_semaphore_infos);

        unsafe { ctx.device.queue_submit2(ctx.queues.graphics, &[submit_info], current_fences[0])? }

        // Image Present
        let swapchains = [swapchain.handle];
        let image_indices = [image_index];

        let present_info = ash::vk::PresentInfoKHR::default()
            .wait_semaphores(&current_signal_semaphores)
            .swapchains(&swapchains)
            .image_indices(&image_indices);

        unsafe { swapchain.loader.queue_present(ctx.queues.graphics, &present_info)? };

        self.current_frame = (self.current_frame + 1) % MAX_FRAMES_IN_FLIGHT as usize;

        Ok(())
    }
}
