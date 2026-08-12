use core::error;
use std::error::Error;

use glm::{ Vec2, Vec3 };

use crate::{log, pipeline::Vertex, vulkan_context::VulkanContext};

const MAX_FRAMES_IN_FLIGHT: u32 = 2;

pub struct FrameRenderer {
    vertex_buffer: ash::vk::Buffer,
    vertex_memory: ash::vk::DeviceMemory,
    index_buffer : ash::vk::Buffer,
    index_memory : ash::vk::DeviceMemory,
}

impl FrameRenderer {

    pub fn new(ctx: &VulkanContext) -> Result<Self, Box<dyn Error>> {
        let (vertices, indices) = Self::get_data();
        let (vertex_buffer, vertex_memory) = Self::create_vertex_buffer(ctx, vertices)?;
        let (index_buffer, index_memory)   = Self::create_index_buffer(ctx, indices)?;

        Ok( Self{
            vertex_buffer,
            vertex_memory,
            index_buffer,
            index_memory
        } )
    }

    pub unsafe fn destroy_resources(&mut self, ctx: &VulkanContext) {
        unsafe {
            ctx.device.destroy_buffer(self.vertex_buffer, None);
            ctx.device.free_memory(self.vertex_memory, None);

            ctx.device.destroy_buffer(self.index_buffer, None);
            ctx.device.free_memory(self.index_memory, None);
        }
    }

    fn create_buffer(
        ctx: &VulkanContext,
        buffer_size: ash::vk::DeviceSize,
        usage_bits: ash::vk::BufferUsageFlags,
        property_flags: ash::vk::MemoryPropertyFlags
    ) -> Result<(ash::vk::Buffer, ash::vk::DeviceMemory), Box<dyn error::Error>> {

        let create_info = ash::vk::BufferCreateInfo::default()
            .sharing_mode(ash::vk::SharingMode::EXCLUSIVE)
            .size(buffer_size)
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

        let regions = [ash::vk::BufferCopy { src_offset: 0, dst_offset: 0, size }];
        unsafe { ctx.device.cmd_copy_buffer(command_buffer[0], src, dst, &regions) };

        // End
        unsafe { ctx.device.end_command_buffer(command_buffer[0])? };

        let submit_info = [ash::vk::SubmitInfo::default().command_buffers(&command_buffer)];

        unsafe { ctx.device.queue_submit(ctx.queues.graphics, &submit_info, ash::vk::Fence::null())? };
        unsafe { ctx.device.queue_wait_idle(ctx.queues.graphics)? }

        Ok(())
    }

    fn create_vertex_buffer(ctx: &VulkanContext, vertices: Vec<Vertex>) -> Result<(ash::vk::Buffer, ash::vk::DeviceMemory), Box<dyn Error>> {
        let buffer_size = (std::mem::size_of::<Vertex>() * vertices.len()) as u64;
        // Create on the host side and then transfer to the device
        let (staging_buffer, staging_buffer_memory) = Self::create_buffer(
            ctx,
            buffer_size,
            ash::vk::BufferUsageFlags::TRANSFER_SRC,
            ash::vk::MemoryPropertyFlags::HOST_VISIBLE | ash::vk::MemoryPropertyFlags::HOST_COHERENT)?;

        let data = unsafe { ctx.device.map_memory(staging_buffer_memory, 0u64, buffer_size, ash::vk::MemoryMapFlags::default())? };

        unsafe { std::ptr::copy_nonoverlapping(
            vertices.as_ptr(),
            data as *mut Vertex,
            vertices.len()
        ) };

        unsafe { ctx.device.unmap_memory(staging_buffer_memory) };

        let (vertex_buffer, vertex_buffer_memory) = Self::create_buffer(
            ctx,
            buffer_size,
            ash::vk::BufferUsageFlags::TRANSFER_DST | ash::vk::BufferUsageFlags::VERTEX_BUFFER,
            ash::vk::MemoryPropertyFlags::DEVICE_LOCAL,
        )?;

        unsafe { Self::copy_command_buffer(ctx, staging_buffer, vertex_buffer, buffer_size) };

        unsafe {
            ctx.device.destroy_buffer(staging_buffer, None);
            ctx.device.free_memory(staging_buffer_memory, None);
        }

        log!(INFO, "Created vertex buffer!");
        Ok((vertex_buffer, vertex_buffer_memory))
    }

    fn create_index_buffer(ctx: &VulkanContext, indices: Vec<u16>) -> Result<(ash::vk::Buffer, ash::vk::DeviceMemory), Box<dyn Error>> {
        let buffer_size: u64 = (std::mem::size_of::<u16>() * indices.len()) as u64;
        let (staging_index_buffer, staging_index_buf_mem) = Self::create_buffer(
            ctx,
            buffer_size,
            ash::vk::BufferUsageFlags::TRANSFER_SRC,
            ash::vk::MemoryPropertyFlags::HOST_VISIBLE | ash::vk::MemoryPropertyFlags::HOST_COHERENT,
        )?;

        /* Host Coherent bit makes it so that the content written in the mapped region is shown to
           the GPU right away without needing manual flushing */

        let data = unsafe { ctx.device.map_memory(staging_index_buf_mem, 0u64, buffer_size, ash::vk::MemoryMapFlags::default())? };

        unsafe {
            std::ptr::copy_nonoverlapping(indices.as_ptr(), data as *mut u16, buffer_size as usize);
        }

        let (index_buf, index_mem) = Self::create_buffer(
            ctx,
            buffer_size,
            ash::vk::BufferUsageFlags::TRANSFER_DST | ash::vk::BufferUsageFlags::INDEX_BUFFER,
            ash::vk::MemoryPropertyFlags::DEVICE_LOCAL)?;

        unsafe {
            Self::copy_command_buffer(
                ctx,
                staging_index_buffer,
                index_buf,
                buffer_size)?;
        }

        unsafe {
            ctx.device.destroy_buffer(staging_index_buffer, None);
            ctx.device.free_memory(staging_index_buf_mem, None);
        }

        log!(INFO, "Created index buffer!");
        Ok((index_buf, index_mem))
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
