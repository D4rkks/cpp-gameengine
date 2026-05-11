#include "SceneManager.h"
#include "../Project/Project.h"

namespace Engine {

static Project*     s_Project = nullptr;
static std::string  s_PendingLoad;
static bool         s_HasPending = false;
static std::string  s_LastError;

void SceneManager::SetProject(Project* project) {
    s_Project = project;
    s_PendingLoad.clear();
    s_HasPending = false;
    s_LastError.clear();
}

Project* SceneManager::GetProject() { return s_Project; }

bool SceneManager::Load(const std::string& name) {
    if (!s_Project) {
        s_LastError = "no project loaded";
        return false;
    }
    if (s_Project->FindScene(name) == nullptr) {
        s_LastError = "scene '" + name + "' is not in the current project";
        return false;
    }
    s_PendingLoad = name;
    s_HasPending = true;
    s_LastError.clear();
    return true;
}

bool SceneManager::IsLoadPending() { return s_HasPending; }

std::optional<std::string> SceneManager::ConsumePendingLoad() {
    if (!s_HasPending) return std::nullopt;
    s_HasPending = false;
    return s_PendingLoad;
}

const std::string& SceneManager::GetLastError() { return s_LastError; }

}
