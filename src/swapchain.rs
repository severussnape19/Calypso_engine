use std::{error::Error, mem::swap};

use ash::khr::{get_surface_capabilities2, swapchain};

use crate::{log, vulkan_context::{self, DeviceQueues, VulkanContext}};

pub struct SwapchainConfig {
    pub min_image_count: u32,
    pub max_image_count: u32,
    pub surface_format: ash::vk::SurfaceFormatKHR,
    pub extent: ash::vk::Extent2D,
    pub present_mode: ash::vk::PresentModeKHR,
}

pub struct Swapchain {
    pub loader: ash::khr::swapchain::Device,
    pub handle: ash::vk::SwapchainKHR,
    pub config: SwapchainConfig,
    pub images: Vec<ash::vk::Image>,
    pub image_views: Vec<ash::vk::ImageView>,
}

impl Swapchain {
    pub fn new(context: &vulkan_context::VulkanContext) -> Result<Self, Box<dyn Error>> {
        let device_loader = unsafe { ash::khr::swapchain::Device::new(&context.instance, &context.device) };
        let (swapchain_config, swapchain, images) = Self::create_swapchain(context, &device_loader, None)?;
        let image_views: Vec<ash::vk::ImageView> = Self::create_swapchain_image_views(context, &images, &swapchain_config)?;
        Ok(Swapchain {
            loader: device_loader,
            handle: swapchain,
            config: swapchain_config,
            images,
            image_views,
        })
    }

    fn create_swapchain(
        context: &vulkan_context::VulkanContext,
        loader: &ash::khr::swapchain::Device,
        old_swapchain: Option<ash::vk::SwapchainKHR>
    ) -> Result<(SwapchainConfig, ash::vk::SwapchainKHR, Vec<ash::vk::Image>), Box<dyn Error>> {
        unsafe {
            if !ash::khr::surface::Instance::get_physical_device_surface_support(
                    &context.surface_loader,
                    context.physical_device,
                    context.queues.graphics_family,
                    context.surface)?
            {
                return Err("[ERR] Could not find surface support for the current device!".into());
            }
        };

        let surface_capabilities = unsafe {
            context.surface_loader.get_physical_device_surface_capabilities(
            context.physical_device,
            context.surface)?
        };

        let capabilities = Self::get_capabilities(context, &surface_capabilities)?;

        let mut swapchain_create_info = ash::vk::SwapchainCreateInfoKHR::default()
            .surface(context.surface)
            .min_image_count(capabilities.min_image_count)
            .present_mode(capabilities.present_mode)
            .image_array_layers(1u32)
            .image_usage(ash::vk::ImageUsageFlags::COLOR_ATTACHMENT)
            .image_format(capabilities.surface_format.format)
            .image_color_space(capabilities.surface_format.color_space)
            .image_extent(capabilities.extent)
            .image_sharing_mode(ash::vk::SharingMode::EXCLUSIVE)
            .pre_transform(surface_capabilities.current_transform)
            .clipped(true)
            .composite_alpha(ash::vk::CompositeAlphaFlagsKHR::OPAQUE)
            .old_swapchain(old_swapchain.unwrap_or_default());

        let swapchain     = unsafe { ash::khr::swapchain::Device::create_swapchain(loader, &swapchain_create_info, None)? };
        let swapchain_images = unsafe { loader.get_swapchain_images(swapchain)? };

        log!(INFO, "Created swapchain!");
        Ok((
            capabilities,
            swapchain,
            swapchain_images
        ))
    }

    fn get_capabilities(context: &VulkanContext, surface_capabilities: &ash::vk::SurfaceCapabilitiesKHR)
        -> Result<SwapchainConfig, Box<dyn Error>> {

        // thriple buffering if available else double buffering
        let mut min_image_count: u32 = 3u32.min(surface_capabilities.min_image_count);
        let max_image_count: u32 = surface_capabilities.max_image_count;

        if max_image_count > 0 && min_image_count > max_image_count {
            min_image_count = max_image_count;
        }

        let surface_formats: Vec<ash::vk::SurfaceFormatKHR> = unsafe {
            context.surface_loader.get_physical_device_surface_formats(
                context.physical_device,
                context.surface
            )?
        };

        let surface_format = surface_formats
            .iter()
            .find(|&fmt| {
                fmt.format == ash::vk::Format::B8G8R8_SRGB &&
                fmt.color_space == ash::vk::ColorSpaceKHR::SRGB_NONLINEAR
            })
            .or_else(|| surface_formats.first())
            .copied()
            .unwrap();

        let swapchain_extent =
            if surface_capabilities.current_extent.width >= surface_capabilities.max_image_extent.width {
                surface_capabilities.max_image_extent
            } else {
                surface_capabilities.current_extent
            };

        log!(INFO, "width: {} | height: {}", swapchain_extent.width, swapchain_extent.height);

        let present_modes = unsafe {
            context.surface_loader.get_physical_device_surface_present_modes(
                context.physical_device,
                context.surface)?
        };

        let present_mode = match present_modes.iter().any(|&mode| {
            mode == ash::vk::PresentModeKHR::MAILBOX
        }) {
            true  => ash::vk::PresentModeKHR::MAILBOX,
            false => ash::vk::PresentModeKHR::FIFO,
        };

        log!(INFO, "Fetched capabilities!");
        Ok(SwapchainConfig {
            min_image_count,
            max_image_count,
            surface_format,
            extent: swapchain_extent,
            present_mode
        })
    }

    fn create_swapchain_image_views(
        context: &VulkanContext,
        swapchain_images: &Vec<ash::vk::Image>,
        swapchain_config: &SwapchainConfig
    ) -> Result<Vec<ash::vk::ImageView>, Box<dyn Error>> {

        let mut image_views: Vec<ash::vk::ImageView> = vec![];
        for image in swapchain_images {
            let image_view_create_info = ash::vk::ImageViewCreateInfo::default()
                .image(*image)
                .format(swapchain_config.surface_format.format)
                .view_type(ash::vk::ImageViewType::TYPE_2D)
                .components(ash::vk::ComponentMapping::default())
                .subresource_range(ash::vk::ImageSubresourceRange{
                    aspect_mask: ash::vk::ImageAspectFlags::COLOR,
                    base_mip_level: 0,
                    level_count: 1,
                    base_array_layer: 0,
                    layer_count: 1
                });

            image_views.push(unsafe { context.device.create_image_view(&image_view_create_info, None)? });
        }

        log!(INFO, "Created Swapchain Image Views!");
        Ok(image_views)
    }

    pub unsafe fn destroy(&mut self, device: &ash::Device) {
        for view in &self.image_views {
            unsafe { device.destroy_image_view(*view, None) };
        }
        self.image_views.clear();

        if self.handle != ash::vk::SwapchainKHR::null() {
            unsafe { self.loader.destroy_swapchain(self.handle, None) };
            self.handle = ash::vk::SwapchainKHR::null();
        }
    }
}
