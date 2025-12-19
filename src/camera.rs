use nalgebra_glm as glm;
use winit::{event::{ElementState, KeyEvent}, keyboard::{Key, ModifiersState, NamedKey}};

#[derive(Default)]
pub struct Camera
{
  velocity: glm::Vec3,
  pub position: glm::Vec3,
  pub pitch: f32,
  delta_pitch: f32,
  pub yaw: f32,
  delta_yaw: f32,
  pub move_speed: f32,
  pub pitch_speed: f32,
  pub yaw_speed: f32,
  shift_mod: bool,
  pub shift_speed: f32,

  pub fov: f32,
  delta_fov: f32,
  pub fov_speed: f32,

  pub viewport_width: f32,
  pub viewport_height: f32,

  forward: glm::Vec3,
  right: glm::Vec3,
}

impl Camera
{
  pub fn new(width: u32, height: u32, position: glm::Vec3, pitch: f32, yaw: f32) -> Self
  {
    Self {
        velocity: glm::vec3(0.0, 0.0, 0.0),
        position: position,
        pitch: pitch,
        delta_pitch: 0.0,
        yaw: yaw,
        delta_yaw: 0.0,
        move_speed: 4.0,
        pitch_speed: 1.0,
        yaw_speed: 1.0,
        shift_mod: false,
        shift_speed: 2.0,
        fov: 45.0,
        delta_fov: 0.0,
        fov_speed: 50.0,
        viewport_width: width as f32,
        viewport_height: height as f32,
        forward: glm::vec3(0.0, 0.0, 1.0),
        right: glm::vec3(1.0, 0.0, 0.0),
    }
  }

  pub fn get_view_matrix(&self) -> glm::Mat4
  {
    let position: glm::Vec3 = self.position;
    let forward: glm::Vec3 = self.forward;
    let up: glm::Vec3 = glm::vec3(0.0, 1.0, 0.0);
    return glm::look_at_lh(&position, &(position + forward), &up);
  }

  pub fn get_proj_matrix(&self) -> glm::Mat4
  {
    let mut proj: glm::Mat4 = glm::perspective(
      self.viewport_width / self.viewport_height, self.fov.to_radians(), 0.01, 100.0);
    proj.m22 *= -1.0;
    return proj;
  }

  pub fn get_rotation_matrix(&self) -> glm::Mat4
  {
    return glm::Mat4::identity();
  }

  pub fn get_model_matrix(&self) -> glm::Mat4
  {
    let rot: glm::Mat4 = self.get_rotation_matrix();
    return glm::rotate_z(&rot, 0.0);
  }

  // pub fn get_mvp_matrix(&self) -> glm::Mat4
  // {
  //   let view:   glm::Mat4 = self.get_view_matrix();
  //   let proj:   glm::Mat4 = self.get_proj_matrix();
  //   let model:  glm::Mat4 = self.get_model_matrix();
  //   return proj * view * model;
  // }

  pub fn update(&mut self, delta: f32)
  {
    let forward = glm::normalize(
      &glm::vec3(self.yaw.cos() * self.pitch.cos(), self.pitch.sin(), self.yaw.sin() * self.pitch.cos()));
    
    let right = glm::normalize(
      &glm::cross(&self.forward, &glm::vec3(0.0, 1.0, 0.0)));

    let pitch = glm::clamp_scalar(self.pitch + self.delta_pitch * self.pitch_speed * delta, 
      -glm::half_pi::<f32>() + 0.01, glm::half_pi::<f32>() - 0.01);

    let yaw = glm::modf(
      self.yaw + self.delta_yaw * self.yaw_speed * delta + glm::pi::<f32>(), glm::two_pi::<f32>()) - glm::pi::<f32>();

    let modifier: f32 = if self.shift_mod {self.shift_speed} else { 1.0 };

    let fov = self.fov + self.delta_fov * self.fov_speed * delta;

    let position = self.position + 
      (self.forward * self.velocity.z + self.right * self.velocity.x + glm::vec3(0.0, 1.0, 0.0) * self.velocity.y) * 
      self.move_speed * delta * modifier;

    self.forward = forward;
    self.right = right;
    self.pitch = pitch;
    self.yaw = yaw;
    self.fov = fov;
    self.position = position;
  }

  pub fn key_handler(&mut self, event: &KeyEvent, modifiers: ModifiersState)
  {
    // if any modifiers are pressed (except shift), return
    if !modifiers.is_empty() && !modifiers.shift_key() { return; }
    match event.state {
      ElementState::Pressed => {
        // println!("Pressed: {:?}", key.as_ref());
        match event.logical_key.as_ref()
        {
          Key::Character("a") | Key::Character("A") => self.velocity.x =  1.0,
          Key::Character("d") | Key::Character("D") => self.velocity.x = -1.0,
          Key::Character("q") | Key::Character("Q") => self.velocity.y = -1.0,
          Key::Character("e") | Key::Character("E") => self.velocity.y =  1.0,
          Key::Character("s") | Key::Character("S") => self.velocity.z =  1.0,
          Key::Character("w") | Key::Character("W") => self.velocity.z = -1.0,
          Key::Named(NamedKey::ArrowDown)          => self.delta_pitch =  1.0,
          Key::Named(NamedKey::ArrowUp)            => self.delta_pitch = -1.0,
          Key::Named(NamedKey::ArrowLeft)           => self.delta_yaw  = -1.0,
          Key::Named(NamedKey::ArrowRight)          => self.delta_yaw  =  1.0,
          Key::Character("-") | Key::Character("_") => self.delta_fov  = -1.0,
          Key::Character("=") | Key::Character("+") => self.delta_fov  =  1.0,
          Key::Named(NamedKey::Shift)               => self.shift_mod  = true,
          _ => ()
        }
      },
      ElementState::Released => {
        match event.logical_key.as_ref() {
        Key::Character("w") | Key::Character("W") | Key::Character("s") | Key::Character("S") => self.velocity.z =   0.0,
        Key::Character("a") | Key::Character("A") | Key::Character("d") | Key::Character("D") => self.velocity.x =   0.0,
        Key::Character("q") | Key::Character("Q") | Key::Character("e") | Key::Character("E") => self.velocity.y =   0.0,
        Key::Named(NamedKey::ArrowUp)   | Key::Named(NamedKey::ArrowDown)                    => self.delta_pitch =   0.0,
        Key::Named(NamedKey::ArrowLeft) | Key::Named(NamedKey::ArrowRight)                    => self.delta_yaw  =   0.0,
        Key::Character("-") | Key::Character("_") | Key::Character("=") | Key::Character("+") => self.delta_fov  =   0.0,
        Key::Named(NamedKey::Shift)                                                           => self.shift_mod  = false,
          _ => ()
        }
      }
    }
  }
}