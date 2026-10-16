#pragma once
#include <glm/glm.hpp>

namespace Engine {
// Conservative homogeneous AABB test, including rotated/nonuniform transforms.
inline bool BoundsVisible(const glm::mat4& clipFromLocal, const glm::vec3& low, const glm::vec3& high) {
  const glm::vec3 center = (low + high) * 0.5f, extent = (high - low) * 0.5f;
  const glm::vec4 w(clipFromLocal[0][3], clipFromLocal[1][3], clipFromLocal[2][3], clipFromLocal[3][3]);
  for (int axis = 0; axis < 3; ++axis) {
    const glm::vec4 row(clipFromLocal[0][axis], clipFromLocal[1][axis], clipFromLocal[2][axis], clipFromLocal[3][axis]);
    for (int sign = -1; sign <= 1; sign += 2) {
      const glm::vec4 plane = w + row * (float)sign;
      if (glm::dot(glm::vec3(plane), center) + plane.w + glm::dot(glm::abs(glm::vec3(plane)), extent) < -0.0001f) return false;
    }
  }
  return true;
}
}
