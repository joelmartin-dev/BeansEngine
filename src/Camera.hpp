#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

class Camera
{
  public:
  glm::dvec3 velocity = glm::vec3(0.0);
  glm::dvec3 position = glm::vec3(0.0, 0.3, 0.0);
  double pitch { 0.0 };
  double deltaPitch { 0.0 };
  double yaw { 0.0 };
  double deltaYaw { 0.0 };
  float moveSpeed { 40.0f };
  float pitchSpeed { 20.0f };
  float yawSpeed{ 20.0f };
  bool shiftMod { false };
  float shiftSpeed { 2.0f };

  float fov { 45.0f };
  double deltaFOV { 0.0 };
  float fovSpeed { 500.0f };

  double oldXpos { 0.0 };
  double oldYpos { 0.0 };

  glm::dvec3 forward;
  glm::dvec3 right;

  glm::mat4 getViewMatrix();
  glm::mat4 getRotationMatrix();

  void update(double delta);

  void cursor_pos_callback(double xpos, double ypos);
  void key_callback(GLFWwindow* pWindow, int key, int scancode, int action, int mods);
};

#endif
