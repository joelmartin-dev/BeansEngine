#ifndef MODEL_HPP
#define MODEL_HPP
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Vertex.hpp"

// stores the unique data of each primitive in a gltf
struct Material {
  uint32_t id;
};

struct Mesh {
  glm::vec3 translation = glm::vec3(0.0f);
  glm::vec3 rotation = glm::vec3(0.0f);
  glm::vec3 scale = glm::vec3(1.0f);

  glm::mat4 getModelMatrix() const
  {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, translation);
    model = glm::rotate(model, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, scale);
    return model;
  }
};

struct RenderTarget {
  vk::raii::DescriptorPool descriptorPool = nullptr;
  vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
  std::vector<vk::raii::DescriptorSet> descriptorSets;

  std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> graphicsPipeline = std::pair(nullptr, nullptr);
};

struct Triangle {
  // These coordinates will always create a triangle that completely covers the screen.
  // Double the width and double the height of the viewport
  const std::vector<Vertex> vertices = {
    {{-1.0f, -1.0f, -0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{-1.0f, 3.0f, -0.0f}, {0.0f, 2.0f}, {1.0f, 1.0f, 1.0f}},
    {{3.0f, -1.0f, -0.0f}, {2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
  };

  const std::vector<uint32_t> indices = {0, 1, 2};
};

struct Quad {
  const std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f, -0.1f}, {0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {}, 1},
    {{-0.5f, 0.5f, -0.1f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {}, 1},
    {{0.5f, -0.5f, 0.1f}, {0.0, 1.0f}, {0.0f, 1.0f, 0.0f}, {}, 1},
    {{0.5f, 0.5f, 0.1f}, {1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {}, 1}
  };

  const std::vector<uint32_t> indices = {0, 1, 2, 2, 1, 3};
};

struct Cube {
  const std::vector<Vertex> vertices = {
    {{-1.0, -1.0, -1.0}, {0.0, 0.0}, {1.0f, 1.0f, 1.0f}, {}, 1},
    {{-1.0, -1.0, 1.0}, {0.0, 1.0}, {1.0f, 1.0f, 1.0f}, {}, 1},
    {{-1.0, 1.0, -1.0}, {1.0, 0.0}, {1.0f, 1.0f, 1.0f}, {}, 1},
    {{-1.0, 1.0, 1.0}, {1.0, 1.0}, {1.0f, 1.0f, 1.0f}, {}, 1},
    {{1.0, -1.0, -1.0}, {1.0, 0.0}, {1.0f, 1.0f, 1.0f}, {}, 1},
    {{1.0, -1.0, 1.0}, {1.0, 1.0}, {1.0f, 1.0f, 1.0f}, {}, 1},
    {{1.0, 1.0, -1.0}, {0.0, 0.0}, {1.0f, 1.0f, 1.0f}, {}, 1},
    {{1.0, 1.0, 1.0}, {0.0, 1.0}, {1.0f, 1.0f, 1.0f}, {}, 1},
  };
  const std::vector<uint32_t> indices = {
    0, 1, 2, 2, 1, 3,
    4, 5, 0, 0, 5, 1,
    6, 7, 4, 4, 7, 5,
    2, 3, 6, 6, 3, 7,
    1, 5, 3, 3, 5, 7,
    0, 2, 4, 4, 2, 6
  };
};
#endif