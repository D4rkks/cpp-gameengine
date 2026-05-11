#pragma once

#include "Project.h"
#include "ProjectSerializer.h"
#include "../Core/EngineAPI.h"
#include <filesystem>

namespace Engine {

class ENGINE_API ProjectLoader {
public:

    static ProjectResult LoadFromDirectory(const std::filesystem::path& dir, Project& out);

    static ProjectResult LoadFromFile(const std::filesystem::path& file, Project& out);
};

}
