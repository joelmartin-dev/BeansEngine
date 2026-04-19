use std::time::{Instant};

use winit::{application::ApplicationHandler, dpi::PhysicalSize, event::{ElementState, WindowEvent, Event}, event_loop, keyboard::{KeyCode, PhysicalKey}, window::Window};

use crate::engine::{App, Engine, RES};
use std::path::Path;

impl ApplicationHandler for App {
  fn resumed(&mut self, event_loop: &event_loop::ActiveEventLoop) {
      let window = event_loop.create_window(
        Window::default_attributes()
        .with_title("Beans Engine")
        .with_inner_size(PhysicalSize::new(RES[0], RES[1]))
      ).unwrap();
      self.engine = Some(Engine::new(&window));
      self.window = Some(window);
  }

  fn window_event(
          &mut self,
          event_loop: &event_loop::ActiveEventLoop,
          window_id: winit::window::WindowId,
          event: WindowEvent,
      ) {
      let engine = self.engine.as_mut().unwrap();
      let context = engine.context.as_mut().unwrap();
      let window = self.window.as_ref().unwrap();


      let debug_ui_context = engine.debug_gui_context.as_mut().unwrap();
      let imgui = &mut debug_ui_context.imgui;
      let platform = &mut debug_ui_context.platform;

      // Get the WindowEvent as a generic Event
      let raw_event: Event<WindowEvent> = Event::WindowEvent { window_id, event: event.clone() };

      // Allow interactions with ImGui UI
      platform.handle_event(imgui.io_mut(), window, &raw_event);

      match event {
        WindowEvent::ModifiersChanged(modifiers) => {
          self.modifiers_state = modifiers.state();
        },
        WindowEvent::KeyboardInput { device_id: _ , event, is_synthetic: _ } => {
          engine.camera.key_handler(&event, self.modifiers_state);
          match &event.physical_key {
            PhysicalKey::Code(KeyCode::Escape) => {
              event_loop.exit();
            },
            PhysicalKey::Code(KeyCode::KeyC) => {
              if event.state == ElementState::Pressed && !event.repeat && self.modifiers_state.alt_key() {
                let debug_ui_context = engine.debug_gui_context.as_mut().unwrap();
                engine.spirv_path = debug_ui_context.spirv_path.clone();
                Engine::compile_shader(context, Path::new(&debug_ui_context.slang_path), Path::new(&engine.spirv_path));
                engine.reload_shaders();
              }
            },
            PhysicalKey::Code(KeyCode::KeyR) => {
              if event.state == ElementState::Pressed && !event.repeat && self.modifiers_state.alt_key() {
                engine.reload_shaders();
              }
            },
            PhysicalKey::Code(KeyCode::KeyA) => {
              if event.state == ElementState::Pressed && !event.repeat && self.modifiers_state.alt_key() {
              }
            }
            _ => ()
          }
        },
        WindowEvent::CloseRequested => {
          event_loop.exit();
        },
        WindowEvent::Resized(_new_size) => {
          engine.set_framebuffer_resized();
        },
        WindowEvent::RedrawRequested => {
          // println!("Redraw");
          let delta_exp = 19;
          let frame_start = Instant::now();
          engine.camera.update((engine.delta as f32) / ((2 << delta_exp) as f32));
          engine.draw_frame(window);
          let mut frame_end = Instant::now();
          while frame_end.duration_since(frame_start).as_micros() < 16667 { frame_end = Instant::now() }
  
          engine.delta = frame_end.duration_since(frame_start).as_micros();
          engine.runtime += engine.delta;

          let debug_ui_context = engine.debug_gui_context.as_mut().unwrap();
          debug_ui_context.delta = frame_end.duration_since(frame_start).as_micros();

          self.window.as_ref().unwrap().request_redraw();
        }
        _ => ()
      }
  }
  
  fn new_events(&mut self, event_loop: &event_loop::ActiveEventLoop, cause: winit::event::StartCause) {
        let _ = (event_loop, cause);
    }
  
  fn user_event(&mut self, event_loop: &event_loop::ActiveEventLoop, event: ()) {
        let _ = (event_loop, event);
    }
  
  fn device_event(
        &mut self,
        event_loop: &event_loop::ActiveEventLoop,
        device_id: winit::event::DeviceId,
        event: winit::event::DeviceEvent,
    ) {
        let _ = (event_loop, device_id, event);
    }
  
  fn about_to_wait(&mut self, event_loop: &event_loop::ActiveEventLoop) {
        let _ = event_loop;
    }
  
  fn suspended(&mut self, event_loop: &event_loop::ActiveEventLoop) {
        let _ = event_loop;
    }
  
  fn exiting(&mut self, event_loop: &event_loop::ActiveEventLoop) {
        let _ = event_loop;
    }
  
  fn memory_warning(&mut self, event_loop: &event_loop::ActiveEventLoop) {
        let _ = event_loop;
    }
}