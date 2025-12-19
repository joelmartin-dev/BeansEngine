use nalgebra_glm as glm;
use crate::vertex::Vertex;
use lazy_static::lazy_static;

#[derive(Default, Clone)]
pub struct Mesh {
  pub vertices: Vec<Vertex>,
  pub indices: Vec<u32>
}

lazy_static! {
  pub static ref TRIANGLE: Mesh = Mesh {
    // These coordinates will always create a triangle that completely covers the screen.
    // Double the width and double the height of the viewport
    vertices: vec![
      Vertex {pos: glm::vec3(-1.0, -1.0, -0.0), tex_coord: glm::vec2(0.0, 0.0), 
        colour: glm::vec3(1.0, 0.0, 0.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(-1.0, 3.0, -0.0), tex_coord: glm::vec2(0.0, 2.0), 
        colour: glm::vec3(1.0, 1.0, 1.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(3.0, -1.0, -0.0), tex_coord: glm::vec2(2.0, 0.0), 
        colour: glm::vec3(0.0, 1.0, 0.0), norm: glm::vec3(0.0, 0.0, 0.0)}
    ],
    indices: vec![0, 1, 2]
  };

  pub static ref QUAD: Mesh = Mesh {
    vertices: vec![
      Vertex {pos: glm::vec3(-0.5, -0.5, 0.0), tex_coord: glm::vec2(0.0, 0.0), 
        colour: glm::vec3(0.0, 0.0, 0.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(-0.5, 0.5, 0.0), tex_coord: glm::vec2(1.0, 0.0), 
        colour: glm::vec3(1.0, 0.0, 0.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(0.5, -0.5, 0.0), tex_coord: glm::vec2(0.0, 1.0), 
        colour: glm::vec3(0.0, 1.0, 0.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(0.5, 0.5, 0.0), tex_coord: glm::vec2(1.0, 1.0), 
        colour: glm::vec3(1.0, 1.0, 0.0), norm: glm::vec3(0.0, 0.0, 0.0)}
    ],
    indices: vec![0, 1, 2, 2, 1, 3]
  };

  pub static ref CUBE: Mesh = Mesh {
    vertices: vec![
      Vertex {pos: glm::vec3(-1.0, -1.0, -1.0), tex_coord: glm::vec2(0.0, 0.0), 
        colour: glm::vec3(1.0, 1.0, 1.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(-1.0, -1.0, 1.0), tex_coord: glm::vec2(0.0, 1.0), 
        colour: glm::vec3(1.0, 1.0, 1.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(-1.0, 1.0, -1.0), tex_coord: glm::vec2(1.0, 0.0), 
        colour: glm::vec3(1.0, 1.0, 1.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(-1.0, 1.0, 1.0), tex_coord: glm::vec2(1.0, 1.0), 
        colour: glm::vec3(1.0, 1.0, 1.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(1.0, -1.0, -1.0), tex_coord: glm::vec2(1.0, 0.0), 
        colour: glm::vec3(1.0, 1.0, 1.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(1.0, -1.0, 1.0), tex_coord: glm::vec2(1.0, 1.0), 
        colour: glm::vec3(1.0, 1.0, 1.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(1.0, 1.0, -1.0), tex_coord: glm::vec2(0.0, 0.0), 
        colour: glm::vec3(1.0, 1.0, 1.0), norm: glm::vec3(0.0, 0.0, 0.0)},
      Vertex {pos: glm::vec3(1.0, 1.0, 1.0), tex_coord: glm::vec2(0.0, 1.0), 
        colour: glm::vec3(1.0, 1.0, 1.0), norm: glm::vec3(0.0, 0.0, 0.0)}
    ],
    indices: vec![
      0, 1, 2, 2, 1, 3,
      4, 5, 0, 0, 5, 1,
      6, 7, 4, 4, 7, 5,
      2, 3, 6, 6, 3, 7,
      1, 5, 3, 3, 5, 7,
      0, 2, 4, 4, 2, 6
    ]
  };
}