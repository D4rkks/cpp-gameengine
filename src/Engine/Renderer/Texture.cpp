#include "stb_image.h"

#include "Texture.h"
#include <GL/glew.h>
#include <iostream>
#include <unordered_map>

namespace Engine {

namespace {
std::unordered_map<std::string, std::weak_ptr<Texture2D>> s_TextureCache;
}

std::shared_ptr<Texture2D> Texture2D::GetOrLoad(const std::string &path) {
  auto it = s_TextureCache.find(path);
  if (it != s_TextureCache.end()) {
    if (auto existing = it->second.lock()) return existing;
    s_TextureCache.erase(it);
  }
  auto t = std::make_shared<Texture2D>(path);
  s_TextureCache[path] = t;
  return t;
}

void Texture2D::ClearCache() { s_TextureCache.clear(); }

Texture2D::Texture2D(const std::string &path)
    : m_Path(path), m_Width(0), m_Height(0), m_Channels(0) {

  stbi_set_flip_vertically_on_load(1);
  unsigned char *data =
      stbi_load(path.c_str(), &m_Width, &m_Height, &m_Channels, 4);

  if (data) {
    m_Channels = 4;
    GLenum internalFormat = GL_RGBA8;
    GLenum dataFormat = GL_RGBA;

    glGenTextures(1, &m_RendererID);
    glBindTexture(GL_TEXTURE_2D, m_RendererID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height, 0,
                 dataFormat, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(data);
  } else {
    std::cout << "Failed to load texture: " << path << std::endl;
  }
}

Texture2D::Texture2D(uint32_t width, uint32_t height)
    : m_Width(width), m_Height(height) {
  m_Channels = 4;
  GLenum internalFormat = GL_RGBA8;
  GLenum dataFormat = GL_RGBA;

  glGenTextures(1, &m_RendererID);
  glBindTexture(GL_TEXTURE_2D, m_RendererID);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                  GL_REPEAT);

  glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height, 0,
               dataFormat, GL_UNSIGNED_BYTE, nullptr);
}

void Texture2D::SetData(void *data, uint32_t size) {
  uint32_t bpp = 4;
  if (size != m_Width * m_Height * bpp) {
    std::cout << "Texture::SetData: Size mismatch!" << std::endl;
    return;
  }

  glBindTexture(GL_TEXTURE_2D, m_RendererID);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, GL_RGBA,
                  GL_UNSIGNED_BYTE, data);
}

Texture2D::~Texture2D() { glDeleteTextures(1, &m_RendererID); }

void Texture2D::Bind(uint32_t slot) const {
  glActiveTexture(GL_TEXTURE0 + slot);
  glBindTexture(GL_TEXTURE_2D, m_RendererID);
}

void Texture2D::Unbind() const { glBindTexture(GL_TEXTURE_2D, 0); }

uint32_t Texture::LoadCubemap(const std::vector<std::string> &faces) {
  uint32_t textureID;
  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

  int width, height, nrChannels;
  for (unsigned int i = 0; i < faces.size(); i++) {
    unsigned char *data =
        stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
    if (data) {

      GLenum dataFormat = (nrChannels == 4) ? GL_RGBA : GL_RGB;
      GLenum internalFormat = (nrChannels == 4) ? GL_RGBA8 : GL_RGB8;
      glPixelStorei(GL_UNPACK_ALIGNMENT, (nrChannels == 4) ? 4 : 1);
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat,
                   width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
      stbi_image_free(data);
    } else {
      std::cout << "Cubemap texture failed to load at path: " << faces[i]
                << std::endl;
      stbi_image_free(data);
    }
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  return textureID;
}

}
