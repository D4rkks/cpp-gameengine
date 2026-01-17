#pragma once

#include "../Renderer/Texture.h"
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace Engine {

class ContentBrowserPanel {
public:
  ContentBrowserPanel();

  void OnImGuiRender();

private:
  std::filesystem::path m_CurrentDirectory;

  std::shared_ptr<Texture2D> m_DirectoryIcon;
  std::shared_ptr<Texture2D> m_FileIcon;
  std::shared_ptr<Texture2D> m_SceneIcon;
  std::shared_ptr<Texture2D> m_ModelIcon;

  std::map<std::filesystem::path, std::shared_ptr<Texture2D>> m_TextureCache;
};

}
