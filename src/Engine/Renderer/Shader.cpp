#include "Shader.h"

#include <iostream>
#include <stdexcept>
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
    std::vector<char> message(length > 0 ? length : 1, '\0');
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
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    throw std::runtime_error("Failed to compile OpenGL shader; see the shader log for details.");
  }

  m_RendererID = glCreateProgram();
  glAttachShader(m_RendererID, vs);
  glAttachShader(m_RendererID, fs);
  glLinkProgram(m_RendererID);

  // Link status is independent of texture bindings. Validation here is too
  // early: sampler uniforms still have their default value (texture unit 0).
  GLint linked = GL_FALSE;
  glGetProgramiv(m_RendererID, GL_LINK_STATUS, &linked);
  if (linked == GL_FALSE) {
    GLint length = 0;
    glGetProgramiv(m_RendererID, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> message(length > 0 ? length : 1, '\0');
    glGetProgramInfoLog(m_RendererID, (GLsizei)message.size(), nullptr, message.data());
    std::string error = "Failed to link OpenGL shader: " + std::string(message.data());
    std::cerr << error << std::endl;
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteProgram(m_RendererID);
    throw std::runtime_error(error);
  }

  glDeleteShader(vs);
  glDeleteShader(fs);
}

Shader::~Shader() { glDeleteProgram(m_RendererID); }

void Shader::Bind() const { glUseProgram(m_RendererID); }

void Shader::Unbind() const { glUseProgram(0); }

int Shader::UniformLocation(const std::string &name) {
  auto found = m_UniformLocations.find(name);
  if (found != m_UniformLocations.end()) return found->second;
  int location = glGetUniformLocation(m_RendererID, name.c_str());
  m_UniformLocations.emplace(name, location);
  return location;
}

void Shader::UploadUniformMat4(const std::string &name,
                               const glm::mat4 &matrix) {
  GLint location = UniformLocation(name);
  glUniformMatrix4fv(location, 1, GL_FALSE, &matrix[0][0]);
}

void Shader::UploadUniformMat3(const std::string &name,
                               const glm::mat3 &matrix) {
  GLint location = UniformLocation(name);
  glUniformMatrix3fv(location, 1, GL_FALSE, &matrix[0][0]);
}

void Shader::UploadUniformFloat4(const std::string &name,
                                 const glm::vec4 &values) {
  GLint location = UniformLocation(name);
  glUniform4f(location, values.x, values.y, values.z, values.w);
}

void Shader::UploadUniformFloat3(const std::string &name,
                                 const glm::vec3 &values) {
  GLint location = UniformLocation(name);
  glUniform3f(location, values.x, values.y, values.z);
}

void Shader::UploadUniformFloat2(const std::string &name,
                                 const glm::vec2 &values) {
  GLint location = UniformLocation(name);
  glUniform2f(location, values.x, values.y);
}

void Shader::UploadUniformFloat(const std::string &name, float value) {
  GLint location = UniformLocation(name);
  glUniform1f(location, value);
}

void Shader::UploadUniformInt(const std::string &name, int value) {
  GLint location = UniformLocation(name);
  glUniform1i(location, value);
}

}
