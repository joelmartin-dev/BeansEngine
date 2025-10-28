#ifndef BUFFERSTRUCTS_HPP
#define BUFFERSTRUCTS_HPP

#include <glm/gtx/type_aligned.hpp> // Known alignment of types is crucial when passing between CPU and GPU

// For passing the Model View Projection matrix to the GPU for vertex transformations
struct MVP {
  glm::aligned_mat4 model = glm::aligned_mat4();
  glm::aligned_mat4 view = glm::aligned_mat4();
  glm::aligned_mat4 invView = glm::aligned_mat4();
  glm::aligned_mat4 proj = glm::aligned_mat4();
  glm::aligned_mat4 invProj = glm::aligned_mat4();
};

struct PathTracePushConstant {
  uint32_t frame;
  float time;
  float intensity;
  glm::aligned_vec3 lightDir;
};

struct RasterPushConstant {
  uint32_t materialIndex;
  uint32_t instanceID;
};

struct RadianceCascadesPushConstant {
  uint32_t level;
};

struct ReSTIRPushConstant {

};

struct SubMesh {
  uint32_t indexOffset;
  uint32_t indexCount;
  int materialID;
  uint32_t firstVertex;
  uint32_t maxVertex;
  bool alphaCut;
  bool reflective;

  bool operator==(const SubMesh& other) const
  {
    return indexOffset == other.indexOffset &&
           indexCount == other.indexCount && 
           materialID == other.materialID && 
           firstVertex == other.firstVertex && 
           maxVertex == other.maxVertex && 
           alphaCut == other.alphaCut && 
           reflective == other.reflective;
  }
};

struct InstanceLUT {
  uint32_t materialID;
  uint32_t indexBufferOffset;
};
#endif