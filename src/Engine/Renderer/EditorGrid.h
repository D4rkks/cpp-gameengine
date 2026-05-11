#pragma once

#include "EditorCamera.h"
#include <memory>

namespace Engine {

class EditorGrid {
public:
  EditorGrid();
  ~EditorGrid() = default;

  void Draw(const EditorCamera &camera);

private:
  unsigned int m_VAO, m_VBO;
  unsigned int m_ShaderID;
};

}
