#include "Renderer.h"
#include "Mesh.h"
#include "RenderState.h"
#include <GL/glew.h>

namespace Engine {

void Renderer::Init() {
  RenderState::SetBlend(true);
  RenderState::SetDefaultBlendFunc();
  RenderState::SetDepthTest(true);
}

void Renderer::Shutdown() {}

void Renderer::SetClearColor(const glm::vec4 &color) {
  glClearColor(color.r, color.g, color.b, color.a);
}

void Renderer::Clear() { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }

static unsigned int s_LineShader = 0;
static unsigned int s_LineVAO = 0, s_LineVBO = 0;

void Renderer::Submit(const std::shared_ptr<Shader> &shader,
                      const std::shared_ptr<VertexArray> &vertexArray,
                      const glm::mat4 &transform) {
  shader->UploadUniformMat4("u_Transform", transform);
  vertexArray->Bind();
  glDrawElements(GL_TRIANGLES, vertexArray->GetIndexBuffer()->GetCount(),
                 GL_UNSIGNED_INT, nullptr);
}

void Renderer::SubmitMesh(const std::shared_ptr<Mesh> &mesh,
                          const std::shared_ptr<Shader> &shader,
                          const glm::mat4 &transform) {
  if (mesh && mesh->IsLoaded()) {
    shader->Bind();
    shader->UploadUniformMat4("u_Transform", transform);
    mesh->Bind();
    glDrawElements(GL_TRIANGLES,
                   mesh->GetVertexArray()->GetIndexBuffer()->GetCount(),
                   GL_UNSIGNED_INT, nullptr);
    mesh->Unbind();
  }
}

void Renderer::DrawLine(const glm::vec3 &p0, const glm::vec3 &p1,
                        const glm::vec4 &color, const glm::mat4 &viewProj) {
  if (s_LineShader == 0) {
    const char *vertexShaderSource = R"(
            #version 330 core
            layout (location = 0) in vec3 aPos;
            uniform mat4 u_ViewProj;
            void main() {
                gl_Position = u_ViewProj * vec4(aPos, 1.0);
            }
        )";
    const char *fragmentShaderSource = R"(
            #version 330 core
            out vec4 FragColor;
            uniform vec4 u_Color;
            void main() {
                FragColor = u_Color;
            }
        )";

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    s_LineShader = glCreateProgram();
    glAttachShader(s_LineShader, vertexShader);
    glAttachShader(s_LineShader, fragmentShader);
    glLinkProgram(s_LineShader);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glGenVertexArrays(1, &s_LineVAO);
    glGenBuffers(1, &s_LineVBO);
    glBindVertexArray(s_LineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, s_LineVBO);
    glBufferData(GL_ARRAY_BUFFER, 2 * 3 * sizeof(float), nullptr,
                 GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);
  }

  glUseProgram(s_LineShader);
  glUniformMatrix4fv(glGetUniformLocation(s_LineShader, "u_ViewProj"), 1,
                     GL_FALSE, &viewProj[0][0]);
  glUniform4f(glGetUniformLocation(s_LineShader, "u_Color"), color.r, color.g,
              color.b, color.a);

  float vertices[] = {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z};
  glBindBuffer(GL_ARRAY_BUFFER, s_LineVBO);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

  glBindVertexArray(s_LineVAO);
  glDrawArrays(GL_LINES, 0, 2);
}

void Renderer::DrawWireBox(const glm::mat4 &transform, const glm::vec4 &color,
                           const glm::mat4 &viewProj) {
  glm::vec3 corners[8] = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
                          {0.5f, 0.5f, -0.5f},   {-0.5f, 0.5f, -0.5f},
                          {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},
                          {0.5f, 0.5f, 0.5f},    {-0.5f, 0.5f, 0.5f}};

  for (int i = 0; i < 8; i++) {
    corners[i] = glm::vec3(transform * glm::vec4(corners[i], 1.0f));
  }

  DrawLine(corners[0], corners[1], color, viewProj);
  DrawLine(corners[1], corners[2], color, viewProj);
  DrawLine(corners[2], corners[3], color, viewProj);
  DrawLine(corners[3], corners[0], color, viewProj);

  DrawLine(corners[4], corners[5], color, viewProj);
  DrawLine(corners[5], corners[6], color, viewProj);
  DrawLine(corners[6], corners[7], color, viewProj);
  DrawLine(corners[7], corners[4], color, viewProj);

  DrawLine(corners[0], corners[4], color, viewProj);
  DrawLine(corners[1], corners[5], color, viewProj);
  DrawLine(corners[2], corners[6], color, viewProj);
  DrawLine(corners[3], corners[7], color, viewProj);
}

}
