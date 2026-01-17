#pragma once

#include "Texture.h"
#include "stb_truetype.h"
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

struct Glyph {
  stbtt_bakedchar Data;
};

class Font {
public:
  Font(const std::string &filepath, float fontSize = 96.0f);
  ~Font();

  std::shared_ptr<Texture2D> GetAtlas() const { return m_Atlas; }
  const stbtt_bakedchar *GetGlyph(char c) const;

  float GetFontSize() const { return m_FontSize; }

private:
  std::string m_FilePath;
  float m_FontSize;
  std::shared_ptr<Texture2D> m_Atlas;
  stbtt_bakedchar m_CharData[96];
};

}
