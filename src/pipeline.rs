use std::{error::Error, ffi::c_char, fs::File};

use crate::vulkan_context::VulkanContext;

struct Pipeline {
    handle: ash::vk::Pipeline,
}

impl Pipeline {
    fn load_shader(ctx: &VulkanContext, path: &std::path::Path) -> Result<ash::vk::ShaderModule, Box<dyn Error>> {
        let mut file = std::io::BufReader::new(
            std::fs::File::open(path)?
        );

        let code = ash::util::read_spv(&mut file)?;

        let create_info = ash::vk::ShaderModuleCreateInfo::default()
            .code(&code);

        let shader_module = unsafe { ctx.device.create_shader_module(&create_info, None)? };
        Ok(shader_module)
    }

    fn create_pipeline() -> Result<(), Box<dyn Error>> {
        Ok(())
    }
}
