use std::{error::Error, ffi::c_void, path::Path, ptr};
use image::{ImageBuffer, Rgba};

use crate::{buffer::DeviceBuffer, image::DeviceImage, vulkan_context::VulkanContext};

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

        let texture = DeviceImage::new(
            ctx,
            tex_width,
            tex_height,
            ash::vk::Format::R8G8B8A8_SRGB,
            ash::vk::ImageTiling::OPTIMAL,
            ash::vk::ImageUsageFlags::TRANSFER_DST | ash::vk::ImageUsageFlags::SAMPLED,
            ash::vk::MemoryPropertyFlags::DEVICE_LOCAL,
        )?;

        Ok(Self { image: texture })
    }
}
