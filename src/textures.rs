use std::{error::Error, ffi::c_void, path::Path, ptr};
use image::{ImageBuffer, Rgba};

use crate::{buffer::DeviceBuffer, command_buffer::CommandBuffers, image::DeviceImage, vulkan_context::VulkanContext};

pub struct Texture {
    image: DeviceImage,
}

impl Texture {
    pub fn new<P: AsRef<Path>>(
        image_path: P,
        ctx: &VulkanContext
    ) -> Result<Self, Box<dyn Error>> {
        let img = image::open("../textures/texture.jpg")?
            .into_rgba8();
        let tex_width = img.width();
        let tex_height = img.height();
        let tex_channels = img.color_space();
        let image_size: u64 = (tex_width * tex_height * 4) as u64;

        let (staging_buf, staging_buf_memory) = DeviceBuffer::new(
            ctx,
            image_size,
            ash::vk::BufferUsageFlags::TRANSFER_SRC,
            ash::vk::MemoryPropertyFlags::HOST_VISIBLE | ash::vk::MemoryPropertyFlags::HOST_COHERENT
        )?;

        unsafe {
            let data = ctx.device.map_memory(
                staging_buf_memory,
                0,
                image_size,
                ash::vk::MemoryMapFlags::default(),
            )?;

            std::ptr::copy_nonoverlapping(
                data,
                img.as_ptr() as *mut c_void,
                image_size as usize
            );

            ctx.device.unmap_memory(staging_buf_memory);
        };

        let texture_image = DeviceImage::new(
            ctx,
            tex_width,
            tex_height,
            ash::vk::Format::R8G8B8A8_SRGB,
            ash::vk::ImageTiling::OPTIMAL,
            ash::vk::ImageUsageFlags::TRANSFER_DST | ash::vk::ImageUsageFlags::SAMPLED,
            ash::vk::MemoryPropertyFlags::DEVICE_LOCAL,
        )?;

        // copy staging buffer memory to image
        let command_buffer = CommandBuffers::new(&ctx.device, &ctx.command_pool, 1)?;
        command_buffer.begin(&ctx.device, &command_buffer.buffers[0]);
        DeviceImage::transition_image_layout(
            &ctx.device,
            &command_buffer.buffers[0],
            texture_image.handle,
            ash::vk::ImageLayout::UNDEFINED,
            ash::vk::ImageLayout::TRANSFER_DST_OPTIMAL,
            ash::vk::AccessFlags2::default(),
            ash::vk::AccessFlags2::TRANSFER_WRITE,
            ash::vk::PipelineStageFlags2::TOP_OF_PIPE,
            ash::vk::PipelineStageFlags2::TRANSFER,
            ash::vk::ImageAspectFlags::default()
        );

        texture_image.copy_buffer_to_image(
            &staging_buf,
            &ctx.device,
            &ctx.command_pool,
            &command_buffer.buffers[0],
            tex_width,
            tex_height
        );

        DeviceImage::transition_image_layout(
            &ctx.device,
            &command_buffer.buffers[0],
            texture_image.handle,
            ash::vk::ImageLayout::TRANSFER_DST_OPTIMAL,
            ash::vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
            ash::vk::AccessFlags2::TRANSFER_WRITE,
            ash::vk::AccessFlags2::SHADER_READ,
            ash::vk::PipelineStageFlags2::TRANSFER,
            ash::vk::PipelineStageFlags2::FRAGMENT_SHADER,
            ash::vk::ImageAspectFlags::default()
        );
        command_buffer.end(&ctx.device, &command_buffer.buffers[0]);

        Ok(Self { image: texture_image })
    }

    pub fn create_texture_sampler() -> Result<(), Box<dyn Error>> { todo!() }

    pub fn destroy_resources(&mut self, device: &ash::Device) {
        unsafe {
            self.image.destroy_resources(device);
        }
    }
}
