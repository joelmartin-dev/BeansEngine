#ifndef BUFFERSTRUCTS_HPP
#define BUFFERSTRUCTS_HPP

#include <glm/gtx/type_aligned.hpp> // Known alignment of types is crucial when passing between CPU and GPU

// For passing the Model View Projection matrix to the GPU for vertex transformations
struct MVP {
  glm::aligned_mat4 mvp = glm::aligned_mat4();
};

// For passing cpu-based timing information to the GPU
struct UniformBuffer {
  float deltaTime = 1.0f;
  float accumTime = 0.0f;
};

struct Point {
  glm::aligned_vec2 pos = glm::aligned_vec2();
  glm::aligned_vec4 colour = glm::aligned_vec4();
};
#endif