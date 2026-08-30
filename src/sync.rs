use std::error::Error;

use crate::{vulkan_context::VulkanContext, log};

pub struct SyncObjects {
    pub present_complete_semaphores: Vec<ash::vk::Semaphore>,
    pub render_finish_semaphores: Vec<ash::vk::Semaphore>,
    pub inflight_fences: Vec<ash::vk::Fence>,
}

impl SyncObjects {
    pub fn new(device: &ash::Device, n: usize) -> Result<SyncObjects, Box<dyn Error>> {
        let semaphore_info = ash::vk::SemaphoreCreateInfo::default();
        let fence_info = ash::vk::FenceCreateInfo::default().flags(ash::vk::FenceCreateFlags::SIGNALED);

        let mut present_complete_semaphores: Vec<ash::vk::Semaphore> = Vec::with_capacity(n);
        let mut render_finish_semaphores: Vec<ash::vk::Semaphore> = Vec::with_capacity(n);
        let mut inflight_fences: Vec<ash::vk::Fence> = Vec::with_capacity(n);

        for _ in 0..n {
            present_complete_semaphores.push(unsafe { device.create_semaphore(&semaphore_info, None)? });
            render_finish_semaphores.push(unsafe { device.create_semaphore(&semaphore_info, None)? });
            inflight_fences.push(unsafe { device.create_fence(&fence_info, None)? });
        }
        log!(INFO, "Sync objects created!");
        Ok(SyncObjects { present_complete_semaphores, render_finish_semaphores, inflight_fences })
    }

    pub fn destroy(&mut self, device: &ash::Device) {
        unsafe {
            for i in 0..self.render_finish_semaphores.len() {
                device.destroy_semaphore(self.present_complete_semaphores[i], None);
                device.destroy_semaphore(self.render_finish_semaphores[i], None);
                device.destroy_fence(self.inflight_fences[i], None);
            }
        }
    }
}
