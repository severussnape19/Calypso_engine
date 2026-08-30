use std::{error::Error, ffi::c_void};

use ash::vk::DeviceMemory;

use crate::{buffer::DeviceBuffer, vulkan_context::VulkanContext};

#[repr(C)]
pub struct UniformBufferObject {
    pub model: glm::Mat4,
    pub view:  glm::Mat4,
    pub proj:  glm::Mat4,
}

pub struct UniformBuffer {
    pub handles: Vec<ash::vk::Buffer>,
    pub memories: Vec<ash::vk::DeviceMemory>,
    pub mappings: Vec<*mut c_void>
}

impl UniformBuffer {
    pub fn new(ctx: &VulkanContext, frames: usize) -> Result<Self, Box<dyn Error>> {
        let mut handles:  Vec<ash::vk::Buffer> = vec![];
        let mut memories: Vec<ash::vk::DeviceMemory> = vec![];
        let mut mappings: Vec<*mut c_void> = vec![];

        for _ in 0..frames {
            let size = std::mem::size_of::<UniformBufferObject>() as u64;
            let (buf, buf_mem) = DeviceBuffer::create_buffer(
                ctx,
                size,
                ash::vk::BufferUsageFlags::UNIFORM_BUFFER,
                ash::vk::MemoryPropertyFlags::HOST_VISIBLE | ash::vk::MemoryPropertyFlags::HOST_COHERENT
            )?;

            handles.push(buf);
            memories.push(buf_mem);

            let mapping = unsafe {
                ctx.device.map_memory(
                    buf_mem,
                    0_u64,
                    size,
                    ash::vk::MemoryMapFlags::default()
                )?
            };
            mappings.push(mapping);
        }

        Ok(Self{
            handles,
            memories,
            mappings
        })
    }

    pub fn destroy_resources(&mut self, device: &ash::Device) {
        unsafe {
            for i in 0..self.handles.len() {
                device.destroy_buffer(self.handles[i], None);
                device.unmap_memory(self.memories[i]);
                device.free_memory(self.memories[i], None);
            }
        }
    }
}
