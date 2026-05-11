

#define GAME_MODULE_BUILD 1
#include "ScriptMacros.h"
#include "GameModuleAPI.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Registry {
    std::vector<std::string>                                         names;
    std::unordered_map<std::string, Engine::Internal::ScriptFactory> factories;
};

Registry& R() { static Registry r; return r; }

}

namespace Engine::Internal {

void RegisterScriptFactory(const char* name, ScriptFactory factory) {
    auto& r = R();
    r.names.emplace_back(name);
    r.factories[name] = std::move(factory);
}

}

extern "C" {

int Game_GetScriptCount() {
    return static_cast<int>(R().names.size());
}

const char* Game_GetScriptName(int index) {
    auto& r = R();
    if (index < 0 || index >= (int)r.names.size()) return nullptr;
    return r.names[index].c_str();
}

Engine::ScriptableEntity* Game_CreateScript(const char* name) {
    auto& r = R();
    auto it = r.factories.find(name);
    if (it == r.factories.end()) return nullptr;
    return it->second();
}

void Game_DestroyScript(Engine::ScriptableEntity* ptr) {
    delete ptr;
}

}
