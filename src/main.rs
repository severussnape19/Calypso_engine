#![allow(dead_code)]
#![allow(unused)]

use std::{ error::Error, ffi::{CStr, CString}, result, task::Context};

use ash::{ Entry, vk };
use raw_window_handle::HasWindowHandle;
use winit::{self, application::ApplicationHandler, event::WindowEvent, event_loop::{ControlFlow, EventLoop}, window::Window};

mod vulkan_context;
mod swapchain;
mod pipeline;
mod frame_renderer;
use vulkan_context::VulkanContext;

use crate::swapchain::Swapchain;
use crate::frame_renderer::FrameRenderer;

mod macros;

#[derive(Default)]
struct App {
    window: Option<Window>,
    vulkan: Option<VulkanContext>,
    swapchain: Option<swapchain::Swapchain>,
    pipeline: Option<pipeline::Pipeline>,
    frame_renderer: Option<frame_renderer::FrameRenderer>,
}

impl App {
    fn destroy_vulkan_resources(&mut self) {
        let swapchain = self.swapchain.take();
        let pipeline = self.pipeline.take();
        let frame_renderer = self.frame_renderer.take();

        if let Some(vulkan) = self.vulkan.as_ref() {
            unsafe {
                vulkan.device.device_wait_idle();

                if let Some(mut renderer_) = frame_renderer {
                    renderer_.destroy_resources(vulkan);
                }

                if let Some(mut pipeline_) = pipeline {
                    pipeline_.destroy_resources(vulkan);
                }

                if let Some(mut swapchain_) = swapchain {
                    swapchain_.destroy(&vulkan.device);
                }
            }
        }
        self.vulkan = None;
    }

    fn draw_frame(&mut self) -> Result<(), Box<dyn Error>> {
        let context = self.vulkan.as_ref().ok_or("Vulkan context not initialized!")?;
        let swapchain = self.swapchain.as_ref().ok_or("Swapchain not initialized!")?;
        let pipeline = self.pipeline.as_ref().ok_or("Pipeline not initalized!")?;
        let frame_renderer = self.frame_renderer.as_mut().ok_or("Frame renderer not initialized!")?;

        frame_renderer.draw_frame(context, swapchain, pipeline)?;
        Ok(())
    }
}

impl Drop for App {
    fn drop(&mut self) {
        self.destroy_vulkan_resources();
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &winit::event_loop::ActiveEventLoop) {

        if self.window.is_none() {
            let attrs = Window::default_attributes().with_title("calypso");
            let window = event_loop.create_window(attrs).unwrap();

            let vulkan = match VulkanContext::new(&window) {
                Ok(vulkan) => vulkan,
                Err(e) => {
                    error!(ERROR, "Failed to initialize vulkan: {e}");
                    event_loop.exit();
                    return;
                }
            };

            let swapchain = match Swapchain::new(&vulkan) {
                Ok(sc) => sc,
                Err(e) => {
                    error!(ERROR, "Failed to create swapchain!: {e}");
                    event_loop.exit();
                    return;
                }
            };

            let pipeline_ = match pipeline::Pipeline::new(&vulkan, &swapchain) {
                Ok(p) => p,
                Err(e) => {
                    error!(ERROR, "Failed to create pipeline!: {e}");
                    event_loop.exit();
                    return;
                }
            };

            let frame_renderer_ = match FrameRenderer::new(&vulkan, &swapchain) {
                Ok(fr) => fr,
                Err(e) => {
                    error!(ERROR, "Failed to create frame renderer: {e}");
                    event_loop.exit();
                    return;
                }
            };

            self.window = Some(window);
            self.vulkan = Some(vulkan);
            self.swapchain = Some(swapchain);
            self.pipeline = Some(pipeline_);
            self.frame_renderer = Some(frame_renderer_);
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
                if let Some(win) = self.window.as_ref() {
                    let size = win.inner_size();
                    if size.width > 0 && size.height > 0 {
                       if let Err(err) = self.draw_frame() {
                            // Check if error is due to swapchain invalidation
                            if let Some(&vk_err) = err.downcast_ref::<vk::Result>() {
                                if vk_err == vk::Result::ERROR_OUT_OF_DATE_KHR || vk_err == vk::Result::SUBOPTIMAL_KHR {
                                    todo!();
                                } else {
                                    eprintln!("[FATAL] Render error: {err}");
                                    event_loop.exit();
                                    return;
                                }
                            }
                        }
                    }
                }
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
