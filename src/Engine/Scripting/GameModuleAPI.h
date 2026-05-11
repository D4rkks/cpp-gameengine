#pragma once

namespace Engine { class ScriptableEntity; }

#if defined(_WIN32)
  #if defined(GAME_MODULE_BUILD)
    #define GAME_MODULE_API __declspec(dllexport)
  #else
    #define GAME_MODULE_API __declspec(dllimport)
  #endif
#else
  #define GAME_MODULE_API
#endif

extern "C" {

GAME_MODULE_API int Game_GetScriptCount();

GAME_MODULE_API const char* Game_GetScriptName(int index);

GAME_MODULE_API Engine::ScriptableEntity* Game_CreateScript(const char* name);

GAME_MODULE_API void Game_DestroyScript(Engine::ScriptableEntity* ptr);

}
