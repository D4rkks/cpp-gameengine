#pragma once

#include "../Core/Application.h"
#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"
#include <glm/glm.hpp>

namespace Engine {

class FollowPlayer : public ScriptableEntity {
public:
  void OnUpdate(float ts) override {
    auto scene = Application::Get().GetScene();
    if (!scene)
      return;

    Entity *player = nullptr;
    player = scene->GetPrimaryCameraEntity();

    if (!player) {
      for (auto &entity : scene->GetEntities()) {
        if (entity.Name == "Player") {
          player = &entity;
          break;
        }
      }
    }

    if (player && player->GetID() != m_Entity->GetID()) {
      glm::vec3 targetPos = player->Transform.Translation;
      glm::vec3 currentPos = m_Entity->Transform.Translation;

      glm::vec3 direction = targetPos - currentPos;
      float distance = glm::length(direction);

      if (distance > 1.5f) {
        direction = glm::normalize(direction);
        float speed =
            20.0f;

        if (m_Entity->HasRigidBody) {
          SetLinearVelocity(direction * speed);
        } else {
          m_Entity->Transform.Translation += direction * speed * ts;
        }

        float angle = atan2(direction.x, direction.z);
        m_Entity->Transform.Rotation.y = angle;
      } else if (m_Entity->HasRigidBody) {
        SetLinearVelocity({0.0f, 0.0f, 0.0f});
      }
    }
  }
};

}
