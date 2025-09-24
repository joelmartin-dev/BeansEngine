#ifndef LIGHT_HPP
#define LIGHT_HPP

#include <array>
#include <vulkan/vulkan.hpp>
#include <glm/gtx/hash.hpp>

struct Light {
  // Attributes
  glm::vec3 pos = {};
  glm::vec3 colour = {};
  
  // How the struct is passed
  static vk::VertexInputBindingDescription getBindingDescription()
  {
    return { 0, sizeof(Light), vk::VertexInputRate::eVertex };
  }

  // How the struct's data is laid out
  static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
  {
    return {
      // location, binding, format, offset
      // Binding is 0, as we decided in getBindingDescription
      // Formats are aliases for in-shader data types, e.g. R32Sfloat is float, R64Sfloat is double
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Light, pos)),
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Light, colour))
    };
  }

  // equal_to function, needed for use of Vertex as Key in unordered containers e.g. unordered_map(Key, T, hash(Key), equal_to(Key))
  bool operator==(const Light& other) const
  {
    return pos == other.pos && colour == other.colour;
  }
};

// Hash function, needed for use of Vertex as Key in unordered containers e.g. unordered_map(Key, T, hash(Key), equal_to(Key))
template<> struct std::hash<Light> {
  size_t operator()(Light const& light) const noexcept
  {
    return ((hash<glm::vec3>()(light.pos) ^
      (hash<glm::vec3>()(light.colour) << 1)) >> 1);
  }
};
#endif