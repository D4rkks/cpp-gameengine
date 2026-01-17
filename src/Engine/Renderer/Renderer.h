#pragma once

#include "Shader.h"
#include "VertexArray.h"
#include <memory>

namespace Engine {

class Renderer {
public:
  static void Init();
  static void Shutdown();

  static void SetClearColor(const glm::vec4 &color);
  static void Clear();

  static void Submit(const std::shared_ptr<Shader> &shader,
                     const std::shared_ptr<VertexArray> &vertexArray,
                     const glm::mat4 &transform = glm::mat4(1.0f));

  static void SubmitMesh(const std::shared_ptr<class Mesh> &mesh,
                         const std::shared_ptr<Shader> &shader,
                         const glm::mat4 &transform = glm::mat4(1.0f));

  static void DrawLine(const glm::vec3 &p0, const glm::vec3 &p1,
                       const glm::vec4 &color, const glm::mat4 &viewProj);

  static void DrawWireBox(const glm::mat4 &transform, const glm::vec4 &color,
                          const glm::mat4 &viewProj);
};

}
