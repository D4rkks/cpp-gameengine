#include "EditorGrid.h"
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

namespace Engine {

static const char *s_GridVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

uniform mat4 u_ViewProjection;
uniform vec3 u_CameraPos;

out vec3 v_NearPoint;
out vec3 v_FarPoint;
out mat4 v_ViewProjection;

vec3 UnprojectPoint(float x, float y, float z, mat4 viewProj) {
    mat4 viewProjInv = inverse(viewProj);
    vec4 unprojectedPoint =  viewProjInv * vec4(x, y, z, 1.0);
    return unprojectedPoint.xyz / unprojectedPoint.w;
}

void main() {
    v_ViewProjection = u_ViewProjection;
    vec3 p = aPos;

    // Grid spanning screen
    v_NearPoint = UnprojectPoint(p.x, p.y, 0.0, u_ViewProjection).xyz;
    v_FarPoint = UnprojectPoint(p.x, p.y, 1.0, u_ViewProjection).xyz;

    gl_Position = vec4(p, 1.0);
}
)";

static const char *s_GridFragmentShader = R"(
#version 330 core
out vec4 FragColor;

in vec3 v_NearPoint;
in vec3 v_FarPoint;
in mat4 v_ViewProjection;

uniform float u_Near;
uniform float u_Far;

vec4 grid(vec3 fragPos3D, float scale, bool drawAxis) {
    vec2 coord = fragPos3D.xz * scale;
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);
    float minimumz = min(derivative.y, 1.0);
    float minimumx = min(derivative.x, 1.0);

    vec4 color = vec4(1.0, 1.0, 1.0, 1.0 - min(line, 1.0));

    if(fragPos3D.x > -0.1 * minimumx && fragPos3D.x < 0.1 * minimumx)
        color.z = 1.0;

    if(fragPos3D.z > -0.1 * minimumz && fragPos3D.z < 0.1 * minimumz)
        color.x = 1.0;

    return color;
}

float computeDepth(vec3 pos) {
    vec4 clip_space_pos = v_ViewProjection * vec4(pos.xyz, 1.0);
    return 0.5 * (clip_space_pos.z / clip_space_pos.w) + 0.5;
}

void main() {
    float t = -v_NearPoint.y / (v_FarPoint.y - v_NearPoint.y);
    vec3 fragPos3D = v_NearPoint + t * (v_FarPoint - v_NearPoint);

    gl_FragDepth = computeDepth(fragPos3D);

    float linearDepth = computeDepth(fragPos3D) * 2.0 - 1.0;
    linearDepth = (2.0 * u_Near * u_Far) / (u_Far + u_Near - linearDepth * (u_Far - u_Near));

    float fading = max(0, (0.5 - linearDepth));

    // Simple Grid
    FragColor = (grid(fragPos3D, 1.0, true) + grid(fragPos3D, 0.1, true)) * float(t > 0);
    // FragColor.a *= fading; // Disable fading to ensure visibility
    FragColor.a *= 0.8; // Constant alpha for now

    if (t <= 0) discard;
}
)";

EditorGrid::EditorGrid() {

  unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &s_GridVertexShader, nullptr);
  glCompileShader(vertexShader);

  int success;
  char infoLog[512];
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
    std::cout << "ERROR::GRID::VERTEX::COMPILATION_FAILED\n"
              << infoLog << std::endl;
  }

  unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &s_GridFragmentShader, nullptr);
  glCompileShader(fragmentShader);

  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
    std::cout << "ERROR::GRID::FRAGMENT::COMPILATION_FAILED\n"
              << infoLog << std::endl;
  }

  m_ShaderID = glCreateProgram();
  glAttachShader(m_ShaderID, vertexShader);
  glAttachShader(m_ShaderID, fragmentShader);
  glLinkProgram(m_ShaderID);

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  float vertices[] = {
      1.0f,  1.0f,  0.0f, 1.0f, 1.0f,
      1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
      -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
      -1.0f, 1.0f,  0.0f, 0.0f, 1.0f,
  };
  unsigned int indices[] = {0, 1, 3, 1, 2, 3};

  glGenVertexArrays(1, &m_VAO);
  glGenBuffers(1, &m_VBO);
  unsigned int EBO;
  glGenBuffers(1, &EBO);

  glBindVertexArray(m_VAO);

  glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
}

void EditorGrid::Draw(const EditorCamera &camera) {
  glUseProgram(m_ShaderID);

  glm::mat4 viewProj =
      camera.GetProjection() *
      glm::mat4(glm::mat3(
          camera.GetViewMatrix()));
  viewProj = camera.GetProjection() * camera.GetViewMatrix();

  glUniformMatrix4fv(glGetUniformLocation(m_ShaderID, "u_ViewProjection"), 1,
                     GL_FALSE, glm::value_ptr(viewProj));
  glUniform1f(glGetUniformLocation(m_ShaderID, "u_Near"), 0.1f);
  glUniform1f(glGetUniformLocation(m_ShaderID, "u_Far"), 1000.0f);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_DEPTH_TEST);

  glBindVertexArray(m_VAO);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  glDisable(GL_BLEND);
}

}
