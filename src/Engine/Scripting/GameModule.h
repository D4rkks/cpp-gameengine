#pragma once

#include "../Core/EngineAPI.h"
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

class ScriptableEntity;

struct GameModuleResult {
    bool Ok;
    std::string Error;
};

class ENGINE_API GameModule {
public:
    GameModule() = default;
    ~GameModule();

    GameModule(const GameModule&) = delete;
    GameModule& operator=(const GameModule&) = delete;

    GameModuleResult Load(const std::filesystem::path& path);
    GameModuleResult Reload(const std::filesystem::path& path);
    void Unload();

    bool IsLoaded() const { return m_Handle != nullptr; }

    std::vector<std::string> GetScriptNames() const;
    ScriptableEntity* CreateScript(const std::string& name);
    void DestroyScript(ScriptableEntity* ptr);

private:
    void* m_Handle = nullptr;

    using FnCount   = int (*)();
    using FnName    = const char* (*)(int);
    using FnCreate  = ScriptableEntity* (*)(const char*);
    using FnDestroy = void (*)(ScriptableEntity*);

    FnCount   m_Count   = nullptr;
    FnName    m_Name    = nullptr;
    FnCreate  m_Create  = nullptr;
    FnDestroy m_Destroy = nullptr;
};

}
