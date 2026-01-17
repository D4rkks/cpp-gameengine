#define STB_TRUETYPE_IMPLEMENTATION
#include "Font.h"
#include <GL/glew.h>
#include <fstream>
#include <iostream>
#include <vector>

namespace Engine {

Font::Font(const std::string &filepath, float fontSize)
    : m_FilePath(filepath), m_FontSize(fontSize) {


  std::ifstream file(filepath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cout << "[Font] Failed to open font file: " << filepath << std::endl;
    return;
  }
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<unsigned char> ttfBuffer(size);
  if (!file.read((char *)ttfBuffer.data(), size)) {
    std::cout << "[Font] Failed to read font file: " << filepath << std::endl;
    return;
  }

  unsigned char *tempBitmap =
      new unsigned char[1024 *
                        1024];

  stbtt_BakeFontBitmap(ttfBuffer.data(), 0, fontSize, tempBitmap, 1024, 1024,
                       32, 96, m_CharData);

  unsigned char *rgbaBitmap = new unsigned char[1024 * 1024 * 4];
  for (int i = 0; i < 1024 * 1024; i++) {
    unsigned char alpha = tempBitmap[i];
    rgbaBitmap[i * 4 + 0] = 255;
    rgbaBitmap[i * 4 + 1] = 255;
    rgbaBitmap[i * 4 + 2] = 255;
    rgbaBitmap[i * 4 + 3] = alpha;
  }

  m_Atlas = std::make_shared<Texture2D>(1024, 1024);
  m_Atlas->SetData(rgbaBitmap, 1024 * 1024 * 4);

  delete[] tempBitmap;
  delete[] rgbaBitmap;

  std::cout << "[Font] Loaded: " << filepath << std::endl;
}

Font::~Font() {}

const stbtt_bakedchar *Font::GetGlyph(char c) const {
  if (c < 32 || c > 126)
    return nullptr;
  return &m_CharData[c - 32];
}

}
