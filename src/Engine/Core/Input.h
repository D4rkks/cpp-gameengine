#pragma once

#include "EngineAPI.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Engine {

class ENGINE_API Input {
public:
  static bool IsKeyPressed(int keycode);
  static bool IsMouseButtonPressed(int button);
  static glm::vec2 GetMousePosition();
  static glm::vec2 GetMouseDelta();
  static float GetMouseX();
  static float GetMouseY();

  static bool   IsGamepadConnected();
  static bool   IsGamepadButtonPressed(int sdlButton);
  static float  GetGamepadAxis(int sdlAxis);
  static glm::vec2 GetGamepadLeftStick();
  static glm::vec2 GetGamepadRightStick();

  static void  RegisterAction(const std::string &action,
                              const std::vector<int> &scancodes,
                              const std::vector<int> &gamepadButtons);
  static bool  IsActionActive(const std::string &action);
  static void  RegisterAxis(const std::string &axis,
                            int scancodePos, int scancodeNeg,
                            int gamepadAxis);
  static float GetAxisValue(const std::string &axis);

  static void Update();

private:
  static glm::vec2 s_MouseDelta;
};

}
