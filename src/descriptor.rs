use std::error::Error;

use crate::{textures::Texture, uniform::{UniformBuffer, UniformBufferObject}, vulkan_context::VulkanContext};

pub struct Descriptor {
    pub pool: ash::vk::DescriptorPool,
    pub sets: Vec<ash::vk::DescriptorSet>
}

impl Descriptor {
    pub fn create_descriptor_pool(ctx: &VulkanContext, frames: u32) -> Result<(ash::vk::DescriptorPool), Box<dyn Error>> {
        let pool_info_uniform= ash::vk::DescriptorPoolSize::default()
            .ty(ash::vk::DescriptorType::UNIFORM_BUFFER)
            .descriptor_count(frames);

        let pool_info_sampler= ash::vk::DescriptorPoolSize::default()
            .ty(ash::vk::DescriptorType::COMBINED_IMAGE_SAMPLER)
            .descriptor_count(frames);

        let pool_size = [pool_info_uniform, pool_info_sampler];

        let create_info = ash::vk::DescriptorPoolCreateInfo::default()
            .flags(ash::vk::DescriptorPoolCreateFlags::FREE_DESCRIPTOR_SET)
            .max_sets(frames)
            .pool_sizes(&pool_size);
        Ok(unsafe { ctx.device.create_descriptor_pool(&create_info, None)? })
    }

    pub fn new(
        ctx: &VulkanContext,
        descriptor_set_layout: ash::vk::DescriptorSetLayout,
        descriptor_pool: ash::vk::DescriptorPool,
        uniform_buffers: &UniformBuffer,
        texture: &Texture,
        frames: u32
    ) -> Result<Self, Box<dyn Error>> {

        // one descriptor set for each frame in flight
        let layouts: Vec<ash::vk::DescriptorSetLayout> = vec![descriptor_set_layout; frames as usize];
        let alloc_info = ash::vk::DescriptorSetAllocateInfo::default()
            .descriptor_pool(descriptor_pool)
            .set_layouts(&layouts);

        let descriptor_sets = unsafe { ctx.device.allocate_descriptor_sets(&alloc_info)? };

        // Descriptor sets are allocated but not yet configured
        for i in 0..frames as usize {
            let buffer_info = ash::vk::DescriptorBufferInfo::default()
                .buffer(uniform_buffers.handles[i])
                .offset(0_u64)
                .range(std::mem::size_of::<UniformBufferObject>() as u64);

            let image_info = ash::vk::DescriptorImageInfo::default()
                .sampler(texture.sampler)
                .image_view(texture.image.view.unwrap())
                .image_layout(texture.image_layout);

            let descriptors_write_buf = ash::vk::WriteDescriptorSet::default()
                .dst_set(descriptor_sets[i])
                .dst_binding(0_u32)
                .dst_array_element(0_u32)
                .descriptor_count(1_u32)
                .descriptor_type(ash::vk::DescriptorType::UNIFORM_BUFFER)
                .buffer_info(std::slice::from_ref(&buffer_info));

            let descriptors_write_img = ash::vk::WriteDescriptorSet::default()
                .dst_set(descriptor_sets[i])
                .dst_binding(1_u32)
                .dst_array_element(0_u32)
                .descriptor_count(1_u32)
                .descriptor_type(ash::vk::DescriptorType::COMBINED_IMAGE_SAMPLER)
                .image_info(std::slice::from_ref(&image_info));

            let descriptor_writes = [descriptors_write_buf, descriptors_write_img];
            unsafe { ctx.device.update_descriptor_sets(
                &descriptor_writes,
                &[]); }
        }
        Ok(Self{
            pool: descriptor_pool,
            sets: descriptor_sets
        })
    }

    pub fn destroy_resources(&mut self, device: &ash::Device) {
        unsafe {
            device.destroy_descriptor_pool(self.pool, None);
        }
    }
}
