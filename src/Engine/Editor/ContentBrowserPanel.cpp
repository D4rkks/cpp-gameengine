#include "ContentBrowserPanel.h"
#include <imgui.h>
#include <iostream>

namespace Engine {

// Change this to your assets directory
extern const std::filesystem::path g_AssetPath = "assets";

ContentBrowserPanel::ContentBrowserPanel() : m_CurrentDirectory(g_AssetPath) {
  m_DirectoryIcon =
      std::make_shared<Texture2D>("resources/editor/textures/folder.png");
  m_FileIcon =
      std::make_shared<Texture2D>("resources/editor/textures/file.png");
  m_SceneIcon =
      std::make_shared<Texture2D>("resources/editor/textures/scene.png");
  m_ModelIcon =
      std::make_shared<Texture2D>("resources/editor/textures/model.png");
}

void ContentBrowserPanel::OnImGuiRender() {
  ImGui::Begin("Content Browser");

  if (!std::filesystem::exists(m_CurrentDirectory)) {
    m_CurrentDirectory = g_AssetPath;
  }

  if (m_CurrentDirectory != std::filesystem::path(g_AssetPath)) {
    if (ImGui::Button("<-")) {
      m_CurrentDirectory = m_CurrentDirectory.parent_path();
    }
  }

  static float padding = 16.0f;
  static float thumbnailSize = 128.0f;
  float cellSize = thumbnailSize + padding;

  float panelWidth = ImGui::GetContentRegionAvail().x;
  int columnCount = (int)(panelWidth / cellSize);
  if (columnCount < 1)
    columnCount = 1;

  ImGui::Columns(columnCount, 0, false);

  for (auto &directoryEntry :
       std::filesystem::directory_iterator(m_CurrentDirectory)) {
    const auto &path = directoryEntry.path();
    auto relativePath = std::filesystem::relative(path, g_AssetPath);
    std::string filenameString = relativePath.filename().string();

    ImGui::PushID(filenameString.c_str());

    // Determine Icon
    std::shared_ptr<Texture2D> icon = m_FileIcon;
    ImVec4 tintColor = {1, 1, 1, 1};

    if (directoryEntry.is_directory()) {
      icon = m_DirectoryIcon;
    } else {
      std::string ext = path.extension().string();
      if (ext == ".scene") {
        icon = m_SceneIcon;
      } else if (ext == ".obj") {
        icon = m_ModelIcon;
      } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                 ext == ".tga") {
        if (m_TextureCache.find(path) == m_TextureCache.end()) {
          m_TextureCache[path] = std::make_shared<Texture2D>(path.string());
        }
        icon = m_TextureCache[path];
      }
    }

    // Icon Button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    // ImGui::ImageButton(const char* str_id, ImTextureID user_texture_id, ...)
    ImGui::ImageButton("##button", (void *)(intptr_t)icon->GetID(),
                       {thumbnailSize, thumbnailSize}, {0, 1}, {1, 0},
                       {0, 0, 0, 0}, tintColor);
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      if (directoryEntry.is_directory())
        m_CurrentDirectory /= path.filename();
    }

    if (ImGui::BeginDragDropSource()) {
      std::string itemPathStr = path.string();
      ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPathStr.c_str(),
                                (itemPathStr.size() + 1) * sizeof(char));
      ImGui::EndDragDropSource();
    }

    ImGui::TextWrapped("%s", filenameString.c_str());

    ImGui::NextColumn();
    ImGui::PopID();
  }

  ImGui::Columns(1);
  ImGui::End();
}

}
