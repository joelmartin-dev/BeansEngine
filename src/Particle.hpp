#ifndef PARTICLE_HPP
#define PARTICLE_HPP

#include <array>
#include <vulkan/vulkan.hpp>
#include <glm/gtx/hash.hpp>

struct Particle {
  // Attributes
  glm::vec2 pos = {};
  glm::vec2 velocity = {};
  glm::vec4 colour = {};

  // How the struct is passed
  static vk::VertexInputBindingDescription getBindingDescription()
  {
    return { 0, sizeof(Particle), vk::VertexInputRate::eVertex };
  }

  // How the struct's data is laid out
  static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
  {
    return {
      // location, binding, format, offset
      // Binding is 0, as we decided in getBindingDescription
      // Formats are aliases for in-shader data types, e.g. R32Sfloat is float, R64Sfloat is double
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(Particle, pos)),
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Particle, colour)),
    };
  }

  // equal_to function, needed for use of Vertex as Key in unordered containers e.g. unordered_map(Key, T, hash(Key), equal_to(Key))
  bool operator==(const Particle& other) const
  {
    return pos == other.pos && colour == other.colour && velocity == other.velocity;
  }
};

// Hash function, needed for use of Vertex as Key in unordered containers e.g. unordered_map(Key, T, hash(Key), equal_to(Key))
template<> struct std::hash<Particle> {
  size_t operator()(Particle const& particle) const noexcept
  {
    return ((hash<glm::vec2>()(particle.pos) ^
      (hash<glm::vec2>()(particle.velocity) << 1)) >> 1) ^
      (hash<glm::vec4>()(particle.colour) << 1);
  }
};
#endif