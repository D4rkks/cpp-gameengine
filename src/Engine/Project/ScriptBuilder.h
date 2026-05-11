#pragma once

#include "Project.h"
#include "../Core/EngineAPI.h"
#include <filesystem>
#include <string>

namespace Engine {

struct BuildResult {
    bool Ok;
    std::string Log;
};

class ENGINE_API ScriptBuilder {
public:

    static void EnsureCMakeTree(const Project& project);

    static BuildResult Build(const Project& project);

    static bool IsStale(const Project& project);
};

}
