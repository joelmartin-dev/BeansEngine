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
#[derive(Clone, Copy)]
pub struct RasterPushConstant {
  pub material_index: u32,
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