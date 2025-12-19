mod engine;
mod camera;
mod vertex;
mod model;
mod buffer_structs;

use winit::{event_loop::{ControlFlow, EventLoop}, keyboard::ModifiersState};
use crate::engine::{App};

// Arch Linux: unset envvar WAYLAND_DISPLAY to force x11
#[tokio::main]
async fn main()
{
  let event_loop = EventLoop::new().unwrap();
  event_loop.set_control_flow(ControlFlow::Poll);

  let mut app = App::default();
  app.modifiers_state = ModifiersState::default();

  // app.measurement_file_name = 
  //   (std::filesystem::path("Measuring") / std::filesystem::path(argv[0]).stem()).string().append(".csv");

  event_loop.run_app(&mut app).unwrap();

  return;
}
