#pragma once

#include "../Core/EngineAPI.h"
#include <optional>
#include <string>

namespace Engine {

class Project;

class ENGINE_API SceneManager {
public:
    static void SetProject(Project* project);
    static Project* GetProject();

    static bool Load(const std::string& name);

    static bool IsLoadPending();
    static std::optional<std::string> ConsumePendingLoad();

    static const std::string& GetLastError();
};

}
