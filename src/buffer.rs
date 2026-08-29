use std::{error::Error, ffi::c_void};

use crate::{pipeline::Vertex, vulkan_context::VulkanContext};

pub struct DeviceBuffer {
    pub handle: ash::vk::Buffer,
    pub memory: ash::vk::DeviceMemory,
    pub size  : ash::vk::DeviceSize,
    pub usage_flags: ash::vk::BufferUsageFlags,
    pub property_flags: ash::vk::MemoryPropertyFlags
}

impl DeviceBuffer {
    pub fn create_buffer(
        ctx: &VulkanContext,
        buffer_size: ash::vk::DeviceSize,
        usage_bits: ash::vk::BufferUsageFlags,
        property_flags: ash::vk::MemoryPropertyFlags
    ) -> Result<(ash::vk::Buffer, ash::vk::DeviceMemory), Box<dyn Error>> {

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

    fn copy_command_buffer(ctx: &VulkanContext, src: ash::vk::Buffer, dst: ash::vk::Buffer, size: u64) -> Result<(), Box<dyn Error>> {
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

        unsafe { ctx.device.free_command_buffers(ctx.command_pool, &command_buffer); }

        Ok(())
    }

    fn create_buffer_from_slice<T: Copy>(
        ctx: &VulkanContext,
        data: &[T]
    ) -> Result<Self, Box<dyn Error>> {
        let size =  std::mem::size_of_val(data) as ash::vk::DeviceSize;
        let (staging_buffer, staging_buffer_memory) = Self::create_buffer(
            ctx,
            size,
            ash::vk::BufferUsageFlags::TRANSFER_SRC,
            ash::vk::MemoryPropertyFlags::HOST_COHERENT | ash::vk::MemoryPropertyFlags::HOST_VISIBLE
        )?;

        let mapped_data = unsafe { ctx.device.map_memory(staging_buffer_memory, 0_u64, size, ash::vk::MemoryMapFlags::default())? };

        unsafe {
            std::ptr::copy_nonoverlapping(
                data.as_ptr(),
                mapped_data as *mut T,
                data.len()
            )
        };

        unsafe { ctx.device.unmap_memory(staging_buffer_memory) };

        Ok(Self {
            handle: staging_buffer,
            memory: staging_buffer_memory,
            size,
            usage_flags: ash::vk::BufferUsageFlags::TRANSFER_SRC,
            property_flags: ash::vk::MemoryPropertyFlags::HOST_COHERENT | ash::vk::MemoryPropertyFlags::HOST_VISIBLE
        })
    }

    pub fn create_vertex_buffer<T: Copy>(
        ctx: &VulkanContext,
        usage_bits: ash::vk::BufferUsageFlags,
        property_flags: ash::vk::MemoryPropertyFlags,
        data: &[T]
    ) -> Result<Self, Box<dyn Error>> {
        let staging_buffer: DeviceBuffer = Self::create_buffer_from_slice(ctx, data)?;
        let size = staging_buffer.size;

        let (vertex_buffer, vertex_buffer_memory) = Self::create_buffer(
            ctx,
            size,
            usage_bits | ash::vk::BufferUsageFlags::TRANSFER_DST,
            property_flags
        )?;

        Self::copy_command_buffer(ctx, staging_buffer.handle, vertex_buffer, size);
        Ok (
            Self {
                handle: vertex_buffer,
                memory: vertex_buffer_memory,
                size,
                usage_flags: usage_bits,
                property_flags
            }
        )
    }

    pub fn create_index_buffer<T: Copy>(
        ctx: &VulkanContext,
        usage_bits: ash::vk::BufferUsageFlags,
        property_flags: ash::vk::MemoryPropertyFlags,
        data: &[T]
    ) -> Result<Self, Box<dyn Error>> {
        let staging_buffer: DeviceBuffer = Self::create_buffer_from_slice(ctx, data)?;
        let size = staging_buffer.size;

        let (index_buffer, index_buffer_memory) = Self::create_buffer(
            ctx,
            size,
            usage_bits | ash::vk::BufferUsageFlags::TRANSFER_DST,
            property_flags
        )?;

        Self::copy_command_buffer(ctx, staging_buffer.handle, index_buffer, size);
        Ok (
            Self {
                handle: index_buffer,
                memory: index_buffer_memory,
                size,
                usage_flags: usage_bits,
                property_flags
            }
        )
    }

    pub fn destroy_resources(&self, ctx: &VulkanContext) {
        unsafe {
            ctx.device.destroy_buffer(self.handle, None);
            ctx.device.free_memory(self.memory, None);
        }
    }
}
