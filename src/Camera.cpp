#include "Camera.hpp"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"

glm::mat4 Camera::GetViewMatrix() const
{
  return glm::lookAt(position, position + forward, glm::dvec3(0.0, 1.0, 0.0));
}
glm::mat4 Camera::GetProjMatrix() const
{
  auto proj = glm::perspective(
    glm::radians(fov),
    viewportWidth / viewportHeight,
    0.01f,
    100.0f
  );
  proj[1][1] *= -1;
  return proj;
}

glm::mat4 Camera::GetRotationMatrix() const
{
  return glm::identity<glm::mat4>();
}

glm::mat4 Camera::GetModelMatrix() const
{
  return glm::rotate(GetRotationMatrix(), 0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::mat4 Camera::GetMVPMatrix() const
{
  auto view = GetViewMatrix();
  auto proj = GetProjMatrix();
  auto model = GetModelMatrix();
  return proj * view * model;
}

glm::dvec3 Camera::GetPos() const
{
  return position;
}

void Camera::Update(double delta)
{
  forward.x = static_cast<float>(cos(yaw) * cos(pitch));
  forward.y = static_cast<float>(sin(pitch));
  forward.z = static_cast<float>(sin(yaw) * cos(pitch));
  forward = glm::normalize(forward);

  right = glm::normalize(glm::cross(forward, glm::dvec3(0.0, 1.0, 0.0)));

  pitch += deltaPitch * static_cast<double>(pitchSpeed) * delta;
  pitch = glm::clamp(pitch, -glm::pi<double>() / 2.0 + 0.01, glm::pi<double>() / 2.0 - 0.01);
  yaw += deltaYaw * static_cast<double>(yawSpeed) * delta;
  yaw = glm::mod(yaw + glm::pi<double>(), glm::pi<double>() * 2.0f) - glm::pi<double>();

  float mod = shiftMod ? shiftSpeed : 1.0f;

  fov += static_cast<float>(deltaFOV) * fovSpeed * static_cast<float>(delta);

  position += (forward * velocity.z + right * velocity.x + glm::dvec3(0.0, 1.0, 0.0) * velocity.y) * static_cast<double>(moveSpeed) * delta * static_cast<double>(mod);

  // CursorHandler needs manual invoking to stop continuous movement
  if (mouseMode) CursorHandler(oldXpos, oldYpos);
}

void Camera::CursorHandler(double xpos, double ypos)
{
  double deltaXpos = oldXpos - xpos;
  double deltaYpos = oldYpos - ypos;

  oldXpos = xpos;
  oldYpos = ypos;

  deltaPitch = deltaYpos;
  deltaYaw = -deltaXpos;
}

void Camera::KeyHandler(GLFWwindow* pWindow, int key, int scancode, int action, int mods)
{
  if (action == GLFW_REPEAT || action == GLFW_PRESS)
  {
    switch (key)
    {
      case GLFW_KEY_W:
        velocity.z = 1.0f;
        break;
      case GLFW_KEY_A:
        velocity.x = -1.0f;
        break;
      case GLFW_KEY_S:
        velocity.z = -1.0f;
        break;
      case GLFW_KEY_D:
        velocity.x = 1.0f;
        break;
      case GLFW_KEY_Q:
        velocity.y = -1.0f;
        break;
      case GLFW_KEY_E:
        velocity.y = 1.0f;
        break;
      case GLFW_KEY_UP:
        deltaPitch = 1.0;
        break;
      case GLFW_KEY_LEFT:
        deltaYaw = -1.0;
        break;
      case GLFW_KEY_DOWN:
        deltaPitch = -1.0;
        break;
      case GLFW_KEY_RIGHT:
        deltaYaw = 1.0;
        break;
      case GLFW_KEY_MINUS:
        deltaFOV = -1.0;
        break;
      case GLFW_KEY_EQUAL:
        deltaFOV = 1.0;
        break;
      case GLFW_KEY_LEFT_SHIFT:
        shiftMod = true;
        break;
      case GLFW_KEY_F:
        glfwSetInputMode(pWindow, GLFW_CURSOR, glfwGetInputMode(pWindow, GLFW_CURSOR) == GLFW_CURSOR_NORMAL ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        mouseMode = glfwGetInputMode(pWindow, GLFW_CURSOR) == GLFW_CURSOR_NORMAL;
        break;
      default:
        break;
    }
  }

  if (action == GLFW_RELEASE)
  {
    printf("Released %i\r", key);
    switch (key)
    {
      case GLFW_KEY_W:
      case GLFW_KEY_S:
        velocity.z = 0.0f;
        break;
      case GLFW_KEY_A:
      case GLFW_KEY_D:
        velocity.x = 0.0f;
        break;
      case GLFW_KEY_Q:
      case GLFW_KEY_E:
        velocity.y = 0.0f;
        break;
      case GLFW_KEY_UP:
      case GLFW_KEY_DOWN:
        deltaPitch = 0.0;
        break;
      case GLFW_KEY_LEFT:
      case GLFW_KEY_RIGHT:
        deltaYaw = 0.0;
        break;
      case GLFW_KEY_MINUS:
      case GLFW_KEY_EQUAL:
        deltaFOV = 0.0;
        break;
      case GLFW_KEY_LEFT_SHIFT:
        shiftMod = false;
        break;
      default:
        break;
    }
  }
}
