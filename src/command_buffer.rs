use std::error::Error;

pub struct CommandBuffers {
    pub buffers: Vec<ash::vk::CommandBuffer>,
}

impl CommandBuffers {
    pub fn new(
        device: &ash::Device,
        cmd_pool: &ash::vk::CommandPool,
        buf_count: u32,
    ) -> Result<Self, Box<dyn Error>> {
        let alloc_info = ash::vk::CommandBufferAllocateInfo::default()
            .command_pool(*cmd_pool)
            .level(ash::vk::CommandBufferLevel::PRIMARY)
            .command_buffer_count(buf_count);

        let command_buffers = unsafe { device.allocate_command_buffers(&alloc_info)? };
        Ok(CommandBuffers { buffers: command_buffers })
    }

    pub fn free(&mut self, device: &ash::Device, cmd_pool: &ash::vk::CommandPool) {
        unsafe { device.free_command_buffers(*cmd_pool, &self.buffers) };
    }

    pub fn start(&self, device: &ash::Device, buffer: &ash::vk::CommandBuffer) -> Result<(), Box<dyn Error>> {
        let begin_info = ash::vk::CommandBufferBeginInfo::default()
            .flags(ash::vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT);

        unsafe { device.begin_command_buffer(*buffer, &begin_info)? };
        Ok(())
    }

    pub fn end(&self, device: &ash::Device, command_buffer: &ash::vk::CommandBuffer) {
        unsafe { device.end_command_buffer(*command_buffer) };
    }

    pub fn submit(
        &self,
        device: &ash::Device,
        queue: &ash::vk::Queue,
        submit_info: ash::vk::SubmitInfo2,
        fence: ash::vk::Fence
    ) -> Result<(), Box<dyn Error>> {
        unsafe {
            device.queue_submit2(*queue, std::slice::from_ref(&submit_info), fence)?;
            device.queue_wait_idle(*queue);
        }
        Ok(())
    }
}
