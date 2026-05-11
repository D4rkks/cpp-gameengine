#pragma once

#include "../Core/Input.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include <SDL_scancode.h>
#include <glm/gtc/quaternion.hpp>
#include <iostream>

namespace Engine {

class PlayerController : public ScriptableEntity {
public:
  void OnUpdate(float ts) override {
    glm::vec2 mouseDelta = Input::GetMouseDelta();
    float sensitivity = 0.002f;

    m_Entity->Transform.Rotation.y -= mouseDelta.x * sensitivity;
    m_Entity->Transform.Rotation.x -= mouseDelta.y * sensitivity;

    if (m_Entity->Transform.Rotation.x > glm::radians(89.0f))
      m_Entity->Transform.Rotation.x = glm::radians(89.0f);
    if (m_Entity->Transform.Rotation.x < glm::radians(-89.0f))
      m_Entity->Transform.Rotation.x = glm::radians(-89.0f);

    glm::vec3 direction(0.0f);
    if (Input::IsKeyPressed(SDL_SCANCODE_W))
      direction.z -= 1.0f;
    if (Input::IsKeyPressed(SDL_SCANCODE_S))
      direction.z += 1.0f;
    if (Input::IsKeyPressed(SDL_SCANCODE_A))
      direction.x -= 1.0f;
    if (Input::IsKeyPressed(SDL_SCANCODE_D))
      direction.x += 1.0f;

    bool isMoving = glm::length(direction) > 0.0f;
    bool isSprinting = Input::IsKeyPressed(SDL_SCANCODE_LSHIFT) && isMoving;

    float targetSpeed = isSprinting ? 18.0f : 10.0f;
    float speed = targetSpeed;

    if (m_Entity->HasCamera) {
      float targetFOV = isSprinting ? 80.0f : 70.0f;
      float fovDiff = targetFOV - m_Entity->Camera.PerspectiveFOV;
      m_Entity->Camera.PerspectiveFOV +=
          fovDiff * 10.0f * ts;
    }

    if (isMoving) {
      direction = glm::normalize(direction);
    }

    glm::quat q(glm::vec3(0.0f, m_Entity->Transform.Rotation.y, 0.0f));
    glm::vec3 rotatedDir = q * direction;

    bool hasRuntimeBody =
        m_Entity->HasRigidBody && m_Entity->RigidBody.RuntimeBody != nullptr;
    if (hasRuntimeBody) {
      glm::vec3 movement = rotatedDir * speed;
      glm::vec3 currentVel = GetLinearVelocity();
      SetLinearVelocity({movement.x, currentVel.y, movement.z});

      bool isSpaceDown = Input::IsKeyPressed(SDL_SCANCODE_SPACE);
      bool spaceJustPressed = isSpaceDown && !m_SpacePressedLastFrame;
      m_SpacePressedLastFrame = isSpaceDown;

      bool isGrounded = std::abs(currentVel.y) < 0.01f;
      if (isGrounded)
        m_JumpCount = 0;

      if (spaceJustPressed) {
        if (isGrounded) {
          m_JumpCount = 1;
          SetLinearVelocity({currentVel.x, 15.0f, currentVel.z});
        } else if (m_JumpCount < 2) {
          m_JumpCount++;
          SetLinearVelocity({currentVel.x, 15.0f, currentVel.z});
        }
      }

      glm::vec3 vel = GetLinearVelocity();
      if (!isGrounded || vel.y > 0.1f) {
        if (vel.y > 0.0f) {
          vel.y -= 15.0f * ts;
        } else {
          vel.y -= 45.0f * ts;
        }
        SetLinearVelocity(vel);
      }
    } else {
      m_Entity->Transform.Translation += rotatedDir * speed * ts;
      if (Input::IsKeyPressed(SDL_SCANCODE_SPACE))
        m_Entity->Transform.Translation.y += speed * ts;
      if (Input::IsKeyPressed(SDL_SCANCODE_LSHIFT))
        m_Entity->Transform.Translation.y -= speed * ts;
    }
  }

private:
  int m_JumpCount = 0;
  bool m_SpacePressedLastFrame = false;
};

}
