#include "Engine/Core/Application.h"
#include <iostream>

// Define entry point
namespace Engine {
extern void RegisterGameScripts();
}

int main(int argc, char *argv[]) {
  Engine::RegisterGameScripts();
  try {
    auto app = new Engine::Application();

    app->Run();

    delete app;
  } catch (const std::exception &e) {
    std::cerr << "FATAL ERROR: " << e.what() << std::endl;
    std::cout << "Press Enter to continue..." << std::endl;
    std::cin.get();
    return -1;
  }

  return 0;
}
