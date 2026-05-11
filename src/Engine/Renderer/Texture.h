#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

class Texture2D {
public:
  Texture2D(const std::string &path);
  Texture2D(uint32_t width, uint32_t height);
  ~Texture2D();

  static std::shared_ptr<Texture2D> GetOrLoad(const std::string &path);
  static void ClearCache();

  uint32_t GetID() const { return m_RendererID; }
  void SetData(void *data, uint32_t size);
  void Bind(uint32_t slot = 0) const;
  void Unbind() const;

  int GetWidth() const { return m_Width; }
  int GetHeight() const { return m_Height; }
  const std::string &GetPath() const { return m_Path; }

  bool operator==(const Texture2D &other) const {
    return m_RendererID == other.m_RendererID;
  }

private:
  std::string m_Path;
  uint32_t m_RendererID;
  int m_Width, m_Height, m_Channels;
};

class Texture {
public:
  static uint32_t LoadCubemap(const std::vector<std::string> &faces);
};

}
