#![allow(dead_code)]
#![allow(unused)]

use std::{ error::Error, ffi::{CStr, CString}, result};

use ash::{ Entry, vk };
use raw_window_handle::HasWindowHandle;
use winit::{self, application::ApplicationHandler, event::WindowEvent, event_loop::{ControlFlow, EventLoop}, window::Window};

mod vulkan_context;
mod swapchain;
use vulkan_context::VulkanContext;
mod macros;

#[derive(Default)]
struct App {
    window: Option<Window>,
    vulkan: Option<VulkanContext>
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &winit::event_loop::ActiveEventLoop) {

        if self.window.is_none() {
            let attrs = Window::default_attributes().with_title("calypso");
            let window = event_loop.create_window(attrs).unwrap();

            match VulkanContext::new(&window) {
                Ok(vulkan) => {
                    self.vulkan = Some(vulkan);
                }
                Err(e) => {
                    eprintln!("[ERR] Failed to initialize vulkan: {e}");
                    event_loop.exit();
                    return;
                }
            }

            self.window = Some(window);
        }
    }

    fn window_event(
        &mut self,
        event_loop: &winit::event_loop::ActiveEventLoop,
        window_id: winit::window::WindowId,
        event: winit::event::WindowEvent,
    ) {
        match event {
            WindowEvent::CloseRequested => {
                eprintln!("The close button was pressed! Stopping!");
                event_loop.exit();
            },
            WindowEvent::RedrawRequested => {
                self.window.as_ref().unwrap().request_redraw();
            },
            _ => {}
        }
    }
}

fn main() -> Result<(), Box<dyn Error>>{
    let event_loop = EventLoop::new().unwrap();
    event_loop.set_control_flow(ControlFlow::Poll);

    let mut app = App::default();
    event_loop.run_app(&mut app).unwrap();

    Ok(())
}
