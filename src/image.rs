use std::error::Error;

use ash::vk::MemoryRequirements;

use crate::{buffer::DeviceBuffer, vulkan_context::VulkanContext};

pub struct DeviceImage {
    pub handle: ash::vk::Image,
    pub memory: ash::vk::DeviceMemory,
    pub size:   ash::vk::DeviceSize,
    pub usage_flags: ash::vk::ImageUsageFlags,
    pub property_flags: ash::vk::MemoryPropertyFlags,
}

impl DeviceImage {
    pub fn new(
        ctx: &VulkanContext,
        width: u32, height: u32,
        format_: ash::vk::Format,
        tiling: ash::vk::ImageTiling,
        usage_flags: ash::vk::ImageUsageFlags,
        memory_properties: ash::vk::MemoryPropertyFlags,
    ) -> Result<Self, Box<dyn Error>> {
        let depth: u32 = 1;
        let image_info = ash::vk::ImageCreateInfo::default()
            .format(format_)
            .extent(ash::vk::Extent3D {
                width, height, depth
            }).mip_levels(1)
            .array_layers(1)
            .samples(ash::vk::SampleCountFlags::TYPE_1)
            .tiling(tiling)
            .usage(usage_flags)
            .sharing_mode(ash::vk::SharingMode::EXCLUSIVE
        );

        let image = unsafe { ctx.device.create_image(&image_info, None)? };

        let mem_reqs = unsafe { ctx.device.get_image_memory_requirements(image) };
        let alloc_info = ash::vk::MemoryAllocateInfo::default()
            .allocation_size(mem_reqs.size)
            .memory_type_index(DeviceBuffer::find_memory_type(ctx, mem_reqs.memory_type_bits, memory_properties)?);

        let image_memory = unsafe { ctx.device.allocate_memory(&alloc_info, None)? };
        unsafe { ctx.device.bind_image_memory(image, image_memory, 0) };

        Ok(DeviceImage {
            handle: image,
            memory: image_memory,
            size: mem_reqs.size,
            usage_flags,
            property_flags: memory_properties
        })
    }
}
