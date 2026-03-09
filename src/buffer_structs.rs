use ash::vk::Bool32;
use nalgebra_glm as glm;

// For passing the Model View Projection matrix to the GPU for vertex transformations
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct MVP {
  pub model: glm::Mat4,
  pub view: glm::Mat4,
  pub inv_view: glm::Mat4,
  pub proj: glm::Mat4,
  pub inv_proj: glm::Mat4
}

// #[repr(C)]
// #[derive(Clone, Copy)]
// pub struct CubeTransforms {
//   pub model: glm::Mat4,
//   pub view: glm::Mat4,
//   pub inv_view: glm::Mat4,
//   pub proj: glm::Mat4,
//   pub inv_proj: glm::Mat4
// }

#[repr(C)]
#[derive(Default, Clone, Copy)]
#[cfg(feature = "reference")]
pub struct PathTracePushConstant {
  pub light_dir: glm::Vec3,
  pub intensity: f32,
  pub time: f32,
  pub frame: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
#[cfg(not(any(feature = "reference", feature = "restir", feature = "radiance_cascades")))]
pub struct RasterPushConstant {
  pub material_index: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
#[cfg(feature = "radiance_cascades")]
pub struct RadianceCascadesPushConstant {
  pub level: u32,
  pub max_level: u32,
  pub base_ray_count: u32,
  pub interval: f32,
  pub intensity: f32,
  pub light_dir: glm::Vec3
}

#[repr(C)]
#[derive(Clone, Copy)]
#[cfg(feature = "restir")]
pub struct ReSTIRPushConstant {

}

#[repr(C)]
#[derive(Clone, Copy, PartialEq)]
pub struct SubMesh {
  pub index_offset: u32,
  pub index_count: u32,
  pub material_id: u32,
  pub first_vertex: u32,
  pub max_vertex: u32,
  pub alpha_cut: Bool32,
}

#[repr(C)]
#[derive(Clone, Copy)]
#[cfg(any(feature = "reference", feature = "restir", feature = "radiance_cascades"))]
pub struct InstanceLUT {
  pub material_id: u32,
  pub index_buffer_offset: u32
}