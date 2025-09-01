#ifndef MEASUREMENT_HPP
#define MEASUREMENT_HPP
#include <cstdint>

// stores measurements data
struct EngineStats {
  long long int frametime = 0L;
  uint32_t tris = 0U;
  uint32_t drawcalls = 0U;
  long long int sceneUpdateTime = 0L;
  long long int meshDrawTime = 0L;
};
#endif