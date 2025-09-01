#ifndef MODEL_HPP
#define MODEL_HPP
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include <glm/gtc/matrix_transform.hpp>

// stores the unique data of each primitive in a gltf
struct Primitive {
  std::vector<uint32_t> indices;

  std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> indexBuffer = std::pair(nullptr, nullptr);
  //vk::raii::DeviceMemory indexBufferMemory = nullptr;

  size_t imageViewIndex = 0;

  std::vector<vk::raii::DescriptorSet> descriptorSets;
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
#endif