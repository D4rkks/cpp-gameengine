#include "GameModule.h"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

namespace Engine {

namespace {
    template<typename Fn>
    Fn Resolve(void* handle, const char* name) {
    #ifdef _WIN32
        return reinterpret_cast<Fn>(GetProcAddress((HMODULE)handle, name));
    #else
        (void)handle; (void)name; return nullptr;
    #endif
    }
}

GameModule::~GameModule() { Unload(); }

GameModuleResult GameModule::Load(const std::filesystem::path& path) {
    if (m_Handle) Unload();

#ifdef _WIN32
    m_Handle = (void*)LoadLibraryW(path.wstring().c_str());
    if (!m_Handle) {
        return {false, "LoadLibraryW failed for " + path.string() +
                       " (GetLastError=" + std::to_string(GetLastError()) + ")"};
    }
#else
    (void)path;
    return {false, "GameModule: non-Windows DLL loading not implemented"};
#endif

    m_Count   = Resolve<FnCount>  (m_Handle, "Game_GetScriptCount");
    m_Name    = Resolve<FnName>   (m_Handle, "Game_GetScriptName");
    m_Create  = Resolve<FnCreate> (m_Handle, "Game_CreateScript");
    m_Destroy = Resolve<FnDestroy>(m_Handle, "Game_DestroyScript");

    if (!m_Count || !m_Name || !m_Create || !m_Destroy) {
        Unload();
        return {false, "DLL is missing one or more required Game_* exports"};
    }
    return {true, ""};
}

GameModuleResult GameModule::Reload(const std::filesystem::path& path) {
    Unload();
    return Load(path);
}

void GameModule::Unload() {
    if (!m_Handle) return;
#ifdef _WIN32
    FreeLibrary((HMODULE)m_Handle);
#endif
    m_Handle = nullptr;
    m_Count = nullptr; m_Name = nullptr; m_Create = nullptr; m_Destroy = nullptr;
}

std::vector<std::string> GameModule::GetScriptNames() const {
    std::vector<std::string> out;
    if (!m_Count || !m_Name) return out;
    int n = m_Count();
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const char* s = m_Name(i);
        if (s) out.emplace_back(s);
    }
    return out;
}

ScriptableEntity* GameModule::CreateScript(const std::string& name) {
    if (!m_Create) return nullptr;
    return m_Create(name.c_str());
}

void GameModule::DestroyScript(ScriptableEntity* ptr) {
    if (!m_Destroy || !ptr) return;
    m_Destroy(ptr);
}

}
