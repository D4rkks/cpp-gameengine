#pragma once

#include "EngineAPI.h"
#include "../Editor/ContentBrowserPanel.h"
#include "../Editor/SceneHierarchyPanel.h"
#include "../Renderer/EditorCamera.h"
#include "../Renderer/EditorGrid.h"
#include "../Scene/Scene.h"
#include "../Project/Project.h"
#include "../Project/ProjectLoader.h"
#include "../Scripting/GameModule.h"
#include "../Utils/PlatformUtils.h"
#include "../Window/Window.h"
#include "../VR/VRSystem.h"
#include <deque>
#include <filesystem>
#include <glm/glm.hpp>

#include <map>
#include <unordered_map>

namespace Engine {

class Shader;
class VertexArray;
class EditorGrid;
class Window;

class ENGINE_API Application {
public:
  Application(int argc, char** argv);
  virtual ~Application();

  const Project* GetProject() const { return m_Project.Name.empty() ? nullptr : &m_Project; }
  Project& GetProjectMutable() { return m_Project; }

  bool OpenProject(const std::filesystem::path& file);
  bool NewProject(const std::filesystem::path& dir, const std::string& name);
  void CloseProject();

  const std::string& GetLastProjectError() const { return m_LastProjectError; }

  void ReloadScripts();

  void TogglePlayMode();
  bool IsPlaying() const { return m_AppState == AppState::Runtime; }

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
  bool m_OpenNewScenePopup = false;
  char m_NewSceneNameBuf[128] = "";
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
  void DuplicateSelectedEntities();

  std::deque<std::string> m_UndoStack;
  std::deque<std::string> m_RedoStack;
  const size_t m_MaxHistorySize = 50;

  bool m_MarqueeActive = false;
  glm::vec2 m_MarqueeStart = {0.0f, 0.0f};
  std::map<int, glm::mat4> m_InitialSelectionTransforms;
  bool m_ShowUIOverlay = true;

  std::string m_NextScenePath;
  std::string m_LastProjectError;
  GameModule m_GameModule;
  Project m_Project;
  static Application *s_Instance;

  std::filesystem::path m_ProjectFilePath;
  std::string           m_PrePlaySceneSnapshot;
  glm::mat4             m_PrevViewProjection{1.0f};
  bool   m_AutosaveEnabled  = false;
  float  m_AutosaveInterval = 60.0f;
  float  m_AutosaveTimer    = 0.0f;

  void PersistProject();
  void AddOrRegisterScene(const std::filesystem::path& scenePath);

  std::string BuildStandaloneGame(const std::filesystem::path& outDir);

  void  SaveGameSet(const std::string &key, const std::string &value);
  std::string SaveGameGet(const std::string &key, const std::string &fallback = "") const;
  void  SaveGameWrite(const std::filesystem::path &file);
  void  SaveGameRead(const std::filesystem::path &file);

  std::string SaveCurrentSceneSnapshot();
  bool        RestoreSceneSnapshot(const std::string &yaml);

  bool        SavePrefab(int entityID, const std::filesystem::path &outFile);

  int         InstantiatePrefab(const std::filesystem::path &file);

  std::unordered_map<std::string, std::string> m_SaveGameKV;

  bool m_ShowBuildDialog = false;
  char m_BuildOutputBuf[512] = "";
  std::string m_BuildResultMsg;
  bool m_BuildResultOk = false;

  VRSystem m_VRSystem;

public:
  static Application &Get() { return *s_Instance; }
  Window &GetWindow() { return *m_Window; }
  GameModule& GetGameModule() { return m_GameModule; }
  VRSystem& GetVRSystem() { return m_VRSystem; }

  std::shared_ptr<Scene> GetScene() { return m_Scene; }

  void SetMouseLocked(bool locked);
  bool IsMouseLocked() const { return m_MouseLocked; }

  void LoadScene(const std::string &path);
};

Application *CreateApplication();
}
