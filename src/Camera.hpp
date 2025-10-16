#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <glm/glm.hpp> // common maths types

// Using for key aliases e.g. GLFW_KEY_ESCAPE, and for window input changes
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

struct Camera
{
  glm::dvec3 velocity = glm::vec3(0.0);
  glm::dvec3 position = glm::vec3(0.0, 0.3, 0.0);
  double pitch { 0.0 };
  double deltaPitch { 0.0 };
  double yaw { 0.0 };
  double deltaYaw { 0.0 };
  float moveSpeed { 4.0f };
  float pitchSpeed { 2.0f };
  float yawSpeed{ 2.0f };
  bool shiftMod { false };
  float shiftSpeed { 2.0f };

  float fov { 45.0f };
  double deltaFOV { 0.0 };
  float fovSpeed { 50.0f };

  double oldXpos { 0.0 };
  double oldYpos { 0.0 };
  bool mouseMode = false;

  float viewportWidth { 0.0f };
  float viewportHeight { 0.0f };

  glm::dvec3 forward;
  glm::dvec3 right;

  glm::mat4 GetViewMatrix() const;
  glm::mat4 GetProjMatrix() const;
  glm::mat4 GetRotationMatrix() const;
  glm::mat4 GetModelMatrix() const;
  glm::mat4 GetMVPMatrix() const;

  glm::dvec3 GetPos() const;

  void Update(double delta);

  void CursorHandler(double xpos, double ypos);
  void KeyHandler(GLFWwindow* pWindow, int key, int scancode, int action, int mods);
};

#endif
