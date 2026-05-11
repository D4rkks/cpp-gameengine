#pragma once

#include "Shader.h"
#include "Texture.h"
#include "VertexArray.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>

namespace Engine {

struct Renderer2DStorage {
  std::shared_ptr<VertexArray> QuadVertexArray;
  std::shared_ptr<Shader> TextureShader;
  std::shared_ptr<Texture2D> WhiteTexture;
};

class Renderer2D {
public:
  static void Init();
  static void Shutdown();

  static void BeginScene(const glm::mat4 &projection);
  static void EndScene();

  static void DrawQuad(const glm::vec2 &position, const glm::vec2 &size,
                       const glm::vec4 &color);
  static void DrawQuad(const glm::vec2 &position, const glm::vec2 &size,
                       const std::shared_ptr<Texture2D> &texture,
                       const glm::vec4 &tint = glm::vec4(1.0f));
  static void DrawQuad(const glm::vec2 &position, const glm::vec2 &size,
                       const std::shared_ptr<Texture2D> &texture,
                       const glm::vec2 &minUV, const glm::vec2 &maxUV,
                       const glm::vec4 &tint = glm::vec4(1.0f));

  static void DrawString(const std::string &text,
                         const std::shared_ptr<class Font> &font,
                         const glm::vec3 &position, const glm::vec4 &color,
                         float fontSize = 32.0f);

private:
  static void Flush();
};

}
