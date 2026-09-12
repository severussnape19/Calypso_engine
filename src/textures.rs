use std::{error::Error, ffi::c_void, path::Path, ptr};
use image::{ImageBuffer, Rgba};

use crate::{buffer::DeviceBuffer, command_buffer::CommandBuffers, image::DeviceImage, vulkan_context::VulkanContext};

pub struct Texture {
    pub image: DeviceImage,
    pub sampler: ash::vk::Sampler,
    pub image_layout: ash::vk::ImageLayout,
}

impl Texture {
    pub fn new<P: AsRef<Path>>(
        image_path: P,
        ctx: &VulkanContext
    ) -> Result<Self, Box<dyn Error>> {
        let img = image::open(image_path)?
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
                img.as_ptr(),
                data as *mut u8,
                image_size as usize
            );

            ctx.device.unmap_memory(staging_buf_memory);
        };

        let texture_image = DeviceImage::new(
            ctx,
            ash::vk::ImageType::TYPE_2D,
            ash::vk::Extent3D::default().width(tex_width).height(tex_height).depth(1_u32),
            ash::vk::Format::R8G8B8A8_SRGB,
            ash::vk::ImageTiling::OPTIMAL,
            ash::vk::ImageUsageFlags::TRANSFER_DST | ash::vk::ImageUsageFlags::SAMPLED,
            ash::vk::MemoryPropertyFlags::DEVICE_LOCAL,
            true
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
            ash::vk::ImageAspectFlags::COLOR
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
            ash::vk::ImageAspectFlags::COLOR
        );

        command_buffer.end(&ctx.device, &command_buffer.buffers[0]);

        let submit_info = ash::vk::SubmitInfo::default()
            .command_buffers(&command_buffer.buffers);
        unsafe {
            ctx.device.queue_submit(ctx.queues.graphics, std::slice::from_ref(&submit_info), ash::vk::Fence::default());
            ctx.device.device_wait_idle();

            ctx.device.destroy_buffer(staging_buf, None);
            ctx.device.free_memory(staging_buf_memory, None);
        }



        Ok(Self {
            image: texture_image,
            sampler: Self::create_texture_sampler(ctx)?,
            image_layout: ash::vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
        })
    }

    pub fn create_texture_sampler(ctx: &VulkanContext) -> Result<ash::vk::Sampler, Box<dyn Error>> {
        let device_props = unsafe { ctx.instance.get_physical_device_properties(ctx.physical_device) };
        let sampler_info = ash::vk::SamplerCreateInfo::default()
            .mag_filter(ash::vk::Filter::LINEAR) // linear interpolation
            .min_filter(ash::vk::Filter::LINEAR)
            .mipmap_mode(ash::vk::SamplerMipmapMode::LINEAR)
            .address_mode_u(ash::vk::SamplerAddressMode::REPEAT)
            .address_mode_v(ash::vk::SamplerAddressMode::REPEAT)
            .address_mode_w(ash::vk::SamplerAddressMode::REPEAT)
            .anisotropy_enable(true)
            .max_anisotropy(device_props.limits.max_sampler_anisotropy)
            .compare_enable(false)
            .compare_op(ash::vk::CompareOp::ALWAYS)
            .border_color(ash::vk::BorderColor::INT_OPAQUE_BLACK)
            .unnormalized_coordinates(false)
            .mip_lod_bias(0_f32)
            .min_lod(0_f32)
            .max_lod(0_f32);

        Ok(unsafe {
            ctx.device.create_sampler(&sampler_info, None)?
        })
    }

    pub fn destroy_resources(&mut self, device: &ash::Device) {
        unsafe {
            device.destroy_sampler(self.sampler, None);
            self.image.destroy_resources(device);
        }
    }
}
