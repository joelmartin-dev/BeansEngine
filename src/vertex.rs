use ash::vk as vk;
use nalgebra_glm as glm;
use memoffset::offset_of;

// Attributes
#[repr(C)]
#[derive(PartialEq, Clone, Copy)]
pub struct Vertex {
  pub pos: glm::Vec3,
  pub tex_coord: glm::Vec2,
  pub colour: glm::Vec3,
  pub norm: glm::Vec3,
}

impl Vertex {
  // How the struct is passed
  pub fn get_binding_description() -> vk::VertexInputBindingDescription
  {
    return vk::VertexInputBindingDescription { 
      binding: 0, 
      stride: size_of::<Vertex>() as u32, 
      input_rate: vk::VertexInputRate::VERTEX
    };
  }

  // How the struct's data is laid out
  pub fn get_attribute_descriptions() -> [vk::VertexInputAttributeDescription; 4]
  {
    return [
      // location, binding, format, offset
      // Binding is 0, as we decided in getBindingDescription
      // Formats are aliases for in-shader data types, e.g. R32Sfloat is float, R64Sfloat is double
      vk::VertexInputAttributeDescription {
        location: 0,
        binding: 0,
        format: vk::Format::R32G32B32_SFLOAT,
        offset: offset_of!(Self, pos) as u32
      },
      vk::VertexInputAttributeDescription {
        location: 1,
        binding: 0,
        format: vk::Format::R32G32_SFLOAT,
        offset: offset_of!(Self, tex_coord) as u32
      },
      vk::VertexInputAttributeDescription {
        location: 2,
        binding: 0,
        format: vk::Format::R32G32B32_SFLOAT,
        offset: offset_of!(Self, colour) as u32
      },
      vk::VertexInputAttributeDescription {
        location: 3,
        binding: 0,
        format: vk::Format::R32G32B32_SFLOAT,
        offset: offset_of!(Self, norm) as u32
      }
    ];
  }
}