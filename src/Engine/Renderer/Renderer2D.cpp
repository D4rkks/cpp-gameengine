#include "Renderer2D.h"
#include "Font.h"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {

static Renderer2DStorage *s_Data = nullptr;

void Renderer2D::Init() {
  s_Data = new Renderer2DStorage();

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  float vertices[] = {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.5f, -0.5f,
                      0.0f,  1.0f,  0.0f, 0.5f, 0.5f, 0.0f, 1.0f,
                      1.0f,  -0.5f, 0.5f, 0.0f, 0.0f, 1.0f};

  uint32_t indices[] = {0, 1, 2, 2, 3, 0};

  s_Data->QuadVertexArray = std::make_shared<VertexArray>();
  auto vbo = std::make_shared<VertexBuffer>(vertices, sizeof(vertices));
  vbo->SetLayout({{GL_FLOAT, 3, "a_Position"}, {GL_FLOAT, 2, "a_TexCoord"}});
  s_Data->QuadVertexArray->AddVertexBuffer(vbo);
  s_Data->QuadVertexArray->SetIndexBuffer(
      std::make_shared<IndexBuffer>(indices, 6));

  uint32_t whiteTextureData = 0xffffffff;
  s_Data->WhiteTexture = std::make_shared<Texture2D>(1, 1);
  s_Data->WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

  std::string vertexSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec2 a_TexCoord;
            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;
            uniform vec2 u_UVOffset;
            uniform vec2 u_UVScale;
            out vec2 v_TexCoord;
            void main() {
                v_TexCoord = a_TexCoord * u_UVScale + u_UVOffset;
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
            }
        )";

  std::string fragmentSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 color;
            in vec2 v_TexCoord;
            uniform vec4 u_Color;
            uniform sampler2D u_Texture;
            void main() {
                color = texture(u_Texture, v_TexCoord) * u_Color;
            }
        )";

  s_Data->TextureShader = std::make_shared<Shader>(vertexSrc, fragmentSrc);
  s_Data->TextureShader->Bind();
  s_Data->TextureShader->UploadUniformInt("u_Texture", 0);
}

void Renderer2D::Shutdown() { delete s_Data; }

void Renderer2D::BeginScene(const glm::mat4 &projection) {
  s_Data->TextureShader->Bind();
  s_Data->TextureShader->UploadUniformMat4("u_ViewProjection", projection);
}

void Renderer2D::EndScene() {}

void Renderer2D::DrawQuad(const glm::vec2 &position, const glm::vec2 &size,
                          const glm::vec4 &color) {
  DrawQuad(position, size, s_Data->WhiteTexture, color);
}

void Renderer2D::DrawQuad(const glm::vec2 &position, const glm::vec2 &size,
                          const std::shared_ptr<Texture2D> &texture,
                          const glm::vec4 &tint) {
  DrawQuad(position, size, texture, {0.0f, 0.0f}, {1.0f, 1.0f}, tint);
}

void Renderer2D::DrawQuad(const glm::vec2 &position, const glm::vec2 &size,
                          const std::shared_ptr<Texture2D> &texture,
                          const glm::vec2 &minUV, const glm::vec2 &maxUV,
                          const glm::vec4 &tint) {
  s_Data->TextureShader->Bind();
  s_Data->TextureShader->UploadUniformFloat4("u_Color", tint);
  s_Data->TextureShader->UploadUniformFloat2("u_UVOffset", minUV);
  s_Data->TextureShader->UploadUniformFloat2("u_UVScale", maxUV - minUV);
  texture->Bind(0);

  glm::mat4 transform =
      glm::translate(glm::mat4(1.0f), {position.x, position.y, 0.0f}) *
      glm::scale(glm::mat4(1.0f), {size.x, size.y, 1.0f});
  s_Data->TextureShader->UploadUniformMat4("u_Transform", transform);

  s_Data->QuadVertexArray->Bind();
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
}

void Renderer2D::DrawString(const std::string &text,
                            const std::shared_ptr<Font> &font,
                            const glm::vec3 &position, const glm::vec4 &color,
                            float fontSize) {
  if (!font)
    return;

  float scale = 1.0f;
  if (fontSize > 0.0f)
    scale = fontSize / font->GetFontSize();

  float x = position.x;
  float y = position.y;

  const stbtt_bakedchar *firstGlyph = font->GetGlyph(' ');
  if (!firstGlyph)
    return;

  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    const stbtt_bakedchar *glyph = font->GetGlyph(c);

    if (!glyph)
      continue;

    float ipw = 1.0f / 1024.0f;
    float iph = 1.0f / 1024.0f;

    float b_w = (glyph->x1 - glyph->x0) * scale;
    float b_h = (glyph->y1 - glyph->y0) * scale;
    float b_xoff = glyph->xoff * scale;
    float b_yoff = glyph->yoff * scale;
    float b_xadvance = glyph->xadvance * scale;

    float round_x = floor(x + b_xoff + 0.5f);

    float finalY = y + b_yoff + b_h * 0.5f;
    float drawX = round_x + b_w * 0.5f;

    float q_s0 = glyph->x0 * ipw;
    float q_t0 = glyph->y0 * iph;
    float q_s1 = glyph->x1 * ipw;
    float q_t1 = glyph->y1 * iph;

    DrawQuad({drawX, finalY}, {b_w, b_h}, font->GetAtlas(), {q_s0, q_t0},
             {q_s1, q_t1}, color);

    x += b_xadvance;
  }
}

}
