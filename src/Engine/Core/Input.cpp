#include "Input.h"

#include <SDL.h>

namespace Engine {

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
}

glm::vec2 Input::GetMousePosition() {
  int x, y;
  SDL_GetMouseState(&x, &y);
  return {(float)x, (float)y};
}

glm::vec2 Input::GetMouseDelta() { return s_MouseDelta; }

float Input::GetMouseX() { return GetMousePosition().x; }

float Input::GetMouseY() { return GetMousePosition().y; }

}
