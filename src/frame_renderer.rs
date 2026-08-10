use core::error;
use std::error::Error;

use glm::{ Vec2, Vec3 };

use crate::{log, pipeline::Vertex, vulkan_context::VulkanContext};

const MAX_FRAMES_IN_FLIGHT: u32 = 2;

pub struct FrameRenderer {

}

impl FrameRenderer {

    pub fn new(ctx: &VulkanContext) -> Result<Self, Box<dyn Error>> {
        let (vertices, indices) = Self::get_data();
        Self::create_vertex_buffer(ctx, vertices)?;

        Ok( Self{} )
    }

    pub unsafe fn destroy_resources(&mut self, ctx: &VulkanContext) {

    }

    fn create_buffer(
        ctx: &VulkanContext,
        buffer_size: &ash::vk::DeviceSize,
        usage_bits: ash::vk::BufferUsageFlags,
        property_flags: ash::vk::MemoryPropertyFlags
    ) -> Result<(ash::vk::Buffer, ash::vk::DeviceMemory), Box<dyn error::Error>> {

        let create_info = ash::vk::BufferCreateInfo::default()
            .sharing_mode(ash::vk::SharingMode::EXCLUSIVE)
            .size(*buffer_size)
            .usage(usage_bits);

        let buffer = unsafe { ctx.device.create_buffer(&create_info, None)? };

        let memory_requirements = unsafe { ctx.device.get_buffer_memory_requirements(buffer) };
        let alloc_info = ash::vk::MemoryAllocateInfo::default()
            .allocation_size(memory_requirements.size)
            .memory_type_index(unsafe { ctx.find_memory_type(
                memory_requirements.memory_type_bits,
                property_flags
            )? });

        let buffer_memory = unsafe { ctx.device.allocate_memory(&alloc_info, None)? };
        unsafe { ctx.device.bind_buffer_memory(buffer, buffer_memory, 0)? };

        Ok((buffer, buffer_memory))
    }

    unsafe fn copy_command_buffer(ctx: &VulkanContext, src: ash::vk::Buffer, dst: ash::vk::Buffer, size: u64) -> Result<(), Box<dyn Error>> {
        let alloc_info = ash::vk::CommandBufferAllocateInfo::default()
            .command_pool(ctx.command_pool)
            .level(ash::vk::CommandBufferLevel::PRIMARY)
            .command_buffer_count(1u32);

        let command_buffer = unsafe {
            ctx.device.allocate_command_buffers(&alloc_info)?
        };

        let cmd_buf_begin_info = ash::vk::CommandBufferBeginInfo::default()
            .flags(ash::vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT);

        // Begin
        unsafe { ctx.device.begin_command_buffer(command_buffer[0], &cmd_buf_begin_info) };

        let regions = [ash::vk::BufferCopy { src_offset: 0, dst_offset: 0, size: size.into() }];
        unsafe { ctx.device.cmd_copy_buffer(command_buffer[0], src, dst, &regions) };

        // End
        unsafe { ctx.device.end_command_buffer(command_buffer[0])? };

        let submit_info = [ash::vk::SubmitInfo::default().command_buffers(&command_buffer)];

        unsafe { ctx.device.queue_submit(ctx.queues.graphics, &submit_info, ash::vk::Fence::null())? };
        unsafe { ctx.device.queue_wait_idle(ctx.queues.graphics)? }

        Ok(())
    }

    fn create_vertex_buffer(ctx: &VulkanContext, vertices: Vec<Vertex>) -> Result<(), Box<dyn Error>> {
        let buffer_size = (std::mem::size_of::<Vertex>() * vertices.len()) as u64;
        // Create on the host side and then transfer to the device
        let (staging_buffer, staging_buffer_memory) = Self::create_buffer(
            ctx,
            &buffer_size,
            ash::vk::BufferUsageFlags::TRANSFER_SRC,
            ash::vk::MemoryPropertyFlags::HOST_VISIBLE | ash::vk::MemoryPropertyFlags::HOST_COHERENT)?;

        let data = unsafe { ctx.device.map_memory(staging_buffer_memory, 0u64, buffer_size, ash::vk::MemoryMapFlags::default())? };

        unsafe { std::ptr::copy_nonoverlapping(
            vertices.as_ptr(),
            data as *mut Vertex,
            vertices.len()
        ) };

        let (vertex_buffer, vertex_buffer_memory) = Self::create_buffer(
            ctx,
            &buffer_size,
            ash::vk::BufferUsageFlags::TRANSFER_DST | ash::vk::BufferUsageFlags::VERTEX_BUFFER,
            ash::vk::MemoryPropertyFlags::DEVICE_LOCAL,
        )?;

        unsafe { Self::copy_command_buffer(ctx, staging_buffer, vertex_buffer, buffer_size) };

        log!(INFO, "Created vertex buffer!");
        Ok(())
    }

    fn get_data() -> (Vec<Vertex>, Vec<u16>) {
        (vec![
            Vertex { pos: Vec2::new(-0.5, -0.5), color: Vec3::new(1.0, 0.0, 0.0)},
            Vertex { pos: Vec2::new( 0.5, -0.5), color: Vec3::new(1.0, 1.0, 0.0)},
            Vertex { pos: Vec2::new( 0.5,  0.5), color: Vec3::new(0.0, 0.0, 1.0)},
            Vertex { pos: Vec2::new(-0.5,  0.5), color: Vec3::new(1.0, 1.0, 1.0)},
        ],
        vec![
            0, 1, 2, 2, 3, 0
        ])
    }
}
