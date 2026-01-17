#pragma once

#include "../Editor/ContentBrowserPanel.h"
#include "../Editor/SceneHierarchyPanel.h"
#include "../Renderer/EditorCamera.h"
#include "../Renderer/EditorGrid.h"
#include "../Scene/Scene.h"
#include "../Utils/PlatformUtils.h"
#include "../Window/Window.h"
#include <deque>
#include <filesystem>
#include <glm/glm.hpp>

#include <map>

namespace Engine {

class Shader;
class VertexArray;
class EditorGrid;
class Window;

class Application {
public:
  Application();
  virtual ~Application();

  void Run();

  enum class AppState { ProjectHub, Editor, Runtime };

private:
  Window *m_Window;
  EditorCamera m_Camera;
  EditorGrid *m_Grid;
  std::shared_ptr<Scene> m_Scene;
  std::shared_ptr<VertexArray> m_VertexArray, m_SkyboxVAO, m_BackdropVAO;
  std::shared_ptr<Shader> m_Shader, m_SkyboxShader;
  SceneHierarchyPanel m_SceneHierarchyPanel;
  ContentBrowserPanel m_ContentBrowserPanel;
  float m_LastFrameTime = 0.0f;
  std::string m_CurrentScenePath;
  bool m_Running = true;
  bool m_MouseLocked = true;
  bool m_ShowSceneSettings = false;
#ifdef GAME_MODE_RUNTIME
  AppState m_AppState = AppState::Runtime;
#else
  AppState m_AppState = AppState::ProjectHub;
#endif
  bool m_LayoutInitialized = false;
  glm::vec2 m_ViewportOffset = {0.0f, 0.0f};
  std::vector<std::string> m_RecentScenes;

  void RenderProjectHub();
  void LoadRecentScenes();
  void SaveRecentScenes();
  void AddToRecentScenes(const std::string &path);
  void SaveHistoryState();
  void Undo();
  void Redo();

  std::deque<std::string> m_UndoStack;
  std::deque<std::string> m_RedoStack;
  const size_t m_MaxHistorySize = 50;

  bool m_MarqueeActive = false;
  glm::vec2 m_MarqueeStart = {0.0f, 0.0f};
  std::map<int, glm::mat4> m_InitialSelectionTransforms;
  bool m_ShowUIOverlay = true;

  std::string m_NextScenePath;
  static Application *s_Instance;

public:
  static Application &Get() { return *s_Instance; }
  Window &GetWindow() { return *m_Window; }

  std::shared_ptr<Scene> GetScene() { return m_Scene; }

  void SetMouseLocked(bool locked) { m_MouseLocked = locked; }
  bool IsMouseLocked() const { return m_MouseLocked; }

  void LoadScene(const std::string &path);
};

Application *CreateApplication();
}
