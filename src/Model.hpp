#ifndef MODEL_HPP
#define MODEL_HPP
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Vertex.hpp"

// stores the unique data of each primitive in a gltf
struct Material {
  std::vector<uint32_t> indices;

  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> indexBuffer = std::pair(nullptr, nullptr);

  bool doubleSided = false;

  std::vector<vk::raii::DescriptorSet> descriptorSets;
};

struct Primitive {

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

struct Quad {
  const std::vector<Vertex> vertices = {
    {{-1.0f, -1.0f, -0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{-1.0f, 1.0f, -0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
    {{1.0f, 1.0f, -0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{1.0f, -1.0f, -0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
  };
  const std::vector<uint16_t> indices = {
    0, 1, 2, 2, 3, 0
  };

  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> indexBuffer = std::pair(nullptr, nullptr);

  vk::raii::DescriptorPool descriptorPool = nullptr;
  vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
  std::vector<vk::raii::DescriptorSet> descriptorSets;

  std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> graphicsPipeline = std::pair(nullptr, nullptr);
};

struct Tri {
  const std::vector<Vertex> vertices = {
    {{-1.0f, -1.0f, -0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{-1.0f, 3.0f, -0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 2.0f}},
    {{3.0f, -1.0f, -0.0f}, {0.0f, 1.0f, 0.0f}, {2.0f, 0.0f}},
  };

  vk::raii::DescriptorPool descriptorPool = nullptr;
  vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
  std::vector<vk::raii::DescriptorSet> descriptorSets;

  std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> graphicsPipeline = std::pair(nullptr, nullptr);
};
#endif