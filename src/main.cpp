#include "Engine/Core/Application.h"
#include <iostream>
#include <fstream>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

int main(int argc, char *argv[]) {
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
