#include "Shader.h"

#include <iostream>
#include <vector>

#include <GL/glew.h>

namespace Engine {

static GLuint CompileShader(GLuint type, const std::string &source) {
  GLuint id = glCreateShader(type);
  const char *src = source.c_str();
  glShaderSource(id, 1, &src, nullptr);
  glCompileShader(id);

  int result;
  glGetShaderiv(id, GL_COMPILE_STATUS, &result);
  if (result == GL_FALSE) {
    int length;
    glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> message(length);
    glGetShaderInfoLog(id, length, &length, message.data());
    std::cerr << "Failed to compile "
              << (type == GL_VERTEX_SHADER ? "vertex" : "fragment")
              << " shader!" << std::endl;
    std::cerr << message.data() << std::endl;
    glDeleteShader(id);
    return 0;
  }

  return id;
}

Shader::Shader(const std::string &vertexSrc, const std::string &fragmentSrc) {
  GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSrc);
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc);

  m_RendererID = glCreateProgram();
  glAttachShader(m_RendererID, vs);
  glAttachShader(m_RendererID, fs);
  glLinkProgram(m_RendererID);
  glValidateProgram(m_RendererID);

  glDeleteShader(vs);
  glDeleteShader(fs);
}

Shader::~Shader() { glDeleteProgram(m_RendererID); }

void Shader::Bind() const { glUseProgram(m_RendererID); }

void Shader::Unbind() const { glUseProgram(0); }

void Shader::UploadUniformMat4(const std::string &name,
                               const glm::mat4 &matrix) {
  GLint location = glGetUniformLocation(m_RendererID, name.c_str());
  glUniformMatrix4fv(location, 1, GL_FALSE, &matrix[0][0]);
}

void Shader::UploadUniformMat3(const std::string &name,
                               const glm::mat3 &matrix) {
  GLint location = glGetUniformLocation(m_RendererID, name.c_str());
  glUniformMatrix3fv(location, 1, GL_FALSE, &matrix[0][0]);
}

void Shader::UploadUniformFloat4(const std::string &name,
                                 const glm::vec4 &values) {
  GLint location = glGetUniformLocation(m_RendererID, name.c_str());
  glUniform4f(location, values.x, values.y, values.z, values.w);
}

void Shader::UploadUniformFloat3(const std::string &name,
                                 const glm::vec3 &values) {
  GLint location = glGetUniformLocation(m_RendererID, name.c_str());
  glUniform3f(location, values.x, values.y, values.z);
}

void Shader::UploadUniformFloat2(const std::string &name,
                                 const glm::vec2 &values) {
  GLint location = glGetUniformLocation(m_RendererID, name.c_str());
  glUniform2f(location, values.x, values.y);
}

void Shader::UploadUniformFloat(const std::string &name, float value) {
  GLint location = glGetUniformLocation(m_RendererID, name.c_str());
  glUniform1f(location, value);
}

void Shader::UploadUniformInt(const std::string &name, int value) {
  GLint location = glGetUniformLocation(m_RendererID, name.c_str());
  glUniform1i(location, value);
}

}
