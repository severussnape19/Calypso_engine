use std::error::Error;

use ash::vk::MemoryRequirements;

use crate::{buffer::DeviceBuffer, vulkan_context::VulkanContext};

pub struct DeviceImage {
    pub handle: ash::vk::Image,
    pub memory: ash::vk::DeviceMemory,
    pub view:   Option<ash::vk::ImageView>,
    pub size:   ash::vk::DeviceSize,
    pub usage_flags: ash::vk::ImageUsageFlags,
    pub property_flags: ash::vk::MemoryPropertyFlags,
}

impl DeviceImage {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        ctx: &VulkanContext, extent: ash::vk::Extent3D, format_: ash::vk::Format,
        tiling: ash::vk::ImageTiling, usage_flags: ash::vk::ImageUsageFlags,
        memory_properties: ash::vk::MemoryPropertyFlags, create_views: bool
    ) -> Result<Self, Box<dyn Error>> {
        let depth: u32 = 1;
        let image_info = ash::vk::ImageCreateInfo::default()
            .format(format_)
            .extent(extent)
            .mip_levels(1_u32)
            .array_layers(1_u32)
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

        let image_views = match create_views {
            true => Some(Self::create_view(&ctx.device, &image, format_)?),
            false => None,
        };

        Ok(DeviceImage {
            handle: image,
            memory: image_memory,
            view: image_views,
            size: mem_reqs.size,
            usage_flags,
            property_flags: memory_properties
        })
    }

    pub fn create_view(device: &ash::Device, image: &ash::vk::Image, format: ash::vk::Format) -> Result<ash::vk::ImageView, Box<dyn Error>> {
        let view_info = ash::vk::ImageViewCreateInfo::default()
            .image(*image)
            .view_type(ash::vk::ImageViewType::TYPE_2D)
            .format(format)
            .subresource_range(ash::vk::ImageSubresourceRange::default()
                .aspect_mask(ash::vk::ImageAspectFlags::COLOR)
                .base_mip_level(0_u32)
                .level_count(1_u32)
                .base_array_layer(0_u32)
                .layer_count(1_u32)
            );
        Ok(unsafe { device.create_image_view(&view_info, None)? })
    }

    #[allow(clippy::too_many_arguments)]
    pub fn transition_image_layout(
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

    pub fn copy_buffer_to_image(
        &self, buffer: &ash::vk::Buffer,
        device: &ash::Device,
        cmd_pool: &ash::vk::CommandPool,
        cmd_buf: &ash::vk::CommandBuffer,
        width: u32, height: u32,
    ) -> Result<(), Box<dyn Error>> {
        let region = ash::vk::BufferImageCopy::default()
            .buffer_offset(0_u64)
            .image_offset(ash::vk::Offset3D{x: 0, y: 0, z: 0})
            .buffer_row_length(0_u32)
            .buffer_image_height(0_u32)
            .image_subresource(ash::vk::ImageSubresourceLayers {
                aspect_mask: ash::vk::ImageAspectFlags::COLOR,
                mip_level: 0_u32,
                base_array_layer: 0_u32,
                layer_count: 1_u32,
            })
            .image_extent(ash::vk::Extent3D::default().width(width).height(height).depth(1));

        unsafe { device.cmd_copy_buffer_to_image(
            *cmd_buf,
            *buffer,
            self.handle,
            ash::vk::ImageLayout::TRANSFER_DST_OPTIMAL,
            std::slice::from_ref(&region))
        };

        Ok(())
    }

    pub fn destroy_resources(&mut self, device: &ash::Device) {
        unsafe {
            device.destroy_image(self.handle, None);
            if let Some(view) = self.view {
                device.destroy_image_view(view, None);
            }
        }
    }
}
