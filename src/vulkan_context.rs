use std::{ collections::{HashMap, HashSet}, error::Error, ffi::{CStr, CString}, hash::Hash, os::raw::c_void, result};
use crate::{log, warn};
use ash::{ Entry, ext::{acquire_drm_display::Instance, debug_utils, extended_dynamic_state}, khr::surface, vk::{self, DebugUtilsMessengerCreateInfoEXT, QueueFamilyProperties} };
use raw_window_handle::HasWindowHandle;
use winit::{self, application::ApplicationHandler, dpi::PixelUnit::Physical, event::WindowEvent, event_loop::{ControlFlow, EventLoop}, window::Window};
use winit::raw_window_handle::HasDisplayHandle;

pub struct DeviceQueues {
    pub graphics_family: u32,
    pub transfer_family: u32,
    pub graphics: ash::vk::Queue,
    pub transfer: ash::vk::Queue
}

pub struct VulkanContext {
    pub entry: ash::Entry,
    pub instance: ash::Instance,
    pub debug_loader: debug_utils::Instance,
    pub debug_messenger: vk::DebugUtilsMessengerEXT,
    pub surface_loader: surface::Instance,
    pub surface: vk::SurfaceKHR,
    pub physical_device: ash::vk::PhysicalDevice,
    pub queues: DeviceQueues,
    pub device: ash::Device,
    pub command_pool: ash::vk::CommandPool,
}

pub unsafe extern "system" fn debug_callback(
    messege_severity: vk::DebugUtilsMessageSeverityFlagsEXT,
    message_type    : vk::DebugUtilsMessageTypeFlagsEXT,
    p_callback_data : *const vk::DebugUtilsMessengerCallbackDataEXT,
    p_user_data     : *mut std::ffi::c_void
) -> vk::Bool32 {
    let callback_message = unsafe { *p_callback_data };
    if !callback_message.p_message.is_null() {
        let message = unsafe { CStr::from_ptr(callback_message.p_message).to_string_lossy() };
        eprintln!("[VULKAN] {}", message);
    }
    vk::FALSE
}

impl VulkanContext {
    pub fn new(window: &Window) -> Result<Self, Box<dyn Error>> {
        let (entry, instance, debug_loader, debug_messenger) = Self::create_instance(window)?;
        let physical_device: ash::vk::PhysicalDevice = Self::pick_physical_device(&instance)?;
        let (queue_infos, device): (DeviceQueues, ash::Device) = Self::create_logical_device(&instance, &physical_device)?;

        let window_handle = window.window_handle()?.as_raw();
        let display_handle = window.display_handle()?.as_raw();

        let surface_loader = ash::khr::surface::Instance::new(&entry, &instance);
        let surface = unsafe {
            ash_window::create_surface(
                &entry,
                &instance,
                display_handle,
                window_handle,
                None
            )?
        };

        let command_pool = Self::create_command_pool(&device, &queue_infos)?;


        Ok( VulkanContext {
            entry,
            instance,
            debug_loader,
            debug_messenger,
            surface_loader,
            surface,
            physical_device,
            queues: queue_infos,
            device,
            command_pool
        })
    }

    fn create_debug_utils_messenger() -> ash::vk::DebugUtilsMessengerCreateInfoEXT<'static> {
        vk::DebugUtilsMessengerCreateInfoEXT::default()
            .message_severity(
                vk::DebugUtilsMessageSeverityFlagsEXT::INFO  |
                vk::DebugUtilsMessageSeverityFlagsEXT::ERROR |
                vk::DebugUtilsMessageSeverityFlagsEXT::WARNING
            )
            .message_type(
                vk::DebugUtilsMessageTypeFlagsEXT::VALIDATION |
                vk::DebugUtilsMessageTypeFlagsEXT::PERFORMANCE
            )
            .pfn_user_callback(Some(debug_callback))
    }

    fn create_instance(window: &winit::window::Window) -> Result<(ash::Entry, ash::Instance, debug_utils::Instance, vk::DebugUtilsMessengerEXT), Box<dyn Error>> {
        let entry = unsafe { Entry::load()? };
        let engine_name = CString::new("Calypso Engine").unwrap();

        let app_info = vk::ApplicationInfo::default()
            .api_version(vk::make_api_version(0, 1, 0, 0))
            .engine_name(&engine_name)
            .engine_version(vk::make_api_version(0, 1, 0, 0))
            .api_version(vk::API_VERSION_1_3);

        let validation_layer = CString::new("VK_LAYER_KHRONOS_validation").unwrap();
        let debug_uitls_extension = vk::EXT_DEBUG_UTILS_NAME;
        let swapchain_extension = ash::vk::KHR_SWAPCHAIN_NAME;

        let required_layers: Vec<&CStr> = vec![&validation_layer];
        let required_extensions: Vec<&CStr> = vec![debug_uitls_extension];

        let window_handle = window.window_handle()?;
        let display_handle = window.display_handle()?;
        let surface_extensions = ash_window::enumerate_required_extensions(display_handle.as_raw())?;

        let layer_properties = unsafe { entry.enumerate_instance_layer_properties()? };
        let extension_properties = unsafe { entry.enumerate_instance_extension_properties(None)? };

        let all_layers_available = required_layers.iter().all(|req| {
            layer_properties.iter().any(|prop| {
                let avl = unsafe { CStr::from_ptr(prop.layer_name.as_ptr()) };
                *req == avl
            })
        });

        let all_extensions_available = required_extensions.iter().all(|req| {
            extension_properties.iter().any(|prop| {
                let avl = unsafe { CStr::from_ptr(prop.extension_name.as_ptr()) };
                *req == avl
            })
        });

        if !all_layers_available || !all_extensions_available {
            return Err("[ERR] Could not find all the required Extensions or layers!".into());
        }

        let layer_ptrs: Vec<*const i8> = required_layers.iter().map(|layer| layer.as_ptr()).collect();
        let mut extension_ptrs: Vec<*const i8> = surface_extensions.to_vec();
        extension_ptrs.push(debug_uitls_extension.as_ptr());

        let mut debug_utils_info = Self::create_debug_utils_messenger();

        let mut create_info = vk::InstanceCreateInfo::default()
            .application_info(&app_info)
            .enabled_layer_names(&layer_ptrs)
            .enabled_extension_names(&extension_ptrs)
            .push_next(&mut debug_utils_info);

        let instance = unsafe { entry.create_instance(&create_info, None)? };

        let debug_loader = debug_utils::Instance::new(&entry, &instance);
        let debug_messenger = unsafe { debug_loader.create_debug_utils_messenger(&debug_utils_info, None)? };
        log!(INFO, "Instance and debug messenger created!");
        Ok((entry, instance, debug_loader, debug_messenger))
    }

    fn pick_physical_device(instance: &ash::Instance) -> Result<ash::vk::PhysicalDevice, Box<dyn Error>> {
        let physical_devices = unsafe { instance.enumerate_physical_devices()? };
        if physical_devices.is_empty() {
            return Err("[ERR] Could not find vulkan capable devices!".into());
        }

        for adapter in physical_devices {
            if Self::device_is_suitable(instance, &adapter) {
                log!(INFO, "Physical device found!");
                return Ok(adapter);
            }
        }
        Err("[ERR] Could not find a device with required properties!".into())
    }

    fn device_is_suitable(instance: &ash::Instance, adapter: &ash::vk::PhysicalDevice) -> bool {
        let adapter_properties = unsafe { instance.get_physical_device_properties(*adapter) };

        let supports_1_3 = adapter_properties.api_version >= vk::API_VERSION_1_3;
        let is_discrete = adapter_properties.device_type == vk::PhysicalDeviceType::DISCRETE_GPU;

        let adapter_queue_properties = unsafe { instance.get_physical_device_queue_family_properties(*adapter) };

        let mut has_compute = false;
        let mut has_graphics = false;

        for queue_prop in adapter_queue_properties {
            if has_compute && has_graphics { break; }
            if queue_prop.queue_flags.contains(vk::QueueFlags::COMPUTE)  { has_compute  = true; }
            if queue_prop.queue_flags.contains(vk::QueueFlags::GRAPHICS) { has_graphics = true; }
        }

        let mut adapter_vulkan11_features       = vk::PhysicalDeviceVulkan11Features::default();
        let mut adapter_vulkan13_features       = vk::PhysicalDeviceVulkan13Features::default();
        let mut extended_dynamic_state_features = vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT::default();

        let mut adapter_features2 = vk::PhysicalDeviceFeatures2::default()
            .push_next(&mut adapter_vulkan11_features)
            .push_next(&mut adapter_vulkan13_features)
            .push_next(&mut extended_dynamic_state_features);

        unsafe { instance.get_physical_device_features2(*adapter, &mut adapter_features2) };

        let supports_required_features = adapter_vulkan11_features.shader_draw_parameters       == vk::TRUE &&
                                         adapter_vulkan13_features.dynamic_rendering            == vk::TRUE &&
                                         extended_dynamic_state_features.extended_dynamic_state == vk::TRUE;

        has_compute && has_graphics && supports_1_3 && is_discrete && supports_required_features
    }

    fn choose_graphics_queue(qfp: &[QueueFamilyProperties]) -> Result<u32, Box<dyn Error>> {
        for (i, property) in qfp.iter().enumerate() {
            if property.queue_flags.contains(vk::QueueFlags::GRAPHICS) {
                return Ok(i as u32);
            }
        }
        Err("[ERR] Could not find a graphics queue!".into())
    }

    fn choose_transfer_queue(qfp: &[QueueFamilyProperties]) -> Result<u32, Box<dyn Error>> {
        let mut transfer_queue = !0u32;
        for (i, property) in qfp.iter().enumerate() {
            if property.queue_flags.contains(vk::QueueFlags::TRANSFER) &&
               !property.queue_flags.contains(vk::QueueFlags::GRAPHICS) &&
               !property.queue_flags.contains(vk::QueueFlags::COMPUTE)
            {
                return Ok(i as u32);
            }

            if property.queue_flags.contains(vk::QueueFlags::TRANSFER) {
                transfer_queue = i as u32;
            }
        }

        if transfer_queue != !0u32 {
            warn!(WARN, "Dedicated transfer queue not found! Falling back to general queue!");
            Ok(transfer_queue)
        } else {
            Err("[ERR] Could not find a transfer queue!".into())
        }
    }

    fn create_logical_device(instance: &ash::Instance, adapter: &ash::vk::PhysicalDevice) -> Result<(DeviceQueues, ash::Device), Box<dyn Error>> {
        let queue_family_properties = unsafe { instance.get_physical_device_queue_family_properties(*adapter) };
        let graphics_queue_index = Self::choose_graphics_queue(&queue_family_properties)?;
        let transfer_queue_index = Self::choose_transfer_queue(&queue_family_properties)?;

        let priorities = [1.0f32];
        let mut unique_queue_families: HashSet<u32> = HashSet::new();
        unique_queue_families.insert(graphics_queue_index);
        unique_queue_families.insert(transfer_queue_index);

        // deduplicate
        let queue_create_infos: Vec<vk::DeviceQueueCreateInfo> = unique_queue_families.iter()
            .map(|&queue_idx| {
                vk::DeviceQueueCreateInfo::default()
                    .queue_family_index(queue_idx)
                    .queue_priorities(&priorities)
            }).collect();

        let device_extensions: [*const i8; 1] = [vk::KHR_SWAPCHAIN_NAME.as_ptr()];

        let mut vulkan11_features = vk::PhysicalDeviceVulkan11Features::default()
            .shader_draw_parameters(true);
        let mut vulkan13_features = vk::PhysicalDeviceVulkan13Features::default()
            .dynamic_rendering(true)
            .synchronization2(true);
        let mut extended_dynamic_state = vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT::default()
            .extended_dynamic_state(true); //pnext chains

        let mut physical_device_features = vk::PhysicalDeviceFeatures2::default()
            .push_next(&mut vulkan11_features)
            .push_next(&mut vulkan13_features)
            .push_next(&mut extended_dynamic_state);

        let create_info = vk::DeviceCreateInfo::default()
            .queue_create_infos(&queue_create_infos)
            .enabled_extension_names(&device_extensions)
            .push_next(&mut physical_device_features); // pnext chain

        let device = unsafe { instance.create_device(*adapter, &create_info, None)? };

        let graphics_queue = unsafe { device.get_device_queue(graphics_queue_index, 0) };
        let transfer_queue = unsafe { device.get_device_queue(transfer_queue_index, 0) };

        let queue_infos: DeviceQueues = DeviceQueues {
            graphics_family: graphics_queue_index, transfer_family: transfer_queue_index,
            graphics: graphics_queue      , transfer: transfer_queue
        };
        log!(INFO, "Created queues and device!");
        Ok((queue_infos, device))
    }

    fn create_command_pool(device: &ash::Device, queues: &DeviceQueues) -> Result<ash::vk::CommandPool, Box<dyn Error>> {
        let create_info = ash::vk::CommandPoolCreateInfo::default()
            .queue_family_index(queues.graphics_family)
            .flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER);

        log!(INFO, "Created Command Pool!");
        Ok(unsafe { device.create_command_pool(&create_info, None)? })
    }



    pub unsafe fn find_memory_type(
        &self,
        type_bits: u32,
        properties:
        ash::vk::MemoryPropertyFlags
    ) -> Result<u32, Box<dyn Error>> {
        let memory_properties = unsafe { self.instance.get_physical_device_memory_properties(self.physical_device) };

        for i in 0..memory_properties.memory_type_count {
            if (type_bits & (1 << i)) != 0 && memory_properties.memory_types[i as usize].property_flags.contains(properties)  {
                return Ok(i);
            }
        }
        Err("[ERR] Could not find required memory properties".into())
    }
}

impl Drop for VulkanContext {
    fn drop(&mut self) {
        unsafe {
            self.device.device_wait_idle();

            self.device.destroy_command_pool(self.command_pool, None);
            self.device.destroy_device(None);
            self.surface_loader.destroy_surface(self.surface, None);
            self.debug_loader.destroy_debug_utils_messenger(self.debug_messenger, None);
            self.instance.destroy_instance(None);
        }
    }
}
