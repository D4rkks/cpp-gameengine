#pragma once

#include <glm/glm.hpp>

namespace Engine {

// The plane's xyz normal must be normalized; w is -dot(normal, point).
inline glm::mat4 PlanarReflectionMatrix(const glm::vec4 &plane) {
  glm::mat4 reflection(1.0f);
  for (int column = 0; column < 3; ++column)
    for (int row = 0; row < 3; ++row)
      reflection[column][row] -= 2.0f * plane[column] * plane[row];
  reflection[3] = glm::vec4(-2.0f * plane.w * glm::vec3(plane), 1.0f);
  return reflection;
}

} // namespace Engine
