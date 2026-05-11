#include "Input.h"

#include <SDL.h>
#include <iostream>
#include <unordered_map>

namespace Engine {

namespace {
SDL_GameController *s_Pad = nullptr;

struct ActionBinding {
  std::vector<int> Scancodes;
  std::vector<int> GamepadButtons;
};
struct AxisBinding {
  int ScancodePos = -1;
  int ScancodeNeg = -1;
  int GamepadAxis = -1;
};
std::unordered_map<std::string, ActionBinding> s_Actions;
std::unordered_map<std::string, AxisBinding>   s_Axes;

void EnsurePadOpen() {
  if (s_Pad) return;
  if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
  }
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    if (SDL_IsGameController(i)) {
      s_Pad = SDL_GameControllerOpen(i);
      if (s_Pad) {
        std::cout << "[Input] Gamepad opened: "
                  << SDL_GameControllerName(s_Pad) << std::endl;
        return;
      }
    }
  }
}
}

bool Input::IsKeyPressed(int keycode) {
  const Uint8 *state = SDL_GetKeyboardState(nullptr);
  return state[keycode];
}

bool Input::IsMouseButtonPressed(int button) {
  int x, y;
  Uint32 state = SDL_GetMouseState(&x, &y);
  return (state & SDL_BUTTON(button));
}

glm::vec2 Input::s_MouseDelta = {0.0f, 0.0f};

void Input::Update() {
  int x, y;
  SDL_GetRelativeMouseState(&x, &y);
  s_MouseDelta = {(float)x, (float)y};
  EnsurePadOpen();

  if (s_Pad && !SDL_GameControllerGetAttached(s_Pad)) {
    SDL_GameControllerClose(s_Pad);
    s_Pad = nullptr;
  }
}

glm::vec2 Input::GetMousePosition() {
  int x, y;
  SDL_GetMouseState(&x, &y);
  return {(float)x, (float)y};
}

glm::vec2 Input::GetMouseDelta() { return s_MouseDelta; }
float     Input::GetMouseX()     { return GetMousePosition().x; }
float     Input::GetMouseY()     { return GetMousePosition().y; }

bool Input::IsGamepadConnected() { return s_Pad != nullptr; }

bool Input::IsGamepadButtonPressed(int sdlButton) {
  if (!s_Pad) return false;
  return SDL_GameControllerGetButton(s_Pad, (SDL_GameControllerButton)sdlButton) != 0;
}

float Input::GetGamepadAxis(int sdlAxis) {
  if (!s_Pad) return 0.0f;
  Sint16 v = SDL_GameControllerGetAxis(s_Pad, (SDL_GameControllerAxis)sdlAxis);
  float f = (float)v / 32767.0f;

  if (f >  0.0f && f <  0.15f) f = 0.0f;
  if (f <  0.0f && f > -0.15f) f = 0.0f;
  return f;
}

glm::vec2 Input::GetGamepadLeftStick() {
  return { GetGamepadAxis(SDL_CONTROLLER_AXIS_LEFTX),
           GetGamepadAxis(SDL_CONTROLLER_AXIS_LEFTY) };
}
glm::vec2 Input::GetGamepadRightStick() {
  return { GetGamepadAxis(SDL_CONTROLLER_AXIS_RIGHTX),
           GetGamepadAxis(SDL_CONTROLLER_AXIS_RIGHTY) };
}

void Input::RegisterAction(const std::string &action,
                           const std::vector<int> &scancodes,
                           const std::vector<int> &gamepadButtons) {
  s_Actions[action] = { scancodes, gamepadButtons };
}
bool Input::IsActionActive(const std::string &action) {
  auto it = s_Actions.find(action);
  if (it == s_Actions.end()) return false;
  for (int sc : it->second.Scancodes)      if (IsKeyPressed(sc))           return true;
  for (int b  : it->second.GamepadButtons) if (IsGamepadButtonPressed(b))  return true;
  return false;
}
void Input::RegisterAxis(const std::string &axis, int scancodePos, int scancodeNeg,
                         int gamepadAxis) {
  s_Axes[axis] = { scancodePos, scancodeNeg, gamepadAxis };
}
float Input::GetAxisValue(const std::string &axis) {
  auto it = s_Axes.find(axis);
  if (it == s_Axes.end()) return 0.0f;
  float v = 0.0f;
  if (it->second.ScancodePos >= 0 && IsKeyPressed(it->second.ScancodePos)) v += 1.0f;
  if (it->second.ScancodeNeg >= 0 && IsKeyPressed(it->second.ScancodeNeg)) v -= 1.0f;
  if (it->second.GamepadAxis >= 0) {
    float pad = GetGamepadAxis(it->second.GamepadAxis);
    if (std::abs(pad) > std::abs(v)) v = pad;
  }
  return glm::clamp(v, -1.0f, 1.0f);
}

}
