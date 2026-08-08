use std::error::Error;

use crate::vulkan_context::{self, VulkanContext, QueueInfos};

pub struct Swapchain {
    pub swapchain: ash::vk::SwapchainKHR,
}

impl Swapchain {
    pub fn new(context: &vulkan_context::VulkanContext) -> Result<(), Box<dyn Error>> {
        Ok(())
    }

    fn create_swapchain(
        context: &vulkan_context::VulkanContext,
        old_swapchain: Option<ash::vk::SwapchainKHR>
    ) -> Result<ash::vk::SwapchainKHR, Box<dyn Error>> {
        unsafe {
            if !ash::khr::surface::Instance::get_physical_device_surface_support(
                &context.surface_loader,
                context.physical_device,
                context.queues.graphics_queue_index,
                context.surface
            )? {
                return Err("[ERR] Could not find surface support for the current device!".into());
            }
        };

        Ok(())
    }
}
