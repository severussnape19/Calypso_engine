use core::error;
use std::{char::MAX, error::Error, ffi::c_void, ops::BitOr, sync::LazyLock, time::Instant};

use ash::{khr::swapchain, nv::descriptor_pool_overallocation, vk::{CommandBuffer, Semaphore}};
use glm::{ Mat4x2, Vec2, Vec3, ext::{look_at, perspective, rotate} };

use crate::{buffer::DeviceBuffer, descriptor::UniformDescriptor, log, mesh::Mesh, pipeline::{self, Pipeline}, swapchain::Swapchain, sync::SyncObjects, uniform::{self, UniformBuffer, UniformBufferObject}, vulkan_context::VulkanContext, warn};

const MAX_FRAMES_IN_FLIGHT: u32 = 2;
static START_TIME: LazyLock<Instant> = LazyLock::new(Instant::now);

pub struct FrameRenderer {
    mesh: Mesh,
    vbo: DeviceBuffer,
    ibo: DeviceBuffer,
    ubo: UniformBuffer,
    uniform_descriptor: UniformDescriptor,
    command_buffers: Vec<ash::vk::CommandBuffer>,
    sync_objects : SyncObjects,
    current_frame: usize,
    framebuffer_resized: bool,
}

impl FrameRenderer {
    pub fn new(ctx: &VulkanContext, swapchain: &Swapchain, pipeline: &Pipeline) -> Result<Self, Box<dyn Error>> {
        let descriptor_pool = UniformDescriptor::create_descriptor_pool(ctx, MAX_FRAMES_IN_FLIGHT)?;
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

        let sync_objects: SyncObjects = SyncObjects::new(&ctx.device, MAX_FRAMES_IN_FLIGHT as usize)?;
        let current_frame = 0;

        let command_buffers = Self::create_command_buffer(ctx)?;

        let ubo = UniformBuffer::new(ctx, MAX_FRAMES_IN_FLIGHT as usize)?;

        let uniform_descriptor = UniformDescriptor::new(
            ctx,
            pipeline.descriptor_set_layout,
            descriptor_pool,
            &ubo,
            MAX_FRAMES_IN_FLIGHT
        )?;

        Ok( Self{
            mesh,
            vbo,
            ibo,
            ubo,
            uniform_descriptor,
            command_buffers,
            sync_objects,
            current_frame,
            framebuffer_resized: false,
        } )
    }

    pub unsafe fn destroy_resources(&mut self, ctx: &VulkanContext) {
        unsafe {
            let _ = ctx.device.device_wait_idle();
            ctx.device.free_command_buffers(ctx.command_pool, &self.command_buffers);

            self.sync_objects.destroy(&ctx.device);

            warn!(WARN, "Render objects destroyed!");
            self.vbo.destroy_resources(&ctx.device);
            self.ibo.destroy_resources(&ctx.device);
            self.ubo.destroy_resources(&ctx.device);
            self.uniform_descriptor.destroy_resources(&ctx.device);
        }
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
            std::slice::from_ref(&self.uniform_descriptor.sets[self.current_frame]),
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
            let dst = self.ubo.mappings[self.current_frame] as *mut UniformBufferObject;
            *dst = ubo;
        }
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
