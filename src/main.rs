mod engine;
mod camera;
mod vertex;
mod model;
mod buffer_structs;
mod app_options;


use winit::{event_loop::{ControlFlow, EventLoop}, keyboard::ModifiersState};
use crate::{engine::App};

// Arch Linux: unset envvar WAYLAND_DISPLAY to force x11
fn main()
{
  let event_loop = EventLoop::new().unwrap();
  event_loop.set_control_flow(ControlFlow::Poll);

  let mut app = App::default();
  app.modifiers_state = ModifiersState::default();

  event_loop.run_app(&mut app).unwrap();

  return;
}
