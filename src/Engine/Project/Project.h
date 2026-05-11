#pragma once

#include "../Core/EngineAPI.h"
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

struct SceneEntry {
    std::string Name;
    std::string RelativePath;
};

class ENGINE_API Project {
public:
    std::string Name;
    std::string StartupSceneName;
    std::vector<SceneEntry> Scenes;
    std::string GameModulePath;
    std::filesystem::path ProjectRoot;

    const SceneEntry* FindScene(const std::string& name) const;
};

}
