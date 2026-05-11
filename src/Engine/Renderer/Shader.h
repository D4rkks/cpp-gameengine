#pragma once

#include <glm/glm.hpp>
#include <string>

typedef unsigned int GLuint;

namespace Engine {

class Shader {
public:
  Shader(const std::string &vertexSrc, const std::string &fragmentSrc);
  ~Shader();

  void Bind() const;
  void Unbind() const;

  void UploadUniformMat4(const std::string &name, const glm::mat4 &matrix);
  void UploadUniformMat3(const std::string &name, const glm::mat3 &matrix);
  void UploadUniformFloat4(const std::string &name, const glm::vec4 &values);
  void UploadUniformFloat3(const std::string &name, const glm::vec3 &values);
  void UploadUniformFloat2(const std::string &name, const glm::vec2 &values);
  void UploadUniformFloat(const std::string &name, float value);
  void UploadUniformInt(const std::string &name, int value);

private:
  GLuint m_RendererID;
};

}
