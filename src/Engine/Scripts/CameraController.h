#pragma once

#include "../Core/Input.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include <SDL.h>

namespace Engine {

class CameraController : public ScriptableEntity {
public:
  void OnCreate() override {
    if (m_Entity)
      m_Entity->Transform.Translation = {0.0f, 0.0f, 0.0f};
  }

  void OnUpdate(float ts) override {
    if (!m_Entity)
      return;

    auto &transform = m_Entity->Transform;
    float speed = 5.0f * ts;

    if (Input::IsKeyPressed(SDL_SCANCODE_I))
      transform.Translation.y += speed;
    if (Input::IsKeyPressed(SDL_SCANCODE_K))
      transform.Translation.y -= speed;
    if (Input::IsKeyPressed(SDL_SCANCODE_J))
      transform.Translation.x -= speed;
    if (Input::IsKeyPressed(SDL_SCANCODE_L))
      transform.Translation.x += speed;
  }
};

}
