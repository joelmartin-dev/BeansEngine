#ifndef RADIANCESTRUCTS_HPP
#define RADIANCESTRUCTS_HPP

#include <array>
#include <vulkan/vulkan.hpp>
#include <glm/gtx/type_aligned.hpp>
#include <glm/gtx/hash.hpp>

struct Vertex2D {
  // Attributes
  glm::aligned_vec2 pos = {};
  glm::aligned_vec3 colour = {};
  
  // How the struct is passed
  static vk::VertexInputBindingDescription getBindingDescription()
  {
    return { 0, sizeof(Vertex2D), vk::VertexInputRate::eVertex };
  }

  // How the struct's data is laid out
  static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
  {
    return {
      // location, binding, format, offset
      // Binding is 0, as we decided in getBindingDescription
      // Formats are aliases for in-shader data types, e.g. R32Sfloat is float, R64Sfloat is double
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex2D, pos)),
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex2D, colour))
    };
  }

  // equal_to function, needed for use of Vertex as Key in unordered containers e.g. unordered_map(Key, T, hash(Key), equal_to(Key))
  bool operator==(const Vertex2D& other) const
  {
    return pos == other.pos && colour == other.colour;
  }
};

// Hash function, needed for use of Vertex as Key in unordered containers e.g. unordered_map(Key, T, hash(Key), equal_to(Key))
template<> struct std::hash<Vertex2D> {
  size_t operator()(Vertex2D const& vertex) const noexcept
  {
    return ((hash<glm::aligned_vec2>()(vertex.pos) ^
      (hash<glm::aligned_vec3>()(vertex.colour) << 1)) >> 1);
  }
};

struct Scene {
  const std::vector<Vertex2D> vertices = {
    {{-0.25f, -0.5f}, {1.0f, 1.0f, 1.0f}},  // Segment 1
    {{0.25f, -0.5f}, {1.0f, 1.0f, 1.0f}},
    {{-1.0f, 0.f}, {0.0f, 0.0f, 0.0f}},     // Segment 2
    {{0.0f, 0.f}, {0.0f, 0.0f, 0.0f}}
  };
};
#endif