#include "Engine/Core/Application.h"
#include "Engine/Project/ProjectLoader.h"
#include "Engine/Project/ScriptBuilder.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

int main(int argc, char *argv[]) {
#ifndef GAME_MODE_RUNTIME
  // "Engine.exe --build-scripts <file.myproject>" compiles the project's
  // Game.dll without opening the editor (same build the editor runs).
  if (argc >= 3 && std::string(argv[1]) == "--build-scripts") {
    Engine::Project project;
    auto loaded = Engine::ProjectLoader::LoadFromFile(
        std::filesystem::absolute(argv[2]), project);
    if (!loaded.Ok) {
      std::cerr << "cannot load project: " << loaded.Error << std::endl;
      return 2;
    }
    auto result = Engine::ScriptBuilder::Build(project);
    std::cout << result.Log << std::endl;
    std::cout << (result.Ok ? "[build-scripts] OK" : "[build-scripts] FAILED")
              << std::endl;
    return result.Ok ? 0 : 1;
  }
#endif

  try {
    auto app = new Engine::Application(argc, argv);
    app->Run();
    delete app;
  } catch (const std::exception &e) {
    std::cerr << "FATAL ERROR: " << e.what() << std::endl;
#ifdef _WIN32
    MessageBoxA(nullptr, e.what(), "Fatal Error", MB_OK | MB_ICONERROR);
#endif
    return -1;
  }
  return 0;
}
