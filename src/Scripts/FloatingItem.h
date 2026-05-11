#pragma once

#include "../Engine/Scene/Components.h"
#include "../Engine/Scene/Entity.h"
#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

class FloatingItem : public ScriptableEntity {
public:
  void OnCreate() {}

  void OnDestroy() {}

  void OnUpdate(float ts) override {
    float rotationSpeed = 2.0f;
    m_Entity->Transform.Rotation.y += rotationSpeed * ts;

    m_Time += ts;
    float frequency = 2.0f;
    float amplitude = 0.5f;

    if (m_IsFirstUpdate) {
      m_InitialY = m_Entity->Transform.Translation.y;
      m_IsFirstUpdate = false;
    }

    m_Entity->Transform.Translation.y =
        m_InitialY + std::sin(m_Time * frequency) * amplitude;
  }

private:
  float m_Time = 0.0f;
  float m_InitialY = 0.0f;
  bool m_IsFirstUpdate = true;
};

}
