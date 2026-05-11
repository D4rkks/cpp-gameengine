#pragma once

#include "Project.h"
#include "../Core/EngineAPI.h"
#include <filesystem>
#include <string>

namespace Engine {

struct ProjectResult {
    bool Ok;
    std::string Error;
};

class ENGINE_API ProjectSerializer {
public:
    static ProjectResult Serialize(const Project& project, const std::filesystem::path& path);
    static ProjectResult Deserialize(const std::filesystem::path& path, Project& out);
};

}
