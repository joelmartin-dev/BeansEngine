#ifndef VERTEX_HPP
#define VERTEX_HPP

#include <array>
#include <vulkan/vulkan.hpp>
#include <glm/gtx/hash.hpp>
#include <glm/gtx/type_aligned.hpp> 

struct Vertex {
  // Attributes
  glm::vec3 pos = {};
  glm::vec2 texCoord = {};
  glm::vec3 colour = {};
  glm::vec3 norm = {};
  uint32_t cubeID = 0;

  // How the struct is passed
  static vk::VertexInputBindingDescription getBindingDescription()
  {
    return { 0, sizeof(Vertex), vk::VertexInputRate::eVertex };
  }

  // How the struct's data is laid out
  static std::array<vk::VertexInputAttributeDescription, 5> getAttributeDescriptions()
  {
    return {
      // location, binding, format, offset
      // Binding is 0, as we decided in getBindingDescription
      // Formats are aliases for in-shader data types, e.g. R32Sfloat is float, R64Sfloat is double
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord)),
      vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, colour)),
      vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, norm)),
      vk::VertexInputAttributeDescription(4, 0, vk::Format::eR32Uint, offsetof(Vertex, cubeID))
    };
  }

  // equal_to function, needed for use of Vertex as Key in unordered containers e.g. unordered_map(Key, T, hash(Key), equal_to(Key))
  bool operator==(const Vertex& other) const
  {
    return pos == other.pos && colour == other.colour && texCoord == other.texCoord && norm == other.norm && cubeID == other.cubeID;
  }
};

// Hash function, needed for use of Vertex as Key in unordered containers e.g. unordered_map(Key, T, hash(Key), equal_to(Key))
template<> struct std::hash<Vertex> {
  size_t operator()(Vertex const& vertex) const noexcept
  {
    return (((hash<glm::vec3>()(vertex.pos) ^
      (hash<glm::vec3>()(vertex.colour) << 1)) >> 1) ^
      (hash<glm::vec2>()(vertex.texCoord) << 1) >> 1) ^
      (hash<glm::vec3>()(vertex.norm) << 1);
  }
};
#endif