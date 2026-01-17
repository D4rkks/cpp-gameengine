#pragma once

#include <glm/glm.hpp>

namespace Engine {

class Input {
public:
  static bool IsKeyPressed(int keycode);
  static bool IsMouseButtonPressed(int button);
  static glm::vec2 GetMousePosition();
  static glm::vec2 GetMouseDelta();
  static float GetMouseX();
  static float GetMouseY();

  static void Update();

private:
  static glm::vec2 s_MouseDelta;
};

}
